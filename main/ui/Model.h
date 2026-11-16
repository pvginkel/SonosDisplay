#pragma once

// What the views draw: plain structs, filled by the host, and the rules by which a command
// changes what they show and a change of a group settles. One of the two doors between ui/ and
// the rest of the firmware; UiHost.h is the other.

#include <algorithm>
#include <cstdint>
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

    // How many commands, volume changes and group changes were not taken.
    uint32_t not_taken() const {
        return playback.commands_not_taken + volume.changes_not_taken + grouping.changes_not_taken;
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
