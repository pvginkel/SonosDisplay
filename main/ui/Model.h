#pragma once

// What the views draw: plain structs, filled by the host, and the rules by which a command or a
// press changes what they show, a change of a group or of the volume settles, a request to play,
// shuffle or add is watched to its end, and a list's pages are held; the library's items and the
// queue's entries; and can_draw(), which says what the fonts draw. One of the two doors between
// ui/ and the rest of the firmware; UiHost.h is the other.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

enum class StreamState {
    // Not connected yet since boot.
    Connecting,
    Connected,
    // Was connected, and lost it.
    Disconnected,
};

// How long a change sent to the backend waits for its answer, from when it is sent. Longer than a
// request on the path of presses takes, whose connection, send and answer HttpClient gives 5 s a
// wait. A change whose answer has not come by then is settled, and its answer changes nothing
// when it does come.
inline constexpr uint32_t ANSWER_TIME_LIMIT_MS = 30 * 1000;

// Whether every text font draws this Unicode code point. A text the backend sends is read without
// the characters they do not draw, which LVGL would draw as its placeholder box.
bool can_draw(uint32_t code_point);

// How the last request for a piece of the backend's state went.
enum class LoadState {
    // Asked for, and not answered yet.
    Loading,
    Loaded,
    // The backend could not be reached.
    Unreachable,
    // The backend answered an error, or something this display cannot read.
    Failed,
};

struct Speaker {
    // The speaker's id as the API gives it.
    std::string id;
    std::string name;

    // The id of every speaker of the group this one plays in, itself included.
    std::vector<std::string> member_ids;

    // Whether the speaker with this id plays in this one's group.
    bool plays_with(const std::string& speaker_id) const {
        return std::find(member_ids.begin(), member_ids.end(), speaker_id) != member_ids.end();
    }

    bool operator==(const Speaker&) const = default;
};

struct SpeakerList {
    LoadState state{LoadState::Loading};

    // What the backend said went wrong, when the state is Failed and it said so.
    std::string error;

    // Whether the backend has answered a list since the display started.
    bool answered{};

    // The last list the backend answered, which a later failure leaves in place.
    std::vector<Speaker> speakers;

    // The speaker with this id in that list, or nullptr when the list lacks it.
    const Speaker* find(const std::string& id) const {
        const auto found =
            std::find_if(speakers.begin(), speakers.end(), [&id](const Speaker& speaker) { return speaker.id == id; });

        return found == speakers.end() ? nullptr : &*found;
    }

    bool operator==(const SpeakerList&) const = default;
};

// A change of a group that this display asked for: a speaker other than the chosen one joins the
// chosen speaker's group, or leaves it.
struct GroupChange {
    // What tells this change from a later one of the same speaker.
    uint32_t id{};

    // The speaker that joins or leaves.
    std::string speaker_id;

    // The speaker that was the chosen one when the change was sent: the group is the one it
    // plays in.
    std::string group_of;

    // Whether the speaker joins the group. It leaves it otherwise.
    bool adds{};

    // LVGL's tick when the change was sent.
    uint32_t sent_at{};

    // LVGL's tick when the backend took the change; empty while the change is on its way.
    std::optional<uint32_t> taken_at;

    bool operator==(const GroupChange&) const = default;
};

// The changes of groups that are not settled, and the rules by which they settle. The backend's
// answer to a change says that the speaker took it, not that the group has changed: the speakers
// list shows the old groups until the speakers have reported the new ones. A change is pending
// from the press until then, and its speaker takes no second change for as long: the backend
// decides from the old groups whether a repeat is sent to the speaker. An answer to a change that
// is no longer pending changes nothing: the host reads one only while is_pending() says so.
struct Grouping {
    // How long a change the backend took waits for a list that shows it.
    static constexpr uint32_t TIME_LIMIT_MS = 15 * 1000;

    std::vector<GroupChange> pending;

    // How many changes were not taken: the backend answered an error, or did not answer. A view
    // says so once for each.
    uint32_t changes_not_taken{};

    // The change of this speaker that is pending, or nullptr.
    const GroupChange* find(const std::string& speaker_id) const {
        const auto found = std::find_if(pending.begin(), pending.end(), [&speaker_id](const GroupChange& change) {
            return change.speaker_id == speaker_id;
        });

        return found == pending.end() ? nullptr : &*found;
    }

    // Whether a change of this speaker can be sent.
    bool allows(const std::string& speaker_id) const { return !find(speaker_id); }

    // Whether the change with this id is pending.
    bool is_pending(uint32_t id) const {
        return std::any_of(pending.begin(), pending.end(), [id](const GroupChange& change) { return change.id == id; });
    }

    // A change is on its way to the backend from `now`, LVGL's tick: `speaker_id` joins the group
    // of `group_of`, the chosen speaker, or leaves it. Returns the change's id, which its answer is
    // told by.
    uint32_t sent(const std::string& speaker_id, const std::string& group_of, bool adds, uint32_t now) {
        const auto id = ++_last_id;
        pending.push_back(
            GroupChange{.id = id, .speaker_id = speaker_id, .group_of = group_of, .adds = adds, .sent_at = now});

        return id;
    }

    // The backend took the change. The time limit runs from here, `now` being LVGL's tick. A
    // change a list has settled already stays settled.
    void taken(uint32_t id, uint32_t now) {
        for (auto& change : pending) {
            if (change.id == id) {
                change.taken_at = now;
            }
        }
    }

    // The backend answered the change with an error, or did not answer. The host fetches the
    // speakers again, which shows the groups as they are.
    void not_taken(uint32_t id) {
        std::erase_if(pending, [id](const GroupChange& change) { return change.id == id; });
        changes_not_taken++;
    }

    // The backend answered a list. A change is settled once the list shows its speaker in or out
    // of the group as asked; a list from before the speakers reported leaves it pending, and so
    // does one that lacks the speaker whose group it is.
    void listed(const SpeakerList& list) {
        std::erase_if(pending, [&list](const GroupChange& change) {
            const auto group = list.find(change.group_of);

            return group && group->plays_with(change.speaker_id) == change.adds;
        });
    }

    // Settles the changes the backend took TIME_LIMIT_MS or longer before `now`, LVGL's tick, and
    // those still on their way ANSWER_TIME_LIMIT_MS or longer after they were sent, and returns
    // whether there was one. The host then fetches the speakers again.
    bool timed_out(uint32_t now) {
        const auto settled = std::erase_if(pending, [now](const GroupChange& change) {
            return change.taken_at ? now - *change.taken_at >= TIME_LIMIT_MS
                                   : now - change.sent_at >= ANSWER_TIME_LIMIT_MS;
        });

        return settled > 0;
    }

private:
    uint32_t _last_id{};
};

// What the group of the chosen speaker is doing, as the API names it.
enum class PlayState {
    Playing,
    Paused,
    Stopped,
    // About to play, and waiting for the music.
    Buffering,
};

// A transport command, each an endpoint of the API.
enum class Command {
    Play,
    Pause,
    Next,
    Previous,
};

struct Track {
    std::string title;

    // For a radio stream: the station's name.
    std::string artist;

    // Empty where the source gives none, as a radio stream does.
    std::string album;

    // The id of the track's picture, as the API gives it. Tracks with the same picture share it.
    // Empty when there is none to show, as for a radio stream.
    std::string artwork_id;

    bool operator==(const Track&) const = default;
};

// What the chosen speaker plays, and what can be done with it.
struct Playback {
    LoadState state{LoadState::Loading};

    // What the backend said went wrong, when the state is Failed and it said so.
    std::string error;

    // The rest is the last playback the backend answered, which a later failure leaves in place,
    // with the play or pause the display has sent since.

    PlayState play_state{PlayState::Stopped};

    // The commands the speaker allows now. A control is live only while its command is.
    bool can_play{};
    bool can_pause{};
    bool can_next{};
    bool can_previous{};

    // The track that is loaded, if one is.
    std::optional<Track> track;

    // The command last sent, until the backend has answered a command.
    std::optional<Command> sending;

    // How many commands were not taken: the backend answered an error, or did not answer. A view
    // says so once for each.
    uint32_t commands_not_taken{};

    bool is_playing() const { return play_state == PlayState::Playing || play_state == PlayState::Buffering; }

    bool allows(Command command) const {
        switch (command) {
            case Command::Play:
                return can_play;
            case Command::Pause:
                return can_pause;
            case Command::Next:
                return can_next;
            case Command::Previous:
                return can_previous;
        }

        return false;
    }

    // A command is on its way to the backend. The answer to it carries no state and the speaker
    // reports the change later, so a play or a pause shows from here on.
    void command_sent(Command command) {
        sending = command;
        show(command);
    }

    // The backend took the command. A playback answered while the command was on its way is from
    // before it, so the play or pause shows again.
    void command_taken(Command command) {
        sending.reset();
        show(command);
    }

    // The backend answered the command with an error, or did not answer. The host fetches the
    // playback again, which puts right what command_sent() showed.
    void command_not_taken() {
        sending.reset();
        commands_not_taken++;
    }

    // Another speaker is chosen: nothing is known of what it plays. The count goes on, so a view
    // does not take the change for a command that was not taken.
    void speaker_changed() {
        const auto not_taken = commands_not_taken;

        *this = {};
        commands_not_taken = not_taken;
    }

private:
    void show(Command command) {
        if (command == Command::Play) {
            play_state = PlayState::Playing;
        } else if (command == Command::Pause) {
            play_state = PlayState::Paused;
        }
    }
};

// A change of the volume on its way to the backend.
struct VolumeChange {
    // What tells this change's answer from another change's.
    uint32_t id{};

    // What the level changes by.
    int step{};

    // Whether the change unmutes the group too.
    bool unmutes{};

    // LVGL's tick when the change was sent.
    uint32_t sent_at{};
};

// The volume of the chosen speaker's group, and the rules by which presses change it. An answer to
// a change that is not the one on its way changes nothing: the host reads one only while
// is_sending() says so.
struct Volume {
    // What one press changes the level by.
    static constexpr int STEP = 2;

    static constexpr int MAX_LEVEL = 100;

    // The largest step the API takes in one change.
    static constexpr int MAX_CHANGE = 100;

    LoadState state{LoadState::Loading};

    // The level, 0 to MAX_LEVEL, and the mute the backend last answered, which a later failure
    // leaves in place. The level is known while the state is Loaded.
    int level{};
    bool muted{};

    // The change on its way to the backend, if one is.
    std::optional<VolumeChange> sending;

    // The steps pressed since that one was sent. They go as one change once it is answered, so
    // at most one change is on its way however many presses there were.
    int waiting{};

    // Whether a press on up since that one was sent unmutes the group. The unmute goes with the
    // steps that wait, also where they come to none.
    bool unmuting{};

    // How many changes were not taken: the backend answered an error, or did not answer. A view
    // says so once for each.
    uint32_t changes_not_taken{};

    bool is_known() const { return state == LoadState::Loaded; }

    bool is_changing() const { return sending || waiting || unmuting; }

    // Whether the change with this id is the one on its way.
    bool is_sending(uint32_t id) const { return sending && sending->id == id; }

    // The level the presses lead to, which a view shows while the level is known.
    int shown() const { return std::clamp(level + (sending ? sending->step : 0) + waiting, 0, MAX_LEVEL); }

    // Whether the group shows as muted: the mute the backend last answered, until a press on up
    // unmutes it. A view shows it while the level is known.
    bool shown_muted() const { return muted && !unmuting && !(sending && sending->unmutes); }

    // Whether a press on down, a negative step, or on up does anything. While the level is on its
    // way nothing does. While the group shows muted up does, also at the top of the range: it
    // unmutes. While the level could not be fetched one step goes without it: the answer to a
    // change carries the level.
    bool allows(int step) const {
        switch (state) {
            case LoadState::Loading:
                return false;
            case LoadState::Loaded:
                return step > 0 ? shown() < MAX_LEVEL || shown_muted() : shown() > 0;
            case LoadState::Unreachable:
            case LoadState::Failed:
                return !sending;
        }

        return false;
    }

    // A press on down or up, which the level shows from here on. A press on up while the group
    // shows muted unmutes it too; down leaves the mute. next_change() says what to send for it.
    void pressed(int step) {
        if (is_known()) {
            if (step > 0 && shown_muted()) {
                unmuting = true;
            }

            const auto from = shown();
            waiting += std::clamp(from + step, 0, MAX_LEVEL) - from;
        } else {
            waiting = step;
        }
    }

    // The change to send now, at `now`, LVGL's tick: the steps that wait and the unmute, once none
    // is on its way. It is on its way from here on.
    std::optional<VolumeChange> next_change(uint32_t now) {
        if (sending || (!waiting && !unmuting)) {
            return std::nullopt;
        }

        sending = VolumeChange{.id = ++_last_id,
                               .step = std::clamp(waiting, -MAX_CHANGE, MAX_CHANGE),
                               .unmutes = unmuting,
                               .sent_at = now};
        waiting = 0;
        unmuting = false;

        return sending;
    }

    // The backend took the change on its way, and its answer is the level.
    void change_taken() { sending.reset(); }

    // The backend answered the change on its way with an error, or did not answer. The steps and
    // the unmute that wait are dropped with it, and the host fetches the volume again, which puts
    // the level right.
    void change_not_taken() {
        sending.reset();
        waiting = 0;
        unmuting = false;
        changes_not_taken++;
    }

    // Settles the change on its way when it was sent ANSWER_TIME_LIMIT_MS or longer before `now`,
    // LVGL's tick, and returns whether there was one. The steps and the unmute that wait are
    // dropped with it, and the host fetches the volume again, which puts the level right.
    bool timed_out(uint32_t now) {
        if (!sending || now - sending->sent_at < ANSWER_TIME_LIMIT_MS) {
            return false;
        }

        sending.reset();
        waiting = 0;
        unmuting = false;

        return true;
    }

    // Another speaker is chosen: nothing is known of its volume. The count goes on, so a view
    // does not take the change for one that was not taken, and so do the ids, so the answer to a
    // change for the speaker before is not taken for a later change's.
    void speaker_changed() {
        const auto not_taken = changes_not_taken;
        const auto last_id = _last_id;

        *this = {};
        changes_not_taken = not_taken;
        _last_id = last_id;
    }

private:
    uint32_t _last_id{};
};

// How many items a list asks the backend for at a time: the most a page of the API holds.
inline constexpr uint32_t PAGE_SIZE = 100;

// A page of a list the backend gives in pages, as the host read the answer.
template <typename Item>
struct Page {
    // Loaded, or why the page did not come: Unreachable or Failed.
    LoadState state{LoadState::Loading};

    // What the backend said went wrong, when the state is Failed and it said so.
    std::string error;

    // Which page it is, from 1, and how many items the whole list has, as the answer says.
    uint32_t number{};
    uint32_t total{};

    std::vector<Item> items;
};

// The pages of a list that a view holds, apart from their items, which PagedList holds: which
// pages they are, and how the last request for one went. Each is the one after the one before. A
// page that comes beside them makes one past MAX_PAGES until the view drops one at an end as it
// builds the list, so it builds at most MAX_PAGES × PAGE_SIZE rows.
struct PageWindow {
    static constexpr size_t MAX_PAGES = 2;

    // How the last request for a page went: Loading while it is on its way, or before the first.
    LoadState state{LoadState::Loading};

    // What the backend said went wrong, when the state is Failed and it said so.
    std::string error;

    // The page last asked for, from 1; 0 before the first.
    uint32_t asked{};

    // How many items the whole list has, by the last page answered.
    uint32_t total{};

    // The page of the first item held, from 1; 0 while none is held.
    uint32_t first_page{};

    // How many items each page held has, in order.
    std::vector<size_t> page_sizes;

    // Whether a page has come, and its items are held.
    bool is_held() const { return !page_sizes.empty(); }

    // How many items are held.
    size_t count() const {
        size_t count = 0;
        for (const auto size : page_sizes) {
            count += size;
        }

        return count;
    }

    // The place in the whole list of the first item held, from 0.
    uint32_t first_index() const { return (first_page - 1) * PAGE_SIZE; }

    // The page before the first one held, or 0 when that is the list's first.
    uint32_t page_before() const { return is_held() && first_page > 1 ? first_page - 1 : 0; }

    // The page after the last one held, or 0 when the list ends with it.
    uint32_t page_after() const {
        return is_held() && first_index() + count() < total ? first_page + uint32_t(page_sizes.size()) : 0;
    }

    // Whether this page was asked for and did not come.
    bool has_failed(uint32_t page) const {
        return page == asked && (state == LoadState::Unreachable || state == LoadState::Failed);
    }

    // A page is on its way from here on. The pages held stay until it comes.
    void ask(uint32_t page) {
        asked = page;
        state = LoadState::Loading;
        error.clear();
    }

    // Drops the first page held, or the last, with its items.
    virtual void drop(bool first) = 0;
};

// The pages of a list that a view holds, with their items, and the rules by which they come: the
// page next to those held is added at their end.
template <typename Item>
struct PagedList : PageWindow {
    // The items of the pages held, in order.
    std::vector<Item> items;

    // The page asked for came, or did not. A page next to those held is added beside them; another
    // takes the place of every page held: the first page, or one asked for again. One that did not
    // come leaves the pages held as they are.
    void answered(Page<Item>&& page) {
        state = page.state;
        error = std::move(page.error);
        if (state != LoadState::Loaded) {
            return;
        }

        total = page.total;
        const auto size = page.items.size();

        if (is_held() && page.number == first_page + page_sizes.size()) {
            items.insert(items.end(), std::make_move_iterator(page.items.begin()),
                         std::make_move_iterator(page.items.end()));
            page_sizes.push_back(size);
        } else if (is_held() && page.number + 1 == first_page) {
            items.insert(items.begin(), std::make_move_iterator(page.items.begin()),
                         std::make_move_iterator(page.items.end()));
            page_sizes.insert(page_sizes.begin(), size);
            first_page--;
        } else {
            items = std::move(page.items);
            page_sizes = {size};
            first_page = page.number;
        }
    }

    // Drops every page held, with its items.
    void clear() {
        items.clear();
        page_sizes.clear();
        first_page = 0;
    }

    void drop(bool first) override {
        if (first) {
            items.erase(items.begin(), items.begin() + ptrdiff_t(page_sizes.front()));
            page_sizes.erase(page_sizes.begin());
            first_page++;
        } else {
            items.erase(items.end() - ptrdiff_t(page_sizes.back()), items.end());
            page_sizes.pop_back();
        }
    }
};

// An artist of the library.
struct Artist {
    // The artist's id as the API gives it.
    std::string id;
    std::string title;

    // The letter the artist files under: 'A' to 'Z', or '#' for an artist that files under none.
    // The backend lists a letter's artists together, those under '#' first.
    char index_letter{};

    // The id of the favourite that marks the artist; none while no favourite does.
    std::optional<int64_t> favourite_id;

    bool operator==(const Artist&) const = default;
};

// An album of the library.
struct Album {
    // The album's id as the API gives it.
    std::string id;
    std::string title;

    // The year the album was released; none for an album without a date.
    std::optional<int32_t> year;

    // The id of the favourite that marks the album; none while no favourite does. The two copies
    // of a release the library holds twice carry the one favourite.
    std::optional<int64_t> favourite_id;

    bool operator==(const Album&) const = default;
};

// A track of an album, as the library lists it.
struct AlbumTrack {
    // The track's id as the API gives it.
    std::string id;
    std::string title;

    bool operator==(const AlbumTrack&) const = default;
};

// A playlist of the library.
struct Playlist {
    // The playlist's id as the API gives it.
    std::string id;
    std::string title;

    // The id of the favourite that marks the playlist; none while no favourite does.
    std::optional<int64_t> favourite_id;

    bool operator==(const Playlist&) const = default;
};

// An entry of the queue of the chosen speaker's group.
struct QueueEntry {
    std::string title;
    std::string artist;

    bool operator==(const QueueEntry&) const = default;
};

// A page of the queue of the chosen speaker's group, in the order the speaker plays the queue, as
// the host read the answer, and where in the queue the group is.
struct QueuePage : Page<QueueEntry> {
    // The place in the queue, from 1, of the track that is loaded: none while the group plays from
    // elsewhere than its queue, and for an empty queue.
    std::optional<uint32_t> current_position;
};

// What an item of the library is, as the API names its kind.
enum class ItemKind {
    Artist,
    Album,
    Track,
    Playlist,
};

// What a press on an item of the library does with the queue of the chosen speaker's group, as
// the API names it.
enum class QueueAction {
    // Replaces the queue with the item, and plays it in order: an artist's albums newest first.
    Play,
    // Replaces the queue with the item, and plays it shuffled. A track is never shuffled.
    Shuffle,
    // Appends the item to the queue, and changes nothing else.
    Add,
};

// A press on an item of the library: which item, and what is done with it.
struct QueueRequest {
    ItemKind kind{};

    // The item's id as the API gives it.
    std::string item_id;

    QueueAction action{};

    bool operator==(const QueueRequest&) const = default;
};

// The end of a task of the backend's, as the event stream reports it.
struct TaskEnd {
    std::string task_id;

    // Why the task failed, in the backend's words; none for a task that completed.
    std::optional<std::string> error;
};

// The items the display asked the backend to play, shuffle or add, and how that went. The backend
// answers a request with the id of the task that does it, and reports the task's end on the event
// stream, which is not the path the answer takes: the end may come first. A request is on its way
// until its answer, or until ANSWER_TIME_LIMIT_MS after it was sent; an answer to a request that is
// no longer on its way changes nothing: the host reads one only while is_sending() says so.
struct Queueing {
    // The most tasks watched for their end, and the most ends held for an answer to come.
    static constexpr size_t MAX_TASKS = 8;

    // A request on its way to the backend.
    struct Sent {
        // What tells this request's answer from another's.
        uint32_t id{};

        QueueRequest request;

        // LVGL's tick when the request was sent.
        uint32_t sent_at{};
    };

    std::vector<Sent> sending;

    // The tasks the backend took a request with, until their end comes, the newest last. The
    // stream drops what is sent while it is away, so the end of one may never come: the oldest
    // gives way past MAX_TASKS.
    std::vector<std::string> running;

    // The ends of tasks no answer has named yet, the newest last. They are held while a request is
    // on its way, whose answer may name one of them.
    std::vector<TaskEnd> ended;

    // How many requests were not taken: the backend answered an error, or did not answer. A view
    // says so once for each.
    uint32_t requests_not_taken{};

    // How many adds the backend took. A view says so once for each: nothing else on screen shows
    // that an add went.
    uint32_t adds_taken{};

    // How many tasks failed, and the last one's failure in the backend's words. A view says it
    // once for each.
    uint32_t tasks_failed{};
    std::string failure;

    // Whether the request with this id is on its way.
    bool is_sending(uint32_t id) const {
        return std::any_of(sending.begin(), sending.end(), [id](const Sent& sent) { return sent.id == id; });
    }

    // Whether a request for this item and action is on its way. A view shows the button that sent
    // it so, and a press on that button sends no second one.
    bool is_on_its_way(const QueueRequest& request) const {
        return std::any_of(sending.begin(), sending.end(),
                           [&request](const Sent& sent) { return sent.request == request; });
    }

    // A request is on its way from `now`, LVGL's tick. Returns its id, which its answer is told
    // by.
    uint32_t sent(const QueueRequest& request, uint32_t now) {
        const auto id = ++_last_id;
        sending.push_back(Sent{.id = id, .request = request, .sent_at = now});

        return id;
    }

    // The backend took the request, and the task with this id does it. A failure of the task that
    // came before the answer is said now.
    void taken(uint32_t id, const std::string& task_id) {
        const auto found =
            std::find_if(sending.begin(), sending.end(), [id](const Sent& sent) { return sent.id == id; });
        if (found->request.action == QueueAction::Add) {
            adds_taken++;
        }
        sending.erase(found);

        const auto end =
            std::find_if(ended.begin(), ended.end(), [&task_id](const TaskEnd& end) { return end.task_id == task_id; });
        if (end != ended.end()) {
            if (end->error) {
                failed(std::move(*end->error));
            }
            ended.erase(end);
        } else {
            running.push_back(task_id);
            if (running.size() > MAX_TASKS) {
                running.erase(running.begin());
            }
        }

        if (sending.empty()) {
            ended.clear();
        }
    }

    // The backend answered the request with an error, or did not answer.
    void not_taken(uint32_t id) {
        std::erase_if(sending, [id](const Sent& sent) { return sent.id == id; });
        requests_not_taken++;

        if (sending.empty()) {
            ended.clear();
        }
    }

    // A task ended. Returns whether a view has something to say for it: it is a task of the
    // display's, and failed. The end of a task no answer has named yet is held while a request is
    // on its way; the events of other tasks are ignored.
    bool task_ended(TaskEnd&& end) {
        const auto found = std::find(running.begin(), running.end(), end.task_id);
        if (found != running.end()) {
            running.erase(found);
            if (end.error) {
                failed(std::move(*end.error));
                return true;
            }
        } else if (!sending.empty()) {
            ended.push_back(std::move(end));
            if (ended.size() > MAX_TASKS) {
                ended.erase(ended.begin());
            }
        }

        return false;
    }

    // Settles the requests sent ANSWER_TIME_LIMIT_MS or longer before `now`, LVGL's tick, as not
    // taken, and returns whether there was one.
    bool timed_out(uint32_t now) {
        const auto settled =
            std::erase_if(sending, [now](const Sent& sent) { return now - sent.sent_at >= ANSWER_TIME_LIMIT_MS; });
        if (!settled) {
            return false;
        }

        requests_not_taken += uint32_t(settled);
        if (sending.empty()) {
            ended.clear();
        }

        return true;
    }

private:
    uint32_t _last_id{};

    void failed(std::string error) {
        tasks_failed++;
        failure = std::move(error);
    }
};

// A press on the favourite toggle of an artist, an album or a playlist of the library: marks the
// item, or unmarks the favourite that marks it.
struct FavouriteRequest {
    ItemKind kind{};

    // The item's id as the API gives it.
    std::string item_id;

    // The favourite that marks the item, which the press unmarks; none for a press that marks it.
    std::optional<int64_t> favourite_id;

    bool operator==(const FavouriteRequest&) const = default;
};

// A change of a favourite the backend took: it marked an item with the favourite with this id, or
// it holds no favourite with this id any longer.
struct FavouriteChange {
    ItemKind kind{};

    // The item the press was on.
    std::string item_id;

    int64_t favourite_id{};
    bool marks{};

    // The favourite that marks the item of this kind and id after the change, which the favourite
    // `favourite_id_before` marked before it, or none. A mark marks the item pressed; an unmark
    // unmarks every item the favourite marked, as the two copies of a release share one.
    std::optional<int64_t> apply(ItemKind item_kind, const std::string& id,
                                 std::optional<int64_t> favourite_id_before) const {
        if (item_kind != kind) {
            return favourite_id_before;
        }
        if (marks) {
            return id == item_id ? std::optional<int64_t>(favourite_id) : favourite_id_before;
        }

        return favourite_id_before == favourite_id ? std::nullopt : favourite_id_before;
    }

    bool operator==(const FavouriteChange&) const = default;
};

// The items the display asked the backend to mark or unmark as a favourite, and how that went. A
// change is on its way until its answer, or until ANSWER_TIME_LIMIT_MS after it was sent; an
// answer to a change that is no longer on its way changes nothing: the host reads one only while
// is_sending() says so.
struct Favouring {
    // The most changes taken that are held for a view to apply.
    static constexpr size_t MAX_CHANGES = 8;

    // A change on its way to the backend.
    struct Sent {
        // What tells this change's answer from another's.
        uint32_t id{};

        FavouriteRequest request;

        // LVGL's tick when the change was sent.
        uint32_t sent_at{};
    };

    std::vector<Sent> sending;

    // The changes the backend took, the newest last, the oldest giving way past MAX_CHANGES, and
    // how many it took in all. A view applies those taken since it last looked to the items it
    // holds; one that missed more than are held fetches its items again.
    std::vector<FavouriteChange> changes;
    uint32_t changes_taken{};

    // How many changes were not taken: the backend answered an error, or did not answer. A view
    // says so once for each.
    uint32_t changes_not_taken{};

    // Whether the change with this id is on its way.
    bool is_sending(uint32_t id) const {
        return std::any_of(sending.begin(), sending.end(), [id](const Sent& sent) { return sent.id == id; });
    }

    // Whether a change of the item of this kind and id is on its way. A view shows its toggle so,
    // and a press on that toggle sends no second one.
    bool is_on_its_way(ItemKind kind, const std::string& item_id) const {
        return std::any_of(sending.begin(), sending.end(), [kind, &item_id](const Sent& sent) {
            return sent.request.kind == kind && sent.request.item_id == item_id;
        });
    }

    // A change is on its way from `now`, LVGL's tick. Returns its id, which its answer is told by.
    uint32_t sent(const FavouriteRequest& request, uint32_t now) {
        const auto id = ++_last_id;
        sending.push_back(Sent{.id = id, .request = request, .sent_at = now});

        return id;
    }

    // The backend took the mark with this id: the favourite with `favourite_id` marks the item.
    void marked(uint32_t id, int64_t favourite_id) { taken(id, favourite_id); }

    // The backend took the unmark with this id, or holds no favourite with its id: the item is
    // unmarked either way.
    void unmarked(uint32_t id) { taken(id, 0); }

    // The backend answered the change with an error, or did not answer.
    void not_taken(uint32_t id) {
        std::erase_if(sending, [id](const Sent& sent) { return sent.id == id; });
        changes_not_taken++;
    }

    // Settles the changes sent ANSWER_TIME_LIMIT_MS or longer before `now`, LVGL's tick, as not
    // taken, and returns whether there was one.
    bool timed_out(uint32_t now) {
        const auto settled =
            std::erase_if(sending, [now](const Sent& sent) { return now - sent.sent_at >= ANSWER_TIME_LIMIT_MS; });
        changes_not_taken += uint32_t(settled);

        return settled > 0;
    }

private:
    uint32_t _last_id{};

    // An unmark's change names the favourite it unmarked; `favourite_id` is a mark's.
    void taken(uint32_t id, int64_t favourite_id) {
        const auto found =
            std::find_if(sending.begin(), sending.end(), [id](const Sent& sent) { return sent.id == id; });
        const auto& request = found->request;

        changes.push_back(FavouriteChange{.kind = request.kind,
                                          .item_id = request.item_id,
                                          .favourite_id = request.favourite_id.value_or(favourite_id),
                                          .marks = !request.favourite_id});
        if (changes.size() > MAX_CHANGES) {
            changes.erase(changes.begin());
        }
        changes_taken++;

        sending.erase(found);
    }
};

struct Model {
    // Bumped by the host on every change, so a view redraws only when something changed.
    uint32_t revision{};

    // The backend's base URL from IoT Support's device configuration; empty when the
    // configuration has none, or none that is valid.
    std::string backend_url;

    StreamState stream_state{StreamState::Connecting};

    // LVGL's tick when the event stream was last lost, or when the app started connecting.
    uint32_t stream_down_since{};

    // The household's speakers.
    SpeakerList speakers;

    // The id of the speaker this display controls, as stored on the display; empty while none is
    // chosen. Only a pick changes it: the list may lack it.
    std::string speaker_id;

    // What the chosen speaker plays.
    Playback playback;

    // The volume of the chosen speaker's group.
    Volume volume;

    // The changes of groups that are pending. A pick of another speaker leaves them as they are:
    // each is settled by the group it was sent for.
    Grouping grouping;

    // The items asked to be played, shuffled or added.
    Queueing queueing;

    // The items asked to be marked or unmarked as a favourite.
    Favouring favouring;

    // How many times the event stream said that the queue of the chosen speaker's group changed, or
    // the track it has loaded. A view that shows the queue fetches it again when this moves.
    uint32_t queue_changes{};

    // How many times the favourites may have changed unseen: the event stream said that a favourite
    // was marked or unmarked, on any display, this one's own marks among them, or it is back after
    // it was away, and the events sent meanwhile are gone. A view that shows which items a favourite
    // marks fetches them again when this moves.
    uint32_t favourites_changes{};

    // How many requests to clear the queue were not taken: the backend answered an error, or could
    // not be reached. A view says so once for each.
    uint32_t clears_not_taken{};

    // How many commands, volume changes, group changes, requests to queue an item, requests to
    // clear the queue and changes of a favourite were not taken.
    uint32_t not_taken() const {
        return playback.commands_not_taken + volume.changes_not_taken + grouping.changes_not_taken +
               queueing.requests_not_taken + clears_not_taken + favouring.changes_not_taken;
    }

    // Whether the display is held on the speaker picker: no speaker is chosen, or the last list
    // the backend answered lacks the chosen one. A list that was never answered holds nothing.
    bool needs_speaker() const {
        if (speaker_id.empty()) {
            return true;
        }

        return speakers.answered && !speakers.find(speaker_id);
    }

    // Whether the speaker with this id plays in the chosen speaker's group, by the last list the
    // backend answered. No speaker does while none is chosen or that list lacks the chosen one.
    bool in_group(const std::string& id) const {
        const auto chosen = speakers.find(speaker_id);

        return chosen && chosen->plays_with(id);
    }
};
