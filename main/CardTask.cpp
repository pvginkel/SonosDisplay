#include "CardTask.h"

#include <cinttypes>
#include <cstring>
#include <memory>
#include <utility>

#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#include "support.h"

LOG_TAG(CardTask);

// The stack is in PSRAM, where internal RAM has no room for it. A task with its stack there
// cannot write flash, which ESP-IDF asserts (spi_flash's cache_utils.c): the task uses no NVS.
static constexpr uint32_t TASK_STACK_SIZE = 8192;
static constexpr BaseType_t TASK_CORE = 0;

// Above the backend's clients, which run on the same core: a read of a cover waits on the bus a
// sector at a time, and at their priority it would wait for the slice of a client busy with TLS
// each time the bus is done, a tick of 1 ms. The task waits on the bus for most of its work, and
// the UI task is on the other core.
static constexpr UBaseType_t TASK_PRIORITY = 2;

// What may wait at once: a read, the erases of pictures that failed their checks, and the writes
// the loader's limit allows, which are the most of the smallest picture the views ask for, 80 px.
static constexpr UBaseType_t QUEUE_DEPTH = 64;
static constexpr size_t SMALLEST_PICTURE_BYTES = 12 + 80 * 80 * 2;
static_assert(ArtworkLoader::MAX_UNWRITTEN_BYTES / SMALLEST_PICTURE_BYTES < QUEUE_DEPTH);

struct CardTask::Job {
    Work work;
    // When it was asked for, in esp_get_millis()'s time.
    int64_t asked;

    // In PSRAM, as what is numerous is: as many as QUEUE_DEPTH wait.
    static void* operator new(size_t size) noexcept { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
    static void operator delete(void* job) { heap_caps_free(job); }
};

// A picture read from the card goes where a downloaded one does, PSRAM, at an address that is a
// multiple of 4.
static std::shared_ptr<uint8_t> allocate_picture(size_t length) {
    const auto bytes = (uint8_t*)heap_caps_malloc(length, MALLOC_CAP_SPIRAM);
    if (!bytes) {
        return nullptr;
    }

    return std::shared_ptr<uint8_t>(bytes, heap_caps_free);
}

CardTask::CardTask(Queue* ui_queue, TfCard* tf_card, const char* format, Answered answered)
    : _ui_queue(ui_queue),
      _tf_card(tf_card),
      _answered(std::move(answered)),
      _store(TfCard::MOUNT_POINT, format, allocate_picture) {}

esp_err_t CardTask::begin() {
    _jobs = xQueueCreateWithCaps(QUEUE_DEPTH, sizeof(Job*), MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(_jobs, ESP_ERR_NO_MEM, TAG, "Cannot create the card's queue");

    const auto stack = (StackType_t*)heap_caps_malloc(TASK_STACK_SIZE, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(stack, ESP_ERR_NO_MEM, TAG, "Cannot allocate the card task's stack");

    xTaskCreateStaticPinnedToCore([](void* arg) { ((CardTask*)arg)->run(); }, "card", TASK_STACK_SIZE, this,
                                  TASK_PRIORITY, stack, &_task, TASK_CORE);

    return ESP_OK;
}

void CardTask::log_state() {
    if (!post(LogState{})) {
        ESP_LOGW(TAG, "The card's task has no room to log its state");
    }
}

bool CardTask::is_on() const {
    const auto state = _state.load();

    return state == State::On || state == State::Full;
}

bool CardTask::is_writable() const { return _state.load() == State::On; }

// A read goes ahead of the writes and the erases that wait. One the task has no room for is let go
// of, and the loader then downloads the picture.
void CardTask::read(ArtworkRead&& read) {
    const auto size = read.size;

    if (post(std::move(read), true)) {
        _unanswered++;
    } else {
        ESP_LOGW(TAG, "The card's task has no room for a read: artwork at %d px is downloaded", int(size));
    }
}

// A write the task has no room for is let go of: the picture is downloaded again the next time.
void CardTask::write(ArtworkWrite&& write) {
    const auto size = write.size;

    if (!post(std::move(write))) {
        ESP_LOGW(TAG, "The card's task has no room for a write: artwork at %d px is not kept on the card", int(size));
    }
}

// The card then keeps the picture's file, and a read of it fails its checks and erases it again.
void CardTask::erase(const std::string& artwork_id, int32_t size) {
    if (!post(Erase{.artwork_id = artwork_id, .size = size})) {
        ESP_LOGW(TAG, "The card's task has no room for an erase: the card keeps its artwork at %d px", int(size));
    }
}

// Hands the task its next work, on the UI task, which never waits for room: at the back of what
// waits, or with `first` ahead of it. What there is no room for is destroyed before this returns.
bool CardTask::post(Work work, bool first) {
    const auto job = new Job{std::move(work), esp_get_millis()};
    if (job && (first ? xQueueSendToFront(_jobs, &job, 0) : xQueueSendToBack(_jobs, &job, 0)) == pdTRUE) {
        return true;
    }

    delete job;

    return false;
}

void CardTask::run() {
    mount();

    while (true) {
        Job* job;
        xQueueReceive(_jobs, &job, portMAX_DELAY);

        _asked = job->asked;
        std::visit([this](auto& work) { perform(work); }, job->work);

        // Lets go of a write and its bytes: the loader counts them as waiting until here.
        delete job;
    }
}

static bool is_mounted(const TfCard::Mount& mount) {
    return mount.found == TfCard::Found::Mounted || mount.found == TfCard::Found::Formatted;
}

void CardTask::mount() {
    _mount = _tf_card->mount();
    log_mount();

    if (!is_mounted(_mount)) {
        _state = State::Off;
        return;
    }

    _store.begin();

    if (_store.get_state() == ArtworkStore::State::On) {
        _state = State::On;
    } else {
        follow_store(ArtworkStore::State::On);
    }
}

void CardTask::perform(ArtworkRead& read) {
    const auto before = _store.get_state();
    const auto started = esp_get_millis();

    auto picture = _store.read(read.artwork_id, read.size);

    if (picture.body) {
        ESP_LOGI(TAG, "Read artwork at %d px: %zu bytes in %" PRId64 " ms, after %" PRId64 " ms in the queue",
                 int(read.size), picture.length, esp_get_millis() - started, started - _asked);
    } else if (picture.problem) {
        ESP_LOGW(TAG, "The card has no artwork at %d px to hand over: %s", int(read.size), picture.problem);
    }

    follow_store(before);

    struct Answer {
        ArtworkRead read;
        ArtworkBody body;
        size_t length;
    };

    // std::function needs a copyable closure, so the answer travels shared. It holds the read
    // until it has run on the UI task.
    auto answer = std::make_shared<Answer>(Answer{std::move(read), std::move(picture.body), picture.length});

    // Waits for room on the UI task's queue, so no answer is dropped. The UI task never waits on
    // this task.
    _ui_queue->enqueue([this, answer]() {
        _answered(answer->read, std::move(answer->body), answer->length);

        _unanswered--;
    });
}

void CardTask::perform(ArtworkWrite& write) {
    const auto before = _store.get_state();
    const auto started = esp_get_millis();

    if (_store.write(write.artwork_id, write.size, write.body.get(), write.length)) {
        ESP_LOGI(TAG, "Artwork at %d px is on the card: %zu bytes in %" PRId64 " ms", int(write.size), write.length,
                 esp_get_millis() - started);
    }

    follow_store(before);
}

void CardTask::perform(Erase& erase) {
    const auto before = _store.get_state();

    _store.erase(erase.artwork_id, erase.size);

    follow_store(before);
}

void CardTask::perform(LogState&) {
    log_mount();

    if (is_mounted(_mount) && _store.get_state() != ArtworkStore::State::On) {
        log_stop();
    }
}

void CardTask::log_mount() const {
    switch (_mount.found) {
        case TfCard::Found::NoCard:
            ESP_LOGW(TAG, "No TF card answers (%s): artwork is not kept across restarts", esp_err_to_name(_mount.err));
            break;
        case TfCard::Found::Mounted:
            ESP_LOGI(TAG, "The TF card is mounted: %" PRIu32 " MB, %" PRIu32 " KB allocation units, at %" PRIu32 " kHz",
                     _mount.megabytes, _mount.cluster_bytes / 1024, _mount.clock_khz);
            break;
        case TfCard::Found::Formatted:
            ESP_LOGW(TAG,
                     "The TF card could not be mounted, and is mounted after a format: %" PRIu32 " MB, %" PRIu32
                     " KB allocation units, at %" PRIu32 " kHz",
                     _mount.megabytes, _mount.cluster_bytes / 1024, _mount.clock_khz);
            break;
        case TfCard::Found::Failed:
            ESP_LOGE(TAG, "The TF card cannot be mounted (%s): artwork is not kept across restarts",
                     esp_err_to_name(_mount.err));
            break;
    }
    // Its own line, which the log keeps whatever the mount found.
    if (_mount.high_clock_err != ESP_OK) {
        ESP_LOGW(TAG, "The TF card failed at %" PRIu32 " kHz (%s), and was mounted at %" PRIu32 " kHz",
                 TfCard::HIGH_CLOCK_KHZ, esp_err_to_name(_mount.high_clock_err), TfCard::DEFAULT_CLOCK_KHZ);
    }
}

// Says what stopped the store's writes, or turned it off.
void CardTask::log_stop() const {
    const auto& fault = _store.get_fault();

    if (_store.get_state() == ArtworkStore::State::Full) {
        ESP_LOGW(TAG, "The TF card is full, and nothing more is written to it: %s failed with errno %d (%s)",
                 fault.doing, fault.error, strerror(fault.error));
    } else {
        ESP_LOGE(TAG, "The TF card is off until the next restart: %s failed with errno %d (%s)", fault.doing,
                 fault.error, strerror(fault.error));
    }
}

// Tells the UI task, and the log, once, that the store left the state `before`.
void CardTask::follow_store(ArtworkStore::State before) {
    const auto state = _store.get_state();
    if (state == before) {
        return;
    }

    _state = state == ArtworkStore::State::Full ? State::Full : State::Off;

    log_stop();
}
