#include "Artwork.h"

#include <algorithm>
#include <utility>

static constexpr int STATUS_OK = 200;
static constexpr size_t HEADER_LENGTH = 12;
static constexpr int32_t BYTES_PER_PIXEL = 2;

namespace {

// What LVGL draws from, and the bytes its pixels are in.
struct Image {
    lv_image_dsc_t dsc{};
    ArtworkBody body;
};

}  // namespace

static int32_t read_u16(const uint8_t* bytes) { return bytes[0] | (bytes[1] << 8); }

const char* read_artwork(int status, ArtworkBody body, size_t length, int32_t size, Artwork& artwork) {
    if (status != STATUS_OK) {
        return "not a 200";
    }
    if (length < HEADER_LENGTH) {
        return "the body is shorter than an image's header";
    }

    // The header, little-endian: the magic byte, the colour format, 16 bits of flags, then the
    // width, the height and the stride as 16 bits each, and 16 bits reserved.
    const auto header = body.get();

    if (header[0] != LV_IMAGE_HEADER_MAGIC) {
        return "the magic byte is not LVGL 9's";
    }
    if (header[1] != LV_COLOR_FORMAT_RGB565) {
        return "the colour format is not RGB565";
    }
    // A flag would have LVGL decompress the pixels, or take them for premultiplied.
    if (read_u16(header + 2) != 0) {
        return "the flags are not 0";
    }
    if (read_u16(header + 4) != size || read_u16(header + 6) != size) {
        return "the image is not of the size asked for";
    }
    if (read_u16(header + 8) != size * BYTES_PER_PIXEL) {
        return "the stride is not two bytes a pixel";
    }
    if (length != HEADER_LENGTH + size_t(size) * size_t(size) * BYTES_PER_PIXEL) {
        return "the length is not the header's and the pixels'";
    }

    auto image = std::make_shared<Image>();
    image->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    image->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    image->dsc.header.w = uint32_t(size);
    image->dsc.header.h = uint32_t(size);
    image->dsc.header.stride = uint32_t(size * BYTES_PER_PIXEL);
    image->dsc.data = body.get() + HEADER_LENGTH;
    image->dsc.data_size = uint32_t(length - HEADER_LENGTH);
    image->body = std::move(body);

    artwork = Artwork(image, &image->dsc);

    return nullptr;
}

// The cache holds one reference to an image, and each view that shows it one more.
static bool is_held(const Artwork& artwork) { return artwork.use_count() > 1; }

Artwork ArtworkCache::find(const std::string& artwork_id, int32_t size) {
    const auto entry = std::find_if(_entries.begin(), _entries.end(), [&](const Entry& candidate) {
        return candidate.size == size && candidate.artwork_id == artwork_id;
    });
    if (entry == _entries.end()) {
        return nullptr;
    }

    _entries.splice(_entries.begin(), _entries, entry);

    return entry->artwork;
}

bool ArtworkCache::add(const std::string& artwork_id, int32_t size, const Artwork& artwork, size_t bytes) {
    size_t pinned = 0;
    for (const auto& entry : _entries) {
        if (is_held(entry.artwork)) {
            pinned += entry.bytes;
        }
    }

    if (pinned + bytes > MAX_BYTES) {
        return false;
    }

    auto entry = _entries.end();
    while (_bytes + bytes > MAX_BYTES) {
        --entry;

        if (!is_held(entry->artwork)) {
            _bytes -= entry->bytes;
            entry = _entries.erase(entry);
        }
    }

    _entries.push_front(Entry{.artwork_id = artwork_id, .size = size, .artwork = artwork, .bytes = bytes});
    _bytes += bytes;

    return true;
}

bool ArtworkCache::is_pinned(const std::string& artwork_id, int32_t size) const {
    return std::any_of(_entries.begin(), _entries.end(), [&](const Entry& entry) {
        return entry.size == size && entry.artwork_id == artwork_id && is_held(entry.artwork);
    });
}

bool ArtworkCache::has(const std::string& artwork_id, int32_t size) const {
    return std::any_of(_entries.begin(), _entries.end(),
                       [&](const Entry& entry) { return entry.size == size && entry.artwork_id == artwork_id; });
}

ArtworkLoader::ArtworkLoader(Fetch fetch, ArtworkCard* card) : _fetch(std::move(fetch)), _card(card) {}

// A picture is wanted while a view that waits for it lives and has not asked for another picture.
static bool is_wanted(const std::vector<ArtworkRequest>& waiting) {
    return std::any_of(waiting.begin(), waiting.end(),
                       [](const ArtworkRequest& request) { return !request.scope.expired(); });
}

// Hands `artwork` to the views that still wait for it, and returns how many they are.
static size_t hand_over(const std::vector<ArtworkRequest>& waiting, const Artwork& artwork) {
    size_t handed = 0;
    for (const auto& request : waiting) {
        if (!request.scope.expired()) {
            request.done(artwork);
            handed++;
        }
    }

    return handed;
}

void ArtworkLoader::request(ArtworkRequest request) {
    if (auto artwork = _cache.find(request.artwork_id, request.size)) {
        request.done(std::move(artwork));
        return;
    }

    if (answer_unwritten(request)) {
        return;
    }

    // Before the request joins it: a read or a download the host let go of brings no answer.
    forget_lost();

    // What is no longer wanted does not wait: a dark screen can last a night of tracks, and a quick
    // scroll of Home's tiles asks for many pictures.
    const auto is_unwanted = [](const Pending& pending) { return !is_wanted(pending.waiting); };
    std::erase_if(_unread, is_unwanted);
    std::erase_if(_queued, is_unwanted);

    if (const auto pending = find(request.artwork_id, request.size)) {
        std::erase_if(pending->waiting, [](const ArtworkRequest& waiting) { return waiting.scope.expired(); });
        pending->waiting.push_back(std::move(request));
    } else {
        auto& lane = _card ? _unread : _queued;
        lane.push_back(Pending{.artwork_id = request.artwork_id, .size = request.size, .waiting = {}});
        lane.back().waiting.push_back(std::move(request));
    }

    start_next();
}

void ArtworkLoader::set_screen_lit(bool lit) {
    _screen_lit = lit;

    start_next();
}

ArtworkOutcome ArtworkLoader::card_answered(ArtworkBody body, size_t length) {
    auto read = std::move(*_reading);
    _reading.reset();

    Artwork artwork;
    const char* problem = nullptr;

    if (body) {
        problem = read_artwork(STATUS_OK, std::move(body), length, read.size, artwork);
        if (problem) {
            _card->erase(read.artwork_id, read.size);
        }
    }

    if (!artwork) {
        _queued.push_back(std::move(read));

        start_next();

        return ArtworkOutcome{.problem = problem, .handed = 0, .write_dropped = false};
    }

    if (!_cache.add(read.artwork_id, read.size, artwork, length)) {
        problem = "the image does not fit the cache beside what is on screen";
        artwork.reset();
    }

    // A view's `done` may ask for another picture, and the read is gone by then.
    const auto handed = hand_over(read.waiting, artwork);

    start_next();

    return ArtworkOutcome{.problem = problem, .handed = handed, .write_dropped = false};
}

ArtworkOutcome ArtworkLoader::answered(int status, ArtworkBody body, size_t length) {
    Artwork artwork;
    auto problem = read_artwork(status, body, length, _current->size, artwork);
    auto write_dropped = false;

    if (!problem) {
        write_dropped = !write_to_card(std::move(body), length);

        if (!_cache.add(_current->artwork_id, _current->size, artwork, length)) {
            problem = "the image does not fit the cache beside what is on screen";
            artwork.reset();
        }
    }

    const auto handed = finish(artwork);

    start_next();

    return ArtworkOutcome{.problem = problem, .handed = handed, .write_dropped = write_dropped};
}

void ArtworkLoader::unanswered() {
    finish(nullptr);

    start_next();
}

size_t ArtworkLoader::get_unwritten_bytes() const {
    size_t bytes = 0;
    for (const auto& unwritten : _unwritten) {
        if (!unwritten.held.expired()) {
            bytes += unwritten.bytes;
        }
    }

    return bytes;
}

// Hands the view a picture that waits to be written to the card, from the bytes that wait, and
// keeps it in the cache again where it fits; one that does not hands the view nullptr, as a read
// of the card does. Returns false where no write of the picture waits.
bool ArtworkLoader::answer_unwritten(const ArtworkRequest& request) {
    const auto unwritten = std::find_if(_unwritten.begin(), _unwritten.end(), [&](const Unwritten& candidate) {
        return !candidate.held.expired() && candidate.size == request.size &&
               candidate.artwork_id == request.artwork_id;
    });
    if (unwritten == _unwritten.end()) {
        return false;
    }

    auto body = unwritten->body.lock();
    const auto length = unwritten->bytes;

    // It passed these checks when it was downloaded.
    Artwork artwork;
    if (!body || read_artwork(STATUS_OK, std::move(body), length, request.size, artwork)) {
        return false;
    }

    if (!_cache.add(request.artwork_id, request.size, artwork, length)) {
        artwork.reset();
    }

    // A view's `done` may ask for another picture, and change what waits to be written.
    request.done(std::move(artwork));

    return true;
}

ArtworkLoader::Pending* ArtworkLoader::find(const std::string& artwork_id, int32_t size) {
    const auto is_of = [&](const Pending& pending) { return pending.size == size && pending.artwork_id == artwork_id; };

    if (_reading && is_of(*_reading)) {
        return &*_reading;
    }
    if (_current && is_of(*_current)) {
        return &*_current;
    }

    for (const auto lane : {&_unread, &_queued}) {
        const auto pending = std::find_if(lane->begin(), lane->end(), is_of);
        if (pending != lane->end()) {
            return &*pending;
        }
    }

    return nullptr;
}

void ArtworkLoader::start_next() {
    forget_lost();

    while (!_reading && _screen_lit && !_unread.empty()) {
        auto read = std::move(_unread.front());
        _unread.pop_front();

        if (!is_wanted(read.waiting)) {
            continue;
        }

        // A card that is off has nothing to read.
        if (!_card->is_on()) {
            _queued.push_back(std::move(read));
            continue;
        }

        auto held = std::make_shared<int>(0);
        _reading_held = held;
        _reading = std::move(read);

        // The host may hand over the answer before this returns.
        _card->read(ArtworkRead{.artwork_id = _reading->artwork_id, .size = _reading->size, .held = std::move(held)});
    }

    while (!_current && _screen_lit && !_queued.empty()) {
        auto download = std::move(_queued.front());
        _queued.pop_front();

        if (!is_wanted(download.waiting)) {
            continue;
        }

        auto held = std::make_shared<int>(0);
        _current_held = held;
        _current = std::move(download);

        // The host may hand over the answer before this returns.
        _fetch(ArtworkDownload{.artwork_id = _current->artwork_id, .size = _current->size, .held = std::move(held)});
    }
}

void ArtworkLoader::forget_lost() {
    if (_reading && _reading_held.expired()) {
        _queued.push_back(std::move(*_reading));
        _reading.reset();
    }

    if (_current && _current_held.expired()) {
        finish(nullptr);
    }
}

// Hands the picture of the download under way to the card. Returns false where a card that takes
// pictures is not handed it: what waits to be written there leaves it no room.
bool ArtworkLoader::write_to_card(ArtworkBody body, size_t length) {
    if (!_card || !_card->is_writable()) {
        return true;
    }

    std::erase_if(_unwritten, [](const Unwritten& unwritten) { return unwritten.held.expired(); });

    if (get_unwritten_bytes() + length > MAX_UNWRITTEN_BYTES) {
        return false;
    }

    auto held = std::make_shared<int>(0);
    _unwritten.push_back(Unwritten{
        .artwork_id = _current->artwork_id,
        .size = _current->size,
        .body = body,
        .bytes = length,
        .held = held,
    });

    // The host may let go of the write before this returns.
    _card->write(ArtworkWrite{
        .artwork_id = _current->artwork_id,
        .size = _current->size,
        .body = std::move(body),
        .length = length,
        .held = std::move(held),
    });

    return true;
}

// The download under way ends, and `artwork` goes to the views that still wait for it. A view's
// `done` may ask for another picture, so the download is gone before the first runs.
size_t ArtworkLoader::finish(const Artwork& artwork) {
    const auto download = std::move(*_current);
    _current.reset();

    return hand_over(download.waiting, artwork);
}
