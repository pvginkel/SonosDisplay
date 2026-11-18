#pragma once

// UiHost on fixtures: what the device gets from IoT Support and the backend, made up.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "CardFolder.h"
#include "UiHost.h"
#include "api/Answers.h"
#include "api/Artwork.h"
#include "api/ArtworkStore.h"
#include "lvgl.h"

// A piece of artwork the views had fetched from the backend, or read from the card.
struct ArtworkFetch {
    std::string artwork_id;
    int32_t size;

    bool operator==(const ArtworkFetch&) const = default;
};

// A change of a group the views had sent to the backend: `speaker_id` joins the group of
// `group_of`, or leaves it.
struct GroupChangeRequest {
    std::string speaker_id;
    std::string group_of;
    bool adds;

    bool operator==(const GroupChangeRequest&) const = default;
};

class SimHost : public UiHost, private ArtworkCard {
public:
    // A touch of the screen: where the finger goes down, and where it is when it lifts.
    struct Touch {
        lv_point_t from;
        lv_point_t to;
    };

    // The status of an answer that did not come: the backend could not be reached.
    static constexpr int UNREACHABLE = 0;

    // A list the backend gives in pages: what the backend answers a request for one of its pages,
    // and the requests the views made. A page is a Page, or one that holds more of the answer.
    template <typename Item, typename PageOf = Page<Item>>
    class ListFixture {
    public:
        using Reader = const char* (*)(int status, const char* body, size_t length, PageOf& page);

    private:
        struct Request {
            uint32_t number;
            std::weak_ptr<void> scope;
            std::function<void(PageOf)> done;
        };

        const char* _name;
        Reader _read;
        std::optional<int> _status;
        std::vector<std::string> _pages;
        std::vector<uint32_t> _requests;
        std::vector<std::string> _parent_ids;
        std::vector<Request> _held;

    public:
        ListFixture(const char* name, Reader read) : _name(name), _read(read) {}

        // What the backend answers a request for page n from here on: the nth of `pages`, with
        // `status`, read as the device reads it. A request for a page past them stays on its way
        // until answer(); without this, every request does.
        void serve(int status, std::vector<std::string> pages) {
            _status = status;
            _pages = std::move(pages);
        }

        // The backend's answer to the oldest request on its way, read as the device reads it. As
        // on the device, the answer to a view that is gone is dropped.
        void answer(int status, const char* body) {
            if (_held.empty()) {
                fprintf(stderr, "render: the %s were answered, and none were asked for\n", _name);
                exit(1);
            }

            const auto request = std::move(_held.front());
            _held.erase(_held.begin());

            answer(request, status, body);
        }

        // The pages the views asked for, in order.
        const std::vector<uint32_t>& get_requests() const { return _requests; }

        // The id of the artist, the album or the chosen speaker whose list each request was for, in
        // order: empty for a list of the library's.
        const std::vector<std::string>& get_parent_ids() const { return _parent_ids; }

        // A view asks for page `number` of the list of the artist, album or speaker with this id.
        void request(std::string parent_id, uint32_t number, std::weak_ptr<void> scope,
                     std::function<void(PageOf)> done) {
            _requests.push_back(number);
            _parent_ids.push_back(std::move(parent_id));

            Request request{number, std::move(scope), std::move(done)};
            if (_status && number >= 1 && number <= _pages.size()) {
                answer(request, *_status, _pages[number - 1].c_str());
            } else {
                _held.push_back(std::move(request));
            }
        }

    private:
        void answer(const Request& request, int status, const char* body) const {
            if (request.scope.expired()) {
                return;
            }

            PageOf page;
            if (status == UNREACHABLE) {
                page.state = LoadState::Unreachable;
            } else if (const auto problem = _read(status, body, strlen(body), page)) {
                printf("W (SimHost) GET %s page %u answered %d, %s: %s\n", _name, unsigned(request.number), status,
                       problem, page.error.c_str());
            }

            request.done(std::move(page));
        }
    };

    // An item of the library the backend gives on its own: what the backend answers a request for
    // one, and the ids the views asked for.
    template <typename Item>
    class ItemFixture {
    public:
        using Reader = const char* (*)(int status, const char* body, size_t length, Item& item, std::string& error);

    private:
        struct Served {
            int status;
            std::string body;
        };

        struct Request {
            std::string id;
            std::weak_ptr<void> scope;
            std::function<void(std::optional<Item>)> done;
        };

        const char* _name;
        Reader _read;
        std::map<std::string, Served> _served;
        std::vector<std::string> _requests;
        std::vector<Request> _held;

    public:
        ItemFixture(const char* name, Reader read) : _name(name), _read(read) {}

        // What the backend answers a request for the item with this id from here on: `body`, with
        // `status`, read as the device reads it. A request for an item not served stays on its way
        // until answer().
        void serve(const std::string& id, int status, std::string body) {
            _served[id] = Served{status, std::move(body)};
        }

        // The backend's answer to the oldest request on its way, read as the device reads it. As
        // on the device, the answer to a view that is gone is dropped.
        void answer(int status, const char* body) {
            if (_held.empty()) {
                fprintf(stderr, "render: an %s was answered, and none was asked for\n", _name);
                exit(1);
            }

            const auto request = std::move(_held.front());
            _held.erase(_held.begin());

            answer(request, status, body);
        }

        // The ids of the items the views asked for, in order.
        const std::vector<std::string>& get_requests() const { return _requests; }

        // A view asks for the item with this id.
        void request(std::string id, std::weak_ptr<void> scope, std::function<void(std::optional<Item>)> done) {
            _requests.push_back(id);

            Request request{std::move(id), std::move(scope), std::move(done)};
            if (const auto served = _served.find(request.id); served != _served.end()) {
                answer(request, served->second.status, served->second.body.c_str());
            } else {
                _held.push_back(std::move(request));
            }
        }

    private:
        void answer(const Request& request, int status, const char* body) const {
            if (request.scope.expired()) {
                return;
            }

            Item item;
            std::string error;
            if (status == UNREACHABLE) {
                request.done(std::nullopt);
            } else if (const auto problem = _read(status, body, strlen(body), item, error)) {
                printf("W (SimHost) GET %s %s answered %d, %s: %s\n", _name, request.id.c_str(), status, problem,
                       error.c_str());

                request.done(std::nullopt);
            } else {
                request.done(std::move(item));
            }
        }
    };

private:
    struct Answer {
        int status;
        std::string body;
    };

    // A change of a group sent to the backend and not answered yet.
    struct UnansweredGroupChange {
        uint32_t id;
        bool adds;
    };

    // A change of a favourite sent to the backend and not answered yet.
    struct UnansweredFavouriteChange {
        uint32_t id;
        bool marks;
    };

    struct ArtworkAnswer {
        int status;
        std::vector<uint8_t> body;
    };

    Model _model;
    std::vector<std::optional<Touch>> _taps;
    // What answers on LVGL's clock, and from when.
    std::vector<std::pair<uint32_t, std::function<void()>>> _later;
    std::vector<bool> _motion_settings;
    std::vector<Command> _commands;
    std::optional<Answer> _command_answer;
    std::vector<std::pair<uint32_t, Command>> _unanswered_commands;
    std::optional<Answer> _next_playback;
    // The stamp of the fetch of the playback that waits to be answered, if one does.
    std::optional<FetchStamp> _playback_fetch;
    std::vector<int> _volume_changes;
    std::vector<std::string> _volume_change_bodies;
    std::vector<uint32_t> _unanswered_volume_changes;
    std::optional<Answer> _volume_change_answer;
    std::optional<Answer> _next_volume;
    // The stamp of the fetch of the volume that waits to be answered, if one does.
    std::optional<FetchStamp> _volume_fetch;
    std::optional<Answer> _next_speakers;
    std::vector<GroupChangeRequest> _group_changes;
    std::vector<UnansweredGroupChange> _unanswered_group_changes;
    std::optional<Answer> _group_change_answer;
    std::vector<QueueRequest> _queue_requests;
    std::vector<std::string> _queue_request_bodies;
    std::vector<uint32_t> _unanswered_queue_requests;
    std::optional<Answer> _queue_answer;
    std::vector<FavouriteRequest> _favourite_requests;
    std::vector<std::string> _favourite_request_bodies;
    std::vector<UnansweredFavouriteChange> _unanswered_favourite_changes;
    std::optional<Answer> _favourite_answer;
    // The folder of a host that was given none.
    std::unique_ptr<CardFolder> _own_card_folder;
    uint8_t _card_buffer[512];
    ArtworkStore _card;
    std::vector<ArtworkFetch> _card_reads;
    bool _card_held{};
    std::optional<ArtworkRead> _held_read;
    ArtworkLoader _artwork{[this](ArtworkDownload&& download) { fetch_artwork(std::move(download)); }, this};
    std::vector<ArtworkFetch> _artwork_fetches;
    std::optional<ArtworkAnswer> _artwork_answer;
    bool _artwork_held{};
    bool _artwork_reachable{true};
    bool _screen_lit{true};
    std::optional<ArtworkDownload> _held_artwork;
    size_t _artwork_unwaited{};
    ListFixture<Artist> _artists{"artists", read_artists};
    ListFixture<Album> _albums{"albums", read_albums};
    ListFixture<AlbumTrack> _tracks{"tracks", read_tracks};
    ItemFixture<Artist> _artist{"artist", read_one_artist};
    ItemFixture<Album> _album{"album", read_one_album};
    ListFixture<Playlist> _playlists{"playlists", read_playlists};
    ListFixture<Favourite> _favourites{"favourites", read_favourites};
    ListFixture<QueueEntry, QueuePage> _queue{"queue", read_queue};
    std::optional<uint32_t> _queue_current_page;
    std::vector<bool> _queue_locates;
    std::vector<std::string> _clears;
    std::optional<Answer> _clear_answer;

public:
    // From one of a scene's touches to the next: what the first one changed is on screen by then.
    static constexpr uint32_t TAP_INTERVAL_MS = 200;

    // The format the card keeps its pictures under: the one the device asks the backend for.
    static constexpr char CARD_FORMAT[] = "lvgl";

    // With a card of its own, a new and empty folder that is gone with the host.
    SimHost();

    // With the card in `card_folder` and what it holds, as the display finds its card when it
    // starts. A folder that is not there is a card that failed: it is off.
    explicit SimHost(const std::string& card_folder);

    Model& model() { return _model; }
    const Model& get_model() const override { return _model; }
    void choose_speaker(const std::string& id) override;
    void set_motion_wakes(bool wakes) override;
    void refresh_speakers() override;
    void refresh_playback() override;
    void refresh_volume() override;
    void change_volume(int step) override;
    void send_command(Command command) override;
    void add_to_group(const std::string& speaker_id) override;
    void remove_from_group(const std::string& speaker_id) override;
    void queue_item(const QueueRequest& request) override;
    void change_favourite(const FavouriteRequest& request) override;
    void request_artists(uint32_t number, std::weak_ptr<void> scope, std::function<void(Page<Artist>)> done) override;
    void request_albums(const std::string& artist_id, uint32_t number, std::weak_ptr<void> scope,
                        std::function<void(Page<Album>)> done) override;
    void request_tracks(const std::string& album_id, uint32_t number, std::weak_ptr<void> scope,
                        std::function<void(Page<AlbumTrack>)> done) override;
    void request_artist(const std::string& artist_id, std::weak_ptr<void> scope,
                        std::function<void(std::optional<Artist>)> done) override;
    void request_album(const std::string& album_id, std::weak_ptr<void> scope,
                       std::function<void(std::optional<Album>)> done) override;
    void request_playlists(uint32_t number, std::weak_ptr<void> scope,
                           std::function<void(Page<Playlist>)> done) override;
    void request_favourites(uint32_t number, std::weak_ptr<void> scope,
                            std::function<void(Page<Favourite>)> done) override;
    void request_queue(uint32_t number, bool locate, std::weak_ptr<void> scope,
                       std::function<void(QueuePage)> done) override;
    void clear_queue() override;
    void request_artwork(const std::string& artwork_id, int32_t size, std::weak_ptr<void> scope,
                         std::function<void(Artwork)> done) override;

    // What the device does on every turn of its loop: the answers due by now come, a change of a
    // group or of the volume past its time limit is settled and the speakers or the volume are
    // asked for again, a request to queue an item or a change of a favourite past its time limit
    // is settled, and the artwork's loader is told whether the screen is lit.
    void process();

    // Runs `answer` in process() once LVGL's clock has moved on by `ms` from now, as an answer that
    // comes that late.
    void answer_after(uint32_t ms, std::function<void()> answer);

    // The backend's answer to GET /api/speakers, read as the device reads it.
    void answer_speakers(int status, const char* body);

    // What the backend answers the next time the speakers are asked for. Without it the request
    // stays unanswered.
    void answer_next_speakers(int status, const char* body) { _next_speakers = Answer{status, body}; }

    // What the backend answers every change of a group from here on, read as the device reads it.
    // Without it a change stays on its way.
    void answer_group_changes(int status, const char* body) { _group_change_answer = Answer{status, body}; }

    // The backend's answer to the oldest change of a group that answer_group_changes() did not
    // answer, read as the device reads it: an answer to a change that is settled by now changes
    // nothing.
    void answer_group_change(int status, const char* body);

    // How many changes of groups are not answered yet, settled ones among them.
    size_t count_unanswered_group_changes() const { return _unanswered_group_changes.size(); }

    // The changes of groups the views have sent, in order.
    const std::vector<GroupChangeRequest>& get_group_changes() const { return _group_changes; }

    // The backend's answer to GET /api/speakers/{speaker_id}/playback, read as the device reads
    // it: the answer to the fetch last asked for, or to one asked for now where none waits.
    void answer_playback(int status, const char* body);

    // What the backend answers the next time the playback is asked for. Without it the request
    // stays unanswered.
    void answer_next_playback(int status, const char* body) { _next_playback = Answer{status, body}; }

    // What the backend answers every command from here on, read as the device reads it. Without
    // it a command stays on its way.
    void answer_commands(int status, const char* body) { _command_answer = Answer{status, body}; }

    // The backend's answer to the oldest command that answer_commands() did not answer, read as the
    // device reads it: an answer to a command that is settled by now changes nothing.
    void answer_command(int status, const char* body);

    // How many commands are not answered yet, settled ones among them.
    size_t count_unanswered_commands() const { return _unanswered_commands.size(); }

    // What the views have set the motion setting to, in order, a setting it had already among them.
    const std::vector<bool>& get_motion_settings() const { return _motion_settings; }

    // The commands the views have sent, in order.
    const std::vector<Command>& get_commands() const { return _commands; }

    // The backend's answer to GET /api/speakers/{speaker_id}, read as the device reads it: the
    // answer to the fetch last asked for, or to one asked for now where none waits.
    void answer_volume(int status, const char* body);

    // What the backend answers the next time the volume is asked for. Without it the request
    // stays unanswered.
    void answer_next_volume(int status, const char* body) { _next_volume = Answer{status, body}; }

    // The backend's answer to the oldest change of the volume that answer_volume_changes() did not
    // answer, read as the device reads it: an answer to a change that is settled by now changes
    // nothing. The steps pressed since go as the next change.
    void answer_volume_change(int status, const char* body);

    // How many changes of the volume are not answered yet, settled ones among them.
    size_t count_unanswered_volume_changes() const { return _unanswered_volume_changes.size(); }

    // What the backend answers every change of the volume from here on. Without it a change
    // stays on its way.
    void answer_volume_changes(int status, const char* body) { _volume_change_answer = Answer{status, body}; }

    // The steps of the changes of the volume sent to the backend, in order.
    const std::vector<int>& get_volume_changes() const { return _volume_changes; }

    // The bodies of the changes of the volume sent to the backend, in order, as the device writes
    // them.
    const std::vector<std::string>& get_volume_change_bodies() const { return _volume_change_bodies; }

    // What the backend answers every request to queue an item from here on, read as the device
    // reads it. Without it a request stays on its way.
    void answer_queue_requests(int status, const char* body) { _queue_answer = Answer{status, body}; }

    // The backend's answer to the oldest request to queue an item that answer_queue_requests() did
    // not answer, read as the device reads it: an answer to a request that is settled by now
    // changes nothing.
    void answer_queue_request(int status, const char* body);

    // How many requests to queue an item are not answered yet, settled ones among them.
    size_t count_unanswered_queue_requests() const { return _unanswered_queue_requests.size(); }

    // The requests to queue an item the views have sent, in order, and their bodies, as the device
    // writes them.
    const std::vector<QueueRequest>& get_queue_requests() const { return _queue_requests; }
    const std::vector<std::string>& get_queue_request_bodies() const { return _queue_request_bodies; }

    // What the backend answers every change of a favourite from here on, read as the device reads
    // it. Without it a change stays on its way.
    void answer_favourite_changes(int status, const char* body) { _favourite_answer = Answer{status, body}; }

    // The backend's answer to the oldest change of a favourite that answer_favourite_changes() did
    // not answer, read as the device reads it: an answer to a change that is settled by now
    // changes nothing.
    void answer_favourite_change(int status, const char* body);

    // How many changes of a favourite are not answered yet, settled ones among them.
    size_t count_unanswered_favourite_changes() const { return _unanswered_favourite_changes.size(); }

    // The changes of a favourite the views have sent, in order, and their bodies, as the device
    // writes them: an unmark, a DELETE, has none.
    const std::vector<FavouriteRequest>& get_favourite_requests() const { return _favourite_requests; }
    const std::vector<std::string>& get_favourite_request_bodies() const { return _favourite_request_bodies; }

    // The event task_event with this payload, which the event stream brings, read as the device
    // reads it.
    void send_task_event(const char* payload);

    // The event playback_changed or queue_changed, `type`, with this payload, which names the
    // speaker it is about, read as the device reads it.
    void send_speaker_event(const char* type, const char* payload);

    // The event favourites_changed, which the event stream brings, as the device takes it.
    void send_favourites_changed();

    // The event stream is lost, or is back after it was away, as the device takes each: once back,
    // what the events sent meanwhile would have said is fetched again.
    void lose_stream();
    void connect_stream();

    // What the backend answers every request to clear the queue from here on, read as the device
    // reads it. Without it a request stays on its way.
    void answer_clears(int status, const char* body) { _clear_answer = Answer{status, body}; }

    // The speakers whose queue the views asked to clear, in order.
    const std::vector<std::string>& get_clears() const { return _clears; }

    // The library's lists.
    ListFixture<Artist>& artists() { return _artists; }
    const ListFixture<Artist>& artists() const { return _artists; }
    ListFixture<Album>& albums() { return _albums; }
    const ListFixture<Album>& albums() const { return _albums; }
    ListFixture<AlbumTrack>& tracks() { return _tracks; }
    const ListFixture<AlbumTrack>& tracks() const { return _tracks; }
    ListFixture<Playlist>& playlists() { return _playlists; }
    const ListFixture<Playlist>& playlists() const { return _playlists; }

    // The library's artists and albums, each asked for on its own.
    ItemFixture<Artist>& artist() { return _artist; }
    const ItemFixture<Artist>& artist() const { return _artist; }
    ItemFixture<Album>& album() { return _album; }
    const ItemFixture<Album>& album() const { return _album; }

    // The favourites.
    ListFixture<Favourite>& favourites() { return _favourites; }
    const ListFixture<Favourite>& favourites() const { return _favourites; }

    // The queue of the chosen speaker's group, a request for which is recorded with the page it
    // was answered with: at the loaded track, the one set_queue_current_page() names.
    ListFixture<QueueEntry, QueuePage>& queue() { return _queue; }
    const ListFixture<QueueEntry, QueuePage>& queue() const { return _queue; }

    // From here on a request at the loaded track is answered with this page, the one that holds
    // it; without it, with the page asked for.
    void set_queue_current_page(uint32_t page) { _queue_current_page = page; }

    // Whether each request for the queue was at the loaded track, in order.
    const std::vector<bool>& get_queue_locates() const { return _queue_locates; }

    // What the backend answers a request for artwork with: LVGL 9's binary image, a deterministic
    // pattern of two colours that the id decides, `size` by `size` pixels.
    static std::vector<uint8_t> make_artwork(const std::string& artwork_id, int32_t size);

    // What the backend answers every request for artwork from here on, read as the device reads
    // it. Without it the answer is make_artwork()'s.
    void answer_artwork_with(int status, std::vector<uint8_t> body) {
        _artwork_answer = ArtworkAnswer{status, std::move(body)};
    }

    // From here on a request for artwork is answered with make_artwork()'s again.
    void answer_artwork_as_made() { _artwork_answer.reset(); }

    // From here on the backend does not answer a request for artwork, or answers it again.
    void set_artwork_reachable(bool reachable) { _artwork_reachable = reachable; }

    // From here on a request for artwork stays on its way until answer_artwork().
    void hold_artwork() { _artwork_held = true; }

    // Answers the request for artwork on its way, if there is one, whether or not a view still
    // waits for it, as the device does. The one the display sends next stays on its way.
    void answer_artwork();

    // Drops the answer to the request for artwork on its way, as HttpClient drops one the UI
    // task's queue has no room for. The display finds it at its loop's next pass, process(), or at
    // the next request for artwork.
    void drop_artwork() { _held_artwork.reset(); }

    // From here on a read of the card stays on its way until answer_card().
    void hold_card() { _card_held = true; }

    // Answers the read of the card on its way, if there is one, with what the card holds by now,
    // whether or not a view still waits for it. The one the display asks for next stays on its
    // way.
    void answer_card();

    // Drops the answer to the read of the card on its way, as the device drops one the UI task's
    // queue has no room for. The display finds it at its loop's next pass, process(), or at the
    // next request for artwork.
    void drop_card_read() { _held_read.reset(); }

    // The artwork the card was asked for, in order: what the cache had is not in it.
    const std::vector<ArtworkFetch>& get_card_reads() const { return _card_reads; }

    // How many answers to requests for artwork came with no view waiting for them any more, which
    // the device logs.
    size_t count_artwork_unwaited() const { return _artwork_unwaited; }

    // Switches the screen off, or on. The device fetches no artwork while it is dark.
    void set_screen_lit(bool lit) {
        _screen_lit = lit;
        _artwork.set_screen_lit(lit);
    }

    // How many reads and downloads of artwork have not started: waiting for the screen, or for the
    // one under way.
    size_t count_artwork_waiting() const { return _artwork.count_waiting(); }

    // The artwork fetched from the backend, in order: what the cache or the card had is not in it.
    const std::vector<ArtworkFetch>& get_artwork_fetches() const { return _artwork_fetches; }

    // Whether a view holds this image, which it does for as long as it draws it.
    bool is_artwork_shown(const std::string& artwork_id, int32_t size) const {
        return _artwork.get_cache().is_pinned(artwork_id, size);
    }

    // Touches the screen at a point, and lifts 100 ms later. The first touch is made as the scene
    // starts, and each one after it TAP_INTERVAL_MS after the one before.
    void tap(int32_t x, int32_t y) { _taps.push_back(Touch{{x, y}, {x, y}}); }

    // Touches the screen at `from`, moves to `to` and lifts there, as a tap does.
    void drag(lv_point_t from, lv_point_t to) { _taps.push_back(Touch{from, to}); }

    // Touches nothing for `ms`, rounded up to whole TAP_INTERVAL_MS: the next touch comes that
    // much later.
    void wait(uint32_t ms) { _taps.resize(_taps.size() + (ms + TAP_INTERVAL_MS - 1) / TAP_INTERVAL_MS); }

    // A scene's touches, in order; an empty one touches nothing.
    const std::vector<std::optional<Touch>>& get_taps() const { return _taps; }

private:
    explicit SimHost(std::unique_ptr<CardFolder> card_folder);

    // The loader's card: the store over the host's folder.
    bool is_on() const override;
    void read(ArtworkRead&& read) override;
    void erase(const std::string& artwork_id, int32_t size) override;

    void command_answered(uint32_t id, Command command, int status, const char* body);
    void send_volume_change();
    void volume_change_answered(uint32_t change, int status, const char* body);
    void send_group_change(const std::string& speaker_id, bool adds);
    void group_change_answered(const UnansweredGroupChange& change, int status, const char* body);
    void queue_answered(uint32_t request, int status, const char* body);
    void favourite_answered(const UnansweredFavouriteChange& change, int status, const char* body);
    void fetch_artwork(ArtworkDownload&& download);
    void answer_artwork(const ArtworkDownload& download);
    void answer_card(const ArtworkRead& read);
};
