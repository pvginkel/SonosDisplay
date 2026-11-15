#include "scenes.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "LoadingView.h"
#include "Messages.h"
#include "NowPlayingView.h"
#include "SpeakerPickerView.h"
#include "api/Answers.h"
#include "cJSON.h"

// The backend's answers, in the shape the deployed backend gives them. The ids are made up: a
// real one names its speaker's hardware address.

static const char SPEAKERS[] = R"({"data":[
{"group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage"],"muted":false,"volume":24},
 "id":"speaker-garage","model":"Play:3","muted":false,"name":"Garage","volume":24},
{"group":{"coordinator_id":"speaker-library","member_ids":["speaker-library","speaker-bathroom"],"muted":false,
 "volume":31},"id":"speaker-library","model":"Playbase","muted":false,"name":"Bibliothèque","volume":35},
{"group":{"coordinator_id":"speaker-library","member_ids":["speaker-library","speaker-bathroom"],"muted":false,
 "volume":31},"id":"speaker-bathroom","model":"Play:1","muted":true,"name":"Żółta łazienka","volume":27}
]})";

static const char SPEAKERS_MANY[] = R"({"data":[
{"id":"speaker-attic","name":"Attic","model":"One","volume":10,"muted":false,
 "group":{"coordinator_id":"speaker-attic","member_ids":["speaker-attic"],"volume":10,"muted":false}},
{"id":"speaker-garage","name":"Garage","model":"Play:3","volume":24,"muted":false,
 "group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage"],"volume":24,"muted":false}},
{"id":"speaker-guests","name":"Gästezimmer im Erdgeschoß, neben der Küche und dem Wintergarten","model":"Five",
 "volume":18,"muted":false,
 "group":{"coordinator_id":"speaker-guests","member_ids":["speaker-guests"],"volume":18,"muted":false}},
{"id":"speaker-kitchen","name":"Kitchen","model":"Era 100","volume":40,"muted":false,
 "group":{"coordinator_id":"speaker-kitchen","member_ids":["speaker-kitchen"],"volume":40,"muted":false}},
{"id":"speaker-office","name":"Office","model":"Play:1","volume":12,"muted":false,
 "group":{"coordinator_id":"speaker-office","member_ids":["speaker-office"],"volume":12,"muted":false}}
]})";

static const char SPEAKERS_EMPTY[] = R"({"data":[]})";

// The error envelope, with the two fields the API's document allows as null.
static const char ERROR_ENVELOPE[] =
    R"({"code":null,"correlationId":null,"details":{"message":"The speakers could not be listed"},)"
    R"("error":"The Sonos household did not answer: no speaker was found on the network within the time a search )"
    R"(is given, and the last one that answered, Bibliothèque, went away while it was asked for its group"})";

// What a request that fails validation is answered with: no envelope.
static const char ERROR_VALIDATION[] =
    R"([{"ctx":{"le":480},"loc":["query","size"],"msg":"Input should be less than or equal to 480",)"
    R"("type":"less_than_equal"}])";

// Answers the API's document does not allow.
static const char* const UNREADABLE[] = {
    "",
    "<html><body>502 Bad Gateway</body></html>",
    R"({"data":[)",
    "null",
    R"([{"id":"speaker-garage","name":"Garage"}])",
    R"({"data":null})",
    R"({"data":{"id":"speaker-garage","name":"Garage"}})",
    R"({"data":["speaker-garage"]})",
    R"({"data":[{"name":"Garage"}]})",
    R"({"data":[{"id":"","name":"Garage"}]})",
    R"({"data":[{"id":7,"name":"Garage"}]})",
    R"({"data":[{"id":"speaker-garage","name":null}]})",
    R"({"data":[{"id":"speaker-garage","name":"Garage"},{"id":"speaker-library"}]})",
};

// A queue from the Plex library, playing.
static const char PLAYING[] =
    R"({"actions":["play","pause","next","previous"],"queue_length":11,"queue_position":3,"shuffle":false,)"
    R"("source":"queue","state":"playing","track":{"album":"Ágætis byrjun","artist":"Sigur Rós",)"
    R"("artwork_id":"YXJ0d29yay9zdmVmbg","duration_ms":604000,"title":"Svefn-g-englar","track_id":"1042"}})";

// The queue's last track: the speaker allows no next.
static const char PLAYING_LAST[] =
    R"({"actions":["play","pause","previous"],"queue_length":11,"queue_position":11,"shuffle":false,)"
    R"("source":"queue","state":"playing","track":{"album":"Ágætis byrjun","artist":"Sigur Rós",)"
    R"("artwork_id":"YXJ0d29yay9zdmVmbg","duration_ms":604000,"title":"Svefn-g-englar","track_id":"1042"}})";

// Paused, with texts longer than their places.
static const char PAUSED[] =
    R"({"actions":["play","pause","next","previous"],"queue_length":24,"queue_position":7,"shuffle":true,)"
    R"("source":"queue","state":"paused","track":{"album":"Rameau: Une symphonie imaginaire – Les Musiciens du )"
    R"(Louvre","artist":"Jean-Philippe Rameau · Marc Minkowski","artwork_id":"YXJ0d29yay9yYW1lYXU",)"
    R"("duration_ms":283000,"title":"Les Boréades, tragédie lyrique en cinq actes: Entrée de Polymnie",)"
    R"("track_id":"2217"}})";

// A radio stream started from the Sonos app, with every null the API's document gives it.
static const char RADIO[] =
    R"({"actions":["play","pause"],"queue_length":null,"queue_position":null,"shuffle":false,"source":"radio",)"
    R"("state":"playing","track":{"album":null,"artist":"Český rozhlas Vltava","artwork_id":null,)"
    R"("duration_ms":null,"title":"Dvořák: Slovanské tance, č. 8 – Česká filharmonie","track_id":null}})";

static const char NOTHING_LOADED[] =
    R"({"actions":[],"queue_length":0,"queue_position":null,"shuffle":false,"source":"queue","state":"stopped",)"
    R"("track":null})";

// A command the playback does not list is answered 409.
static const char ERROR_NOT_AVAILABLE[] =
    R"({"code":"ACTION_NOT_AVAILABLE","correlationId":"3f0c9a52-7d1e-4b8a-9c60-2e5d4f7a1b93",)"
    R"("details":{"message":"The request conflicts with the current state"},)"
    R"("error":"The speaker does not allow next now"})";

static const char ERROR_SPEAKER[] =
    R"({"code":null,"correlationId":null,"details":{"message":"The speaker failed or did not answer"},)"
    R"("error":"The speaker Garage did not answer within the time it is given, and what it plays is not known"})";

// Playbacks the API's document does not allow.
static const char* const UNREADABLE_PLAYBACKS[] = {
    "",
    "<html><body>502 Bad Gateway</body></html>",
    R"({"state":"playing")",
    "null",
    "[]",
    R"({"actions":["play"],"track":null})",
    R"({"state":"dancing","actions":["play"],"track":null})",
    R"({"state":null,"actions":["play"],"track":null})",
    R"({"state":"playing","track":null})",
    R"({"state":"playing","actions":null,"track":null})",
    R"({"state":"playing","actions":"pause","track":null})",
    R"({"state":"playing","actions":["pause",1],"track":null})",
    R"({"state":"playing","actions":["pause"]})",
    R"({"state":"playing","actions":["pause"],"track":"Svefn-g-englar"})",
    R"({"state":"playing","actions":["pause"],"track":{"artist":"Sigur Rós","album":null}})",
    R"({"state":"playing","actions":["pause"],"track":{"title":7,"artist":"Sigur Rós","album":null}})",
    R"({"state":"playing","actions":["pause"],"track":{"title":"Svefn-g-englar","artist":null,"album":null}})",
    R"({"state":"playing","actions":["pause"],"track":{"title":"Svefn-g-englar","artist":"Sigur Rós"}})",
    R"({"state":"playing","actions":["pause"],"track":{"title":"Svefn-g-englar","artist":"Sigur Rós","album":7}})",
};

// Where the now-playing screen's controls are.
static constexpr lv_point_t SPEAKER_BUTTON = {488, 56};
static constexpr lv_point_t PLAY_PAUSE = {612, 412};
static constexpr lv_point_t NEXT = {724, 412};
static constexpr lv_point_t TRY_AGAIN = {612, 420};

// Where the speaker picker's first two rows are.
static constexpr lv_point_t FIRST_SPEAKER = {400, 163};
static constexpr lv_point_t SECOND_SPEAKER = {400, 263};

// A fixture that does not read as its scene says fails the render.
static void expect(bool condition, const char* what) {
    if (!condition) {
        fprintf(stderr, "render: %s\n", what);
        exit(1);
    }
}

template <typename T>
static bool shows(const Shell& shell) {
    return dynamic_cast<const T*>(shell.get_current()) != nullptr;
}

static void loading(SimHost& host, Shell& shell) { shell.replace(std::make_unique<LoadingView>(&host, MSG_STARTING)); }

static void loading_update(SimHost& host, Shell& shell) {
    shell.replace(std::make_unique<LoadingView>(&host, MSG_CHECKING_FOR_UPDATE));
}

static void no_backend(SimHost& host, Shell& shell) {
    host.model().backend_url.clear();

    auto view = std::make_unique<LoadingView>(&host, MSG_CHECKING_FOR_UPDATE);
    auto loading_view = view.get();
    shell.replace(std::move(view));

    loading_view->set_error(MSG_NO_BACKEND);
}

static void answer_speakers(SimHost& host) {
    host.answer_speakers(200, SPEAKERS);

    const auto& list = host.get_model().speakers;
    expect(list.state == LoadState::Loaded, "the speakers fixture was not read as a list");
    expect(list.speakers.size() == 3, "the speakers fixture does not hold three speakers");
    expect(list.speakers[1] == Speaker{.id = "speaker-library", .name = "Bibliothèque"},
           "the speakers fixture's second speaker was not read as it is");
}

static void speakers(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-library";
    answer_speakers(host);

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));
}

static const char* check_held(const SimHost&, const Shell& shell) {
    return shows<SpeakerPickerView>(shell) ? nullptr : "the display is not held on the speaker picker";
}

// No speaker is chosen: the display is held on the picker.
static void speakers_none(SimHost& host, Shell& shell) {
    answer_speakers(host);

    shell.open();
}

// The stored speaker is not in the list: the display is held on the picker, nothing is marked,
// and the choice stays stored.
static void speakers_missing(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-shed";
    answer_speakers(host);

    shell.open();
}

static const char* check_missing(const SimHost& host, const Shell& shell) {
    if (host.get_model().speaker_id != "speaker-shed") {
        return "the list changed the stored choice";
    }

    return check_held(host, shell);
}

// The list lacked the stored speaker, and the backend cannot be reached since: the display stays
// held on the picker.
static void speakers_missing_unreachable(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-shed";
    answer_speakers(host);
    host.model().speakers.state = LoadState::Unreachable;

    shell.open();
}

// A touch on the third row picks its speaker.
static void speakers_pick(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    answer_speakers(host);

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));

    host.tap(400, 360);
}

static const char* check_pick(const SimHost& host, const Shell&) {
    return host.get_model().speaker_id == "speaker-bathroom" ? nullptr : "the touch did not pick the third speaker";
}

static void speakers_many(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-guests";
    host.answer_speakers(200, SPEAKERS_MANY);
    expect(host.get_model().speakers.speakers.size() == 5, "the long speakers fixture does not hold five speakers");

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));
}

static void speakers_loading(SimHost& host, Shell& shell) {
    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));
}

// An empty list lacks the stored speaker: the display is held on the picker, and the choice
// stays stored.
static void speakers_empty(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-library";
    host.answer_speakers(200, SPEAKERS_EMPTY);

    const auto& list = host.get_model().speakers;
    expect(list.state == LoadState::Loaded && list.speakers.empty(), "an empty list was not read as a list");

    shell.open();
}

static const char* check_empty(const SimHost& host, const Shell& shell) {
    if (host.get_model().speaker_id != "speaker-library") {
        return "an empty list changed the stored choice";
    }

    return check_held(host, shell);
}

static void speakers_unreachable(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-library";
    host.model().speakers.state = LoadState::Unreachable;

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));
}

// An error after a list: the error shows, and the list and the choice are kept.
static void speakers_error(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-library";
    answer_speakers(host);
    host.answer_speakers(500, ERROR_ENVELOPE);

    const auto& list = host.get_model().speakers;
    expect(list.state == LoadState::Failed, "an error status was not read as a failure");
    expect(list.error.starts_with("The Sonos household did not answer: no speaker"),
           "the envelope's error was not read");
    expect(list.speakers.size() == 3, "an error dropped the last list the backend answered");

    // What the backend says is kept up to 200 bytes, and never cut inside a character: here the
    // 200th and 201st bytes are one.
    const auto long_error = std::string(199, 'a');
    const auto long_body = R"({"error":")" + long_error + R"(éé","details":{"message":""}})";
    host.answer_speakers(500, long_body.c_str());
    expect(list.error == long_error, "a long error was not cut at a character");
    host.answer_speakers(500, ERROR_ENVELOPE);

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));
}

// Error bodies that are not the envelope, and answers that are not a list: each is a failure
// without a message.
static void speakers_unreadable(SimHost& host, Shell& shell) {
    const auto& list = host.get_model().speakers;

    host.answer_speakers(400, ERROR_VALIDATION);
    expect(list.state == LoadState::Failed && list.error.empty(), "a bare array was not read as a failure");

    for (const auto body : UNREADABLE) {
        host.answer_speakers(200, SPEAKERS_EMPTY);
        host.answer_speakers(200, body);
        expect(list.state == LoadState::Failed && list.error.empty(), "an unreadable answer was not refused");

        host.answer_speakers(502, body);
        expect(list.state == LoadState::Failed && list.error.empty(), "an unreadable error body gave a message");
    }

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));
}

// A touch on "Try again" asks for the speakers again.
static void speakers_retry(SimHost& host, Shell& shell) {
    host.model().speakers.state = LoadState::Unreachable;

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));

    host.tap(400, 372);
}

static const char* check_retry(const SimHost& host, const Shell&) {
    return host.get_model().speakers.state == LoadState::Loading ? nullptr : "the touch did not ask again";
}

static void speakers_reconnecting(SimHost& host, Shell& shell) {
    host.model().stream_state = StreamState::Disconnected;
    host.model().stream_down_since = lv_tick_get() - 10 * 1000;
    host.model().speaker_id = "speaker-library";
    answer_speakers(host);

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));
}

// The now-playing screen as the app opens it: a speaker chosen that the list has, and a playback
// answered.
static void open_now_playing(SimHost& host, Shell& shell, const char* playback) {
    host.model().speaker_id = "speaker-garage";
    answer_speakers(host);
    host.answer_playback(200, playback);
    expect(host.get_model().playback.state == LoadState::Loaded, "the playback fixture was not read as a playback");

    shell.open();
}

static const char* check_now_playing(const SimHost&, const Shell& shell) {
    return shows<NowPlayingView>(shell) ? nullptr : "the display is not on the now-playing screen";
}

static void now_playing(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);

    const auto& playback = host.get_model().playback;
    expect(playback.play_state == PlayState::Playing, "the playing fixture does not play");
    expect(playback.can_play && playback.can_pause && playback.can_next && playback.can_previous,
           "the playing fixture's actions were not read");
    expect(playback.track == Track{.title = "Svefn-g-englar", .artist = "Sigur Rós", .album = "Ágætis byrjun"},
           "the playing fixture's track was not read as it is");
}

static void now_playing_paused(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PAUSED);

    expect(host.get_model().playback.play_state == PlayState::Paused, "the paused fixture is not paused");
}

// A radio stream: the station as the artist, no album, pause live and Next unavailable. A touch
// on Next sends nothing.
static void now_playing_radio(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, RADIO);

    const auto& playback = host.get_model().playback;
    expect(playback.can_pause && !playback.can_next && !playback.can_previous,
           "the radio fixture's actions were not read");
    expect(playback.track && playback.track->artist == "Český rozhlas Vltava" && playback.track->album.empty(),
           "the radio fixture's track was not read as it is");

    host.tap(NEXT.x, NEXT.y);
}

static const char* check_radio(const SimHost& host, const Shell&) {
    return host.get_commands().empty() ? nullptr : "a control that is not live sent a command";
}

static void now_playing_nothing(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, NOTHING_LOADED);

    expect(!host.get_model().playback.track, "the fixture with nothing loaded has a track");
}

// A touch on pause shows at once, and the backend's 204 leaves it showing.
static void now_playing_press(SimHost& host, Shell& shell) {
    // A playback answered while the command is on its way is from before it.
    Playback playback;
    expect(!read_playback(200, PLAYING, strlen(PLAYING), playback), "the playing fixture was not read");
    playback.command_sent(Command::Pause);
    expect(!playback.is_playing() && playback.sending == Command::Pause, "a pause that was sent does not show");
    expect(!read_playback(200, PLAYING, strlen(PLAYING), playback) && playback.is_playing(),
           "an answer did not replace what a command showed");
    playback.command_taken(Command::Pause);
    expect(!playback.is_playing() && !playback.sending, "a pause that was taken does not show");

    std::string error;
    expect(!read_command(204, "", 0, error), "a 204 was not read as a command taken");

    open_now_playing(host, shell, PLAYING);
    host.answer_commands(204, "");

    host.tap(PLAY_PAUSE.x, PLAY_PAUSE.y);
}

static const char* check_press(const SimHost& host, const Shell&) {
    const auto& playback = host.get_model().playback;

    if (host.get_commands() != std::vector<Command>{Command::Pause}) {
        return "the touch did not send one pause";
    }

    return !playback.is_playing() && !playback.sending ? nullptr : "the pause does not show";
}

// A touch on Next shows while the backend has not answered it.
static void now_playing_next(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);

    host.tap(NEXT.x, NEXT.y);
}

static const char* check_next(const SimHost& host, const Shell&) {
    if (host.get_commands() != std::vector<Command>{Command::Next}) {
        return "the touch did not send one next";
    }

    return host.get_model().playback.sending == Command::Next ? nullptr : "the next does not show as on its way";
}

// A command the speaker refuses: the screen says it was not taken, and the playback fetched again
// no longer lists it.
static void now_playing_refused(SimHost& host, Shell& shell) {
    // Any status but the 204 is a command not taken, whatever its body.
    std::string error;
    expect(read_command(409, ERROR_NOT_AVAILABLE, strlen(ERROR_NOT_AVAILABLE), error) &&
               error == "The speaker does not allow next now",
           "a 409 was not read as a command refused");
    for (const auto body : UNREADABLE) {
        expect(read_command(502, body, strlen(body), error) && error.empty(),
               "a 502 without the envelope was not read as a command refused");
        expect(read_command(200, body, strlen(body), error), "a 200 was read as a command taken");
    }

    open_now_playing(host, shell, PLAYING);
    host.answer_commands(409, ERROR_NOT_AVAILABLE);
    host.answer_next_playback(200, PLAYING_LAST);

    host.tap(NEXT.x, NEXT.y);
}

static const char* check_refused(const SimHost& host, const Shell&) {
    const auto& playback = host.get_model().playback;

    if (host.get_commands() != std::vector<Command>{Command::Next}) {
        return "the touch did not send one next";
    }
    if (playback.commands_not_taken != 1 || playback.sending) {
        return "the refused command does not count as not taken";
    }

    return playback.state == LoadState::Loaded && !playback.can_next ? nullptr : "the playback was not fetched again";
}

// The first pick: from the picker the display is held on to the now-playing screen, where the
// playback is on its way.
static void now_playing_loading(SimHost& host, Shell& shell) {
    answer_speakers(host);

    shell.open();
    expect(shows<SpeakerPickerView>(shell), "the display is not held on the speaker picker");

    host.tap(FIRST_SPEAKER.x, FIRST_SPEAKER.y);
}

static const char* check_loading(const SimHost& host, const Shell& shell) {
    if (host.get_model().speaker_id != "speaker-garage") {
        return "the touch did not pick the first speaker";
    }
    if (host.get_model().playback.state != LoadState::Loading) {
        return "the pick did not ask for the playback";
    }

    return check_now_playing(host, shell);
}

// No list was ever answered: the stored choice stands, and the display is not held on the picker.
static void now_playing_unreachable(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    host.model().speakers.state = LoadState::Unreachable;
    host.model().playback.state = LoadState::Unreachable;

    shell.open();
}

// An error after a playback: the error shows, and the playback is kept.
static void now_playing_error(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);
    host.answer_playback(502, ERROR_SPEAKER);

    const auto& playback = host.get_model().playback;
    expect(playback.state == LoadState::Failed, "an error status was not read as a failure");
    expect(playback.error.starts_with("The speaker Garage did not answer"), "the envelope's error was not read");
    expect(playback.track && playback.track->title == "Svefn-g-englar",
           "an error dropped the last playback the backend answered");
}

// Error bodies that are not the envelope, and answers that are not a playback: each is a failure
// without a message. What the document allows is taken, whatever the display does not show holds.
static void now_playing_unreadable(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    answer_speakers(host);

    const auto& playback = host.get_model().playback;

    for (const auto body : UNREADABLE_PLAYBACKS) {
        host.answer_playback(200, PLAYING);
        host.answer_playback(200, body);
        expect(playback.state == LoadState::Failed && playback.error.empty(), "an unreadable playback was not refused");
        expect(playback.track && playback.can_next, "an unreadable playback changed the last one answered");

        host.answer_playback(502, body);
        expect(playback.state == LoadState::Failed && playback.error.empty(),
               "an unreadable error body gave a message");
    }

    // An action the display has no control for, and fields it does not show: missing, or not what
    // the document says.
    host.answer_playback(200, R"({"state":"buffering","actions":["seek","next"],"source":7,"track":)"
                              R"({"title":"","artist":"","album":null,"duration_ms":"long","lyrics":[]}})");
    expect(playback.state == LoadState::Loaded && playback.is_playing() && playback.can_next && !playback.can_pause,
           "a playback was refused for what the display does not show");

    // A text is kept up to 200 bytes, and never cut inside a character.
    const auto long_title = std::string(199, 'a');
    const auto long_body =
        R"({"state":"paused","actions":[],"track":{"title":")" + long_title + R"(éé","artist":"","album":null}})";
    host.answer_playback(200, long_body.c_str());
    expect(playback.track && playback.track->title == long_title, "a long title was not cut at a character");

    // An event's payload names its speaker, or is refused.
    std::string speaker_id;
    const auto read_event = [&speaker_id](const char* payload) {
        const auto json = std::unique_ptr<cJSON, void (*)(cJSON*)>(cJSON_Parse(payload), cJSON_Delete);
        return read_speaker_event(json.get(), speaker_id);
    };
    expect(!read_event(R"({"speaker_id":"speaker-garage"})") && speaker_id == "speaker-garage",
           "an event's speaker was not read");
    for (const auto payload :
         {"", "{}", "null", R"("speaker-garage")", R"({"speaker_id":7})", R"({"speaker_id":null})"}) {
        expect(read_event(payload), "an event without a speaker was not refused");
    }

    host.answer_playback(200, PLAYING);
    host.answer_playback(400, ERROR_VALIDATION);
    expect(playback.state == LoadState::Failed && playback.error.empty(), "a bare array was not read as a failure");

    shell.open();
}

// A touch on "Try again" asks for the playback again.
static void now_playing_retry(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    answer_speakers(host);
    host.model().playback.state = LoadState::Unreachable;

    shell.open();

    host.tap(TRY_AGAIN.x, TRY_AGAIN.y);
}

static const char* check_playback_retry(const SimHost& host, const Shell&) {
    return host.get_model().playback.state == LoadState::Loading ? nullptr : "the touch did not ask again";
}

static void now_playing_reconnecting(SimHost& host, Shell& shell) {
    host.model().stream_state = StreamState::Disconnected;
    host.model().stream_down_since = lv_tick_get() - 10 * 1000;

    open_now_playing(host, shell, PLAYING);
}

// A touch on the speaker button opens the picker over the now-playing screen.
static void now_playing_speakers(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);

    host.tap(SPEAKER_BUTTON.x, SPEAKER_BUTTON.y);
}

static const char* check_speakers_open(const SimHost&, const Shell& shell) {
    return shows<SpeakerPickerView>(shell) ? nullptr : "the touch did not open the speaker picker";
}

// A pick of the speaker that is chosen already changes nothing, and comes back to now playing.
static void now_playing_same_speaker(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);

    host.tap(SPEAKER_BUTTON.x, SPEAKER_BUTTON.y);
    host.tap(FIRST_SPEAKER.x, FIRST_SPEAKER.y);
}

static const char* check_same_speaker(const SimHost& host, const Shell& shell) {
    const auto& model = host.get_model();

    if (model.speaker_id != "speaker-garage" || !model.playback.track) {
        return "a pick of the chosen speaker changed what the display shows";
    }

    return check_now_playing(host, shell);
}

// A pick of another speaker comes back to now playing, which shows what that one plays.
static void now_playing_other_speaker(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);
    host.answer_next_playback(200, RADIO);

    host.tap(SPEAKER_BUTTON.x, SPEAKER_BUTTON.y);
    host.tap(SECOND_SPEAKER.x, SECOND_SPEAKER.y);
}

static const char* check_other_speaker(const SimHost& host, const Shell& shell) {
    const auto& model = host.get_model();

    if (model.speaker_id != "speaker-library") {
        return "the touch did not pick the second speaker";
    }
    if (!model.playback.track || model.playback.track->artist != "Český rozhlas Vltava") {
        return "the pick did not fetch what the speaker plays";
    }

    return check_now_playing(host, shell);
}

const std::vector<Scene>& get_scenes() {
    static const std::vector<Scene> scenes = {
        {"loading", loading},
        {"loading-update", loading_update},
        {"no-backend", no_backend},
        {"speakers", speakers},
        {"speakers-none", speakers_none, check_held},
        {"speakers-missing", speakers_missing, check_missing},
        {"speakers-missing-unreachable", speakers_missing_unreachable, check_missing},
        {"speakers-pick", speakers_pick, check_pick},
        {"speakers-many", speakers_many},
        {"speakers-loading", speakers_loading},
        {"speakers-empty", speakers_empty, check_empty},
        {"speakers-unreachable", speakers_unreachable},
        {"speakers-error", speakers_error},
        {"speakers-unreadable", speakers_unreadable},
        {"speakers-retry", speakers_retry, check_retry},
        {"speakers-reconnecting", speakers_reconnecting},
        {"now-playing", now_playing, check_now_playing},
        {"now-playing-paused", now_playing_paused},
        {"now-playing-radio", now_playing_radio, check_radio},
        {"now-playing-nothing", now_playing_nothing},
        {"now-playing-press", now_playing_press, check_press},
        {"now-playing-next", now_playing_next, check_next},
        {"now-playing-refused", now_playing_refused, check_refused},
        {"now-playing-loading", now_playing_loading, check_loading},
        {"now-playing-unreachable", now_playing_unreachable, check_now_playing},
        {"now-playing-error", now_playing_error},
        {"now-playing-unreadable", now_playing_unreadable},
        {"now-playing-retry", now_playing_retry, check_playback_retry},
        {"now-playing-reconnecting", now_playing_reconnecting},
        {"now-playing-speakers", now_playing_speakers, check_speakers_open},
        {"now-playing-same-speaker", now_playing_same_speaker, check_same_speaker},
        {"now-playing-other-speaker", now_playing_other_speaker, check_other_speaker},
    };

    return scenes;
}
