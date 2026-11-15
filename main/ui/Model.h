#pragma once

// What the views draw: plain structs, filled by the host, and the rules by which a command
// changes what they show. One of the two doors between ui/ and the rest of the firmware; UiHost.h
// is the other.

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

    bool operator==(const SpeakerList&) const = default;
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

    // Whether the display is held on the speaker picker: no speaker is chosen, or the last list
    // the backend answered lacks the chosen one. A list that was never answered holds nothing.
    bool needs_speaker() const {
        if (speaker_id.empty()) {
            return true;
        }

        const auto listed = std::any_of(speakers.speakers.begin(), speakers.speakers.end(),
                                        [this](const Speaker& speaker) { return speaker.id == speaker_id; });

        return speakers.answered && !listed;
    }
};
