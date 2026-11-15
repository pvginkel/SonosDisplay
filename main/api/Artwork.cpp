#include "Artwork.h"

#include <algorithm>
#include <utility>

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
    if (status != 200) {
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

ArtworkLoader::ArtworkLoader(Fetch fetch) : _fetch(std::move(fetch)) {}

void ArtworkLoader::request(ArtworkRequest request) {
    if (auto artwork = _cache.find(request.artwork_id, request.size)) {
        request.done(std::move(artwork));
        return;
    }

    if (_screen_lit) {
        _fetch(std::move(request));
        return;
    }

    // A dark screen can last a night of tracks: what is no longer wanted does not wait with it.
    std::erase_if(_waiting, [](const ArtworkRequest& waiting) { return waiting.scope.expired(); });

    _waiting.push_back(std::move(request));
}

void ArtworkLoader::set_screen_lit(bool lit) {
    if (lit == _screen_lit) {
        return;
    }

    _screen_lit = lit;

    if (!lit) {
        return;
    }

    auto waiting = std::move(_waiting);
    _waiting.clear();

    for (auto& request : waiting) {
        if (!request.scope.expired()) {
            this->request(std::move(request));
        }
    }
}

const char* ArtworkLoader::answered(const ArtworkRequest& request, int status, ArtworkBody body, size_t length) {
    // Two requests for one image can both be on their way: the first answer's image is the one
    // kept, and the one both get.
    auto artwork = _cache.find(request.artwork_id, request.size);

    const char* problem = nullptr;

    if (!artwork) {
        problem = read_artwork(status, std::move(body), length, request.size, artwork);

        if (!problem && !_cache.add(request.artwork_id, request.size, artwork, length)) {
            problem = "the image does not fit the cache beside what is on screen";
            artwork.reset();
        }
    }

    request.done(std::move(artwork));

    return problem;
}

void ArtworkLoader::unanswered(const ArtworkRequest& request) { request.done(nullptr); }
