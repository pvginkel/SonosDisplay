#pragma once

// The artwork the views ask for. The backend prepares it in the form LVGL draws, so the display
// decodes nothing: an answer is checked where it enters and drawn from its bytes as they arrived.
// What was fetched is kept by id and size, within a limit, in memory, and behind that on the
// host's card, where it has one, which is handed every picture downloaded. Nothing is read or
// fetched while the screen is dark.
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
// asks of the pixels 12 bytes into it. A picture the card gives back is read the same way, as the
// answer with status 200 it was kept from.
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

    // Whether the image is here, which leaves it as recently used as it was.
    bool has(const std::string& artwork_id, int32_t size) const;

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

// A picture at a size that the loader has the host read from its card. The host holds the read
// until it hands the card's answer to card_answered(); one it lets go of before then brings no
// answer, which the loader reads from `held`.
struct ArtworkRead {
    std::string artwork_id;
    int32_t size{};
    // Lives for as long as the host holds a copy of the read.
    std::shared_ptr<void> held;
};

// A picture at a size that the loader has the host write to its card: the bytes as the backend
// answered them. The host holds the write, and with it the picture's bytes, until the card has the
// picture or cannot keep it, and then lets go of it; the loader reads from `held` what still waits.
struct ArtworkWrite {
    std::string artwork_id;
    int32_t size{};
    ArtworkBody body;
    size_t length{};
    // Lives for as long as the host holds a copy of the write.
    std::shared_ptr<void> held;
};

// The card the host keeps pictures on, which outlives a restart. The loader calls it on the UI
// task.
class ArtworkCard {
public:
    virtual ~ArtworkCard() = default;

    // Whether the card reads pictures: one that is not there, or that failed, does not.
    virtual bool is_on() const = 0;

    // Whether the card takes pictures to keep: one that is off, or full, does not.
    virtual bool is_writable() const = 0;

    // Reads a picture. The host hands what the card has of it to ArtworkLoader::card_answered(),
    // before this returns or later.
    virtual void read(ArtworkRead&& read) = 0;

    // Keeps a picture, before this returns or later. A picture the card holds is not written
    // again.
    virtual void write(ArtworkWrite&& write) = 0;

    // Deletes the card's file of a picture, where it has one. The card keeps a picture it holds as
    // it is, so one that is not fit to draw goes before the picture is kept again.
    virtual void erase(const std::string& artwork_id, int32_t size) = 0;
};

// What became of an answer: the card's to a read, or the backend's to a download.
struct ArtworkOutcome {
    // What was wrong with the answer, for the log, or nullptr.
    const char* problem;
    // How many views it was handed to. None once every view that asked is gone or wants another
    // picture, and none for a read the card had no picture to draw for: its views wait on, for
    // the download.
    size_t handed;
    // Whether a download that passed its checks was left off a card that takes pictures: too much
    // waits to be written there. The picture is downloaded again the next time.
    bool write_dropped;
};

// Answers the views' requests for artwork from the cache, then from the host's card, then from the
// backend through the host's `fetch`: one read and one download at a time, and neither waits for
// the other. A view that asks for a picture that is being read or downloaded, or waits for either
// to start, waits with it: no picture at a size is read or downloaded twice at once. A read or a
// download that has not started when no view wants it any longer is not made; one under way is
// taken to its end and its image kept, whether or not a view still waits for it. Without a card,
// or with one that is off, every picture is downloaded. A download that passes its checks is also
// handed to a card that takes pictures, to be written there, for as long as what waits to be
// written stays within a limit. Runs on the UI task.
class ArtworkLoader {
public:
    using Fetch = std::function<void(ArtworkDownload&&)>;

    // What the pictures handed to the card and not written yet may take together: two of the
    // now-playing screen's covers, with room left. Their bytes stay in memory until they are
    // written, whether or not the cache still holds them.
    static constexpr size_t MAX_UNWRITTEN_BYTES = 640 * 1024;

private:
    // A picture at a size that is not here yet, and the views' requests that wait for it.
    struct Pending {
        std::string artwork_id;
        int32_t size;
        std::vector<ArtworkRequest> waiting;
    };

    // A picture of `bytes` bytes handed to the card to write.
    struct Unwritten {
        std::string artwork_id;
        int32_t size;
        // The picture's bytes, which the write holds for as long as `held` lives.
        std::weak_ptr<const uint8_t> body;
        size_t bytes;
        // Expires once the host lets go of the write.
        std::weak_ptr<void> held;
    };

    Fetch _fetch;
    ArtworkCard* _card;
    ArtworkCache _cache;
    bool _screen_lit{true};
    // The reads not started, oldest first.
    std::deque<Pending> _unread;
    std::optional<Pending> _reading;
    // Expires once the host lets go of _reading's read.
    std::weak_ptr<void> _reading_held;
    // The downloads not started, oldest first.
    std::deque<Pending> _queued;
    std::optional<Pending> _current;
    // Expires once the host lets go of _current's download.
    std::weak_ptr<void> _current_held;
    // The pictures handed to the card to write, each until the host lets go of it.
    std::vector<Unwritten> _unwritten;

public:
    // `card` is nullptr where the host has no card.
    ArtworkLoader(Fetch fetch, ArtworkCard* card);
    ArtworkLoader(const ArtworkLoader&) = delete;
    ArtworkLoader& operator=(const ArtworkLoader&) = delete;

    // An image the cache has is handed over before this returns, and so is one that waits to be
    // written to the card, from the bytes that wait: the card may read before it writes. Any other
    // waits on the card's read, which starts once the screen is lit and the read before it is
    // done, and where the card has no picture to draw, on its download, which starts once the
    // download before it is done.
    void request(ArtworkRequest request);

    // Says whether the screen is lit, which the host does on every pass of the UI task's loop.
    // While it is dark no read and no download starts; what was asked for meanwhile, and is still
    // wanted, is read or downloaded when it wakes. What the host let go of without an answer is
    // found here. A read is then a picture the card does not have, which is downloaded. The views
    // that wait on a download are handed nullptr, as for one the backend did not answer.
    void set_screen_lit(bool lit);

    // The card's answer to the read under way: the picture's bytes as the card holds them, or
    // nullptr where it has none. A picture that passes the checks of a download and fits the cache
    // is kept, and handed to the views that still wait for it; one that does not fit hands them
    // nullptr. One that fails the checks is erased from the card and, as one the card does not
    // have, downloaded.
    ArtworkOutcome card_answered(ArtworkBody body, size_t length);

    // The backend's answer to the download under way, which the host hands over whether or not a
    // view still waits for it. An image that passes its checks and fits the cache is kept, and
    // handed to the views that still wait for it; any other answer hands them nullptr. One that
    // passes its checks is handed to the card too, whether or not it fits the cache.
    ArtworkOutcome answered(int status, ArtworkBody body, size_t length);

    // The backend did not answer the download under way: the views that wait for it are handed
    // nullptr.
    void unanswered();

    const ArtworkCache& get_cache() const { return _cache; }

    // How many reads and downloads wait to start: for the screen, or for the one under way.
    size_t count_waiting() const { return _unread.size() + _queued.size(); }

    // How many bytes of pictures the card was handed and the host has not let go of yet.
    size_t get_unwritten_bytes() const;

private:
    Pending* find(const std::string& artwork_id, int32_t size);
    bool answer_unwritten(const ArtworkRequest& request);
    void start_next();
    void forget_lost();
    bool write_to_card(ArtworkBody body, size_t length);
    size_t finish(const Artwork& artwork);
};
