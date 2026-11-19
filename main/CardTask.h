#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>

#include "Queue.h"
#include "api/Artwork.h"
#include "api/ArtworkStore.h"
#include "board/TfCard.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// The card the artwork loader keeps its pictures on (api/Artwork.h): the board's TF card, through
// ArtworkStore. Every access to the card runs on the card's own task: the mount, a format, the
// clearing at start, and then the reads, the writes and the erases the UI task asks for, in the
// order it asked. The task touches no LVGL, model, I2C or NVS, and hands a read's answer to the UI
// task through its queue.
//
// The card never stops the boot. Until it is mounted, and when it is missing or fails, the card is
// off, and the loader downloads every picture.
class CardTask : public ArtworkCard {
public:
    // What a read brought, on the UI task: the picture's bytes as the card holds them and their
    // length, or nullptr where it has none. Hands them to ArtworkLoader::card_answered().
    using Answered = std::function<void(const ArtworkRead& read, ArtworkBody body, size_t length)>;

private:
    // Mounting: the task has not mounted the card yet. The rest are the store's, and Off is also
    // a card that is not mounted.
    enum class State { Mounting, On, Full, Off };

    struct Erase {
        std::string artwork_id;
        int32_t size;
    };

    struct LogState {};

    using Work = std::variant<ArtworkRead, ArtworkWrite, Erase, LogState>;

    struct Job;

    // One sector. Every byte to and from the card passes through it: the driver transfers into
    // and out of internal RAM as it is, a sector a command, and takes a sector of internal RAM of
    // its own for each transfer that touches PSRAM.
    static constexpr size_t BUFFER_LENGTH = 512;

    Queue* _ui_queue;
    TfCard* _tf_card;
    Answered _answered;
    alignas(4) uint8_t _buffer[BUFFER_LENGTH];
    ArtworkStore _store;
    // The task's control block, which FreeRTOS keeps in internal RAM; its stack is in PSRAM.
    StaticTask_t _task;
    QueueHandle_t _jobs{};
    // Written by the card's task only.
    std::atomic<State> _state{State::Mounting};
    // What the mount found. The card's task only.
    TfCard::Mount _mount{};
    // The reads asked for whose answer has not run on the UI task, or been dropped. The UI task
    // only.
    uint32_t _unanswered{};

public:
    // `format` is the format the backend is asked for artwork in, which is part of a picture's
    // key. `answered` runs on the UI task.
    CardTask(Queue* ui_queue, TfCard* tf_card, const char* format, Answered answered);
    CardTask(const CardTask&) = delete;
    CardTask& operator=(const CardTask&) = delete;

    // Starts the task, which mounts the card. Returns before the card is mounted.
    esp_err_t begin();

    // Logs what the mount found, and what stopped the card since, again: the task mounts the card
    // before esp-mdm ships the log. The task logs it, after the mount where that is still under
    // way.
    void log_state();

    // No read is waiting or under way, and every answer has run on the UI task.
    bool is_idle() const { return _unanswered == 0; }

    bool is_on() const override;
    bool is_writable() const override;
    void read(ArtworkRead&& read) override;
    void write(ArtworkWrite&& write) override;
    void erase(const std::string& artwork_id, int32_t size) override;

private:
    bool post(Work work);
    void run();
    void mount();
    void perform(ArtworkRead& read);
    void perform(ArtworkWrite& write);
    void perform(Erase& erase);
    void perform(LogState& log);
    void log_mount() const;
    void log_stop() const;
    void follow_store(ArtworkStore::State before);
};
