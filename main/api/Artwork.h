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
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <optional>
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

// A picture at a size that the loader has the host download from the backend. The host holds the
// download until it hands the backend's answer to answered() or unanswered(); one it lets go of
// before then brings no answer, which the loader reads from `held`.
struct ArtworkDownload {
    std::string artwork_id;
    int32_t size{};
    // Lives for as long as the host holds a copy of the download.
    std::shared_ptr<void> held;
};

// What became of the backend's answer to a download.
struct ArtworkOutcome {
    // What was wrong with the answer, for the log, or nullptr.
    const char* problem;
    // How many views it was handed to: none once every view that asked is gone or wants another
    // picture.
    size_t handed;
};

// Answers the views' requests for artwork from the cache, and from the backend through the host's
// `fetch`, one download at a time. A view that asks for a picture whose download is under way or
// waits to start waits on that download: there is one per picture and size. A download that has
// not started when no view wants it any longer is not made; one under way is read to its end and
// its image kept, whether or not a view still waits for it. Runs on the UI task.
class ArtworkLoader {
public:
    using Fetch = std::function<void(ArtworkDownload&&)>;

private:
    // A picture at a size, and the views' requests that wait for it.
    struct Download {
        std::string artwork_id;
        int32_t size;
        std::vector<ArtworkRequest> waiting;
    };

    Fetch _fetch;
    ArtworkCache _cache;
    bool _screen_lit{true};
    // The downloads not started, oldest first.
    std::deque<Download> _queued;
    std::optional<Download> _current;
    // Expires once the host lets go of _current's download.
    std::weak_ptr<void> _current_held;

public:
    explicit ArtworkLoader(Fetch fetch);
    ArtworkLoader(const ArtworkLoader&) = delete;
    ArtworkLoader& operator=(const ArtworkLoader&) = delete;

    // An image the cache has is handed over before this returns. Any other waits on its download,
    // which starts once the screen is lit and the download before it is done.
    void request(ArtworkRequest request);

    // Says whether the screen is lit, which the host does on every pass of the UI task's loop.
    // While it is dark no download starts; what was asked for meanwhile, and is still wanted, is
    // downloaded when it wakes. A download the host let go of without an answer is found here: the
    // views that wait on it are handed nullptr, as for one the backend did not answer.
    void set_screen_lit(bool lit);

    // The backend's answer to the download under way, which the host hands over whether or not a
    // view still waits for it. An image that passes its checks and fits the cache is kept, and
    // handed to the views that still wait for it; any other answer hands them nullptr.
    ArtworkOutcome answered(int status, ArtworkBody body, size_t length);

    // The backend did not answer the download under way: the views that wait for it are handed
    // nullptr.
    void unanswered();

    const ArtworkCache& get_cache() const { return _cache; }

    // How many downloads wait to start: for the screen, or for the one under way.
    size_t count_waiting() const { return _queued.size(); }

private:
    Download* find(const std::string& artwork_id, int32_t size);
    void start_next();
    void forget_lost();
    size_t finish(const Artwork& artwork);
};
