#pragma once

// The artwork the views ask for. The backend prepares it in the form LVGL draws, so the display
// decodes nothing: an answer is checked where it enters and drawn from its bytes as they arrived.
// What was fetched is kept by id and size, within a limit, and nothing is fetched while the screen
// is dark.
//
// It includes LVGL, the C++ standard library and ui/UiHost.h only, so sim/ builds it too and its
// scenes get their artwork the way the device does.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "ui/UiHost.h"

// The bytes of an answer, which live for as long as the artwork read from them does.
using ArtworkBody = std::shared_ptr<const uint8_t>;

// Reads the answer to GET /api/artwork/{artwork_id}?size=<size>&format=lvgl into `artwork`: LVGL
// 9's binary image, a 12-byte header and then `size` by `size` RGB565 pixels, which `artwork`
// draws from `body` as they are. Returns what was wrong with the answer, for the log, or nullptr
// when it was what the API's document says. `body` is at an address a multiple of 4, which LVGL
// asks of the pixels 12 bytes into it.
const char* read_artwork(int status, ArtworkBody body, size_t length, int32_t size, Artwork& artwork);

// The images that were fetched, by artwork id and size. An image a view holds is pinned: it is
// never freed to make room.
class ArtworkCache {
    struct Entry {
        std::string artwork_id;
        int32_t size;
        Artwork artwork;
        size_t bytes;
    };

    // The most recently used first.
    std::list<Entry> _entries;
    size_t _bytes{};

public:
    // What the images together may take: four of the now-playing screen's covers, with room left.
    static constexpr size_t MAX_BYTES = 1536 * 1024;

    // The image, which is the most recently used one from here on, or nullptr.
    Artwork find(const std::string& artwork_id, int32_t size);

    // Keeps an image of `bytes` bytes, and frees the least recently used ones no view holds until
    // it fits. Returns false, and frees nothing, when it cannot fit: the rest is on screen.
    bool add(const std::string& artwork_id, int32_t size, const Artwork& artwork, size_t bytes);

    // Whether the image is here and a view holds it.
    bool is_pinned(const std::string& artwork_id, int32_t size) const;

    size_t get_bytes() const { return _bytes; }
};

// What a view asked for, as UiHost::request_artwork() takes it.
struct ArtworkRequest {
    std::string artwork_id;
    int32_t size{};
    std::weak_ptr<void> scope;
    std::function<void(Artwork)> done;
};

// Answers the views' requests for artwork from the cache, and from the backend through the host's
// `fetch`, which hands the backend's answer to answered(). Runs on the UI task.
class ArtworkLoader {
public:
    using Fetch = std::function<void(ArtworkRequest&&)>;

private:
    Fetch _fetch;
    ArtworkCache _cache;
    bool _screen_lit{true};
    // What was asked for while the screen was dark.
    std::vector<ArtworkRequest> _waiting;

public:
    explicit ArtworkLoader(Fetch fetch);
    ArtworkLoader(const ArtworkLoader&) = delete;
    ArtworkLoader& operator=(const ArtworkLoader&) = delete;

    // An image the cache has is handed over before this returns. Any other is fetched, once the
    // screen is lit.
    void request(ArtworkRequest request);

    // Says whether the screen is lit. While it is dark nothing is fetched; what was asked for
    // meanwhile, and is still wanted, is fetched when it wakes.
    void set_screen_lit(bool lit);

    // The backend's answer to a request `fetch` was given, which the host hands over only while
    // the request's scope lives, as HttpClient does. An image that passes its checks and fits the
    // cache is kept, and handed to the request; any other answer hands it nullptr. Returns what
    // was wrong, for the log, or nullptr.
    const char* answered(const ArtworkRequest& request, int status, ArtworkBody body, size_t length);

    // The backend did not answer a request `fetch` was given: the request is handed nullptr.
    void unanswered(const ArtworkRequest& request);

    const ArtworkCache& get_cache() const { return _cache; }

    // How many requests wait for the screen.
    size_t count_waiting() const { return _waiting.size(); }
};
