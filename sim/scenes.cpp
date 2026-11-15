#include "scenes.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Controls.h"
#include "LoadingView.h"
#include "Messages.h"
#include "NowPlayingView.h"
#include "SpeakerPickerView.h"
#include "api/Answers.h"
#include "api/Artwork.h"
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
    R"({"state":"playing","actions":["pause"],"track":{"title":"Svefn-g-englar","artist":"Sigur Rós","album":null}})",
    R"({"state":"playing","actions":["pause"],"track":{"title":"Svefn-g-englar","artist":"Sigur Rós","album":null,)"
    R"("artwork_id":7}})",
};

// The pictures of the playing and the paused fixtures' tracks, and the sizes the now-playing
// screen and the bar show a cover at.
static const char ARTWORK_PLAYING[] = "YXJ0d29yay9zdmVmbg";
static const char ARTWORK_PAUSED[] = "YXJ0d29yay9yYW1lYXU";
static constexpr int32_t COVER_SIZE = 400;
static constexpr int32_t BAR_COVER_SIZE = 80;

// One speaker, as GET /api/speakers/{speaker_id} answers it: the Garage of the list, alone in its
// group.
static const char SPEAKER[] =
    R"({"group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage"],"muted":false,"volume":24},)"
    R"("id":"speaker-garage","model":"Play:3","muted":false,"name":"Garage","volume":24})";

// A speaker of a group of two: the group's volume and mute are not its own.
static const char SPEAKER_GROUPED[] =
    R"({"group":{"coordinator_id":"speaker-library","member_ids":["speaker-library","speaker-bathroom"],)"
    R"("muted":false,"volume":31},"id":"speaker-library","model":"Playbase","muted":true,"name":"Bibliothèque",)"
    R"("volume":35})";

static const char SPEAKER_MUTED[] =
    R"({"group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage"],"muted":true,"volume":24},)"
    R"("id":"speaker-garage","model":"Play:3","muted":true,"name":"Garage","volume":24})";

static const char SPEAKER_LOUDEST[] =
    R"({"group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage"],"muted":false,"volume":100},)"
    R"("id":"speaker-garage","model":"Play:3","muted":false,"name":"Garage","volume":100})";

static const char ERROR_SPEAKER_NOT_FOUND[] =
    R"({"code":"SPEAKER_NOT_FOUND","correlationId":"8a41d0c7-5e2b-4f39-b6a8-1c9e7d3f2a50",)"
    R"("details":{"message":"The requested resource could not be found"},)"
    R"("error":"No speaker has the id speaker-garage"})";

// Speakers the API's document does not allow.
static const char* const UNREADABLE_SPEAKERS[] = {
    "",
    "<html><body>502 Bad Gateway</body></html>",
    R"({"group":{"volume":24,)",
    "null",
    "[]",
    R"({"id":"speaker-garage","name":"Garage","volume":24,"muted":false})",
    R"({"group":null})",
    R"({"group":[24,false]})",
    R"({"group":{"muted":false}})",
    R"({"group":{"volume":null,"muted":false}})",
    R"({"group":{"volume":"24","muted":false}})",
    R"({"group":{"volume":24.5,"muted":false}})",
    R"({"group":{"volume":-1,"muted":false}})",
    R"({"group":{"volume":101,"muted":false}})",
    R"({"group":{"volume":1e30,"muted":false}})",
    R"({"group":{"volume":24}})",
    R"({"group":{"volume":24,"muted":null}})",
    R"({"group":{"volume":24,"muted":0}})",
};

// What a change of the group's volume is answered with: the volume and the mute after it.
static const char VOLUME_26[] = R"({"muted":false,"volume":26})";
static const char VOLUME_34[] = R"({"muted":false,"volume":34})";

// Answers to a change that the API's document does not allow.
static const char* const UNREADABLE_VOLUMES[] = {
    "",
    "<html><body>502 Bad Gateway</body></html>",
    R"({"volume":26,)",
    "null",
    "[26,false]",
    R"({"muted":false})",
    R"({"volume":null,"muted":false})",
    R"({"volume":"26","muted":false})",
    R"({"volume":26.5,"muted":false})",
    R"({"volume":-1,"muted":false})",
    R"({"volume":101,"muted":false})",
    R"({"volume":26})",
    R"({"volume":26,"muted":"no"})",
};

// Where the now-playing screen's controls are.
static constexpr lv_point_t SPEAKER_BUTTON = {488, 56};
static constexpr lv_point_t PLAY_PAUSE = {612, 324};
static constexpr lv_point_t NEXT = {724, 324};
static constexpr lv_point_t TRY_AGAIN = {612, 332};
static constexpr lv_point_t VOLUME_DOWN = {492, 420};
static constexpr lv_point_t VOLUME_UP = {732, 420};

// Where the bar's controls are.
static constexpr lv_point_t BAR_PLAY_PAUSE = {400, 433};
static constexpr lv_point_t BAR_NEXT = {484, 433};
static constexpr lv_point_t BAR_VOLUME_DOWN = {580, 433};
static constexpr lv_point_t BAR_VOLUME_UP = {748, 433};

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

// Whether a label that is on screen says this.
static bool says(const lv_obj_t* obj, const char* text) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), text)) {
        return true;
    }

    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        if (says(lv_obj_get_child(obj, int32_t(i)), text)) {
            return true;
        }
    }

    return false;
}

static bool says(const char* text) { return says(lv_screen_active(), text); }

// Whether a button that is on screen is drawn in part only: an object it is in ends before it.
static bool cuts_button(const lv_obj_t* obj) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }

    if (lv_obj_check_type(obj, &lv_button_class)) {
        lv_area_t button;
        lv_obj_get_coords(obj, &button);

        for (auto parent = lv_obj_get_parent(obj); parent; parent = lv_obj_get_parent(parent)) {
            lv_area_t within;
            lv_obj_get_coords(parent, &within);

            if (button.x1 < within.x1 || button.y1 < within.y1 || button.x2 > within.x2 || button.y2 > within.y2) {
                return true;
            }
        }
    }

    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        if (cuts_button(lv_obj_get_child(obj, int32_t(i)))) {
            return true;
        }
    }

    return false;
}

static const char* check_no_bar(const SimHost&, const Shell& shell) {
    return shell.shows_bar() ? "the bar shows under a view that goes without it" : nullptr;
}

// The views of the start go without the bar, with a speaker chosen too: the stored choice is read
// before they show.
static void loading(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";

    shell.replace(std::make_unique<LoadingView>(&host, MSG_STARTING));
}

static void loading_update(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";

    shell.replace(std::make_unique<LoadingView>(&host, MSG_CHECKING_FOR_UPDATE));
}

static void no_backend(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
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

static const char* check_held(const SimHost& host, const Shell& shell) {
    if (!shows<SpeakerPickerView>(shell)) {
        return "the display is not held on the speaker picker";
    }

    return check_no_bar(host, shell);
}

// No speaker is chosen: the display is held on the picker.
static void speakers_none(SimHost& host, Shell& shell) {
    answer_speakers(host);

    shell.open();
}

// The Shell as the app opens it: with the stored choice, and no list answered yet.
static void open_before_list(SimHost& host, Shell& shell, const char* speaker_id) {
    host.model().speaker_id = speaker_id;

    shell.open();
    expect(shows<NowPlayingView>(shell), "a stored choice did not open on the now-playing screen");
}

// The list that arrives lacks the stored speaker: the display leaves the now-playing screen and
// is held on the picker, nothing is marked, and the choice stays stored.
static void speakers_missing(SimHost& host, Shell& shell) {
    open_before_list(host, shell, "speaker-shed");

    answer_speakers(host);
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
    open_before_list(host, shell, "speaker-shed");

    answer_speakers(host);
    host.model().speakers.state = LoadState::Unreachable;
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
    open_before_list(host, shell, "speaker-library");

    host.answer_speakers(200, SPEAKERS_EMPTY);

    const auto& list = host.get_model().speakers;
    expect(list.state == LoadState::Loaded && list.speakers.empty(), "an empty list was not read as a list");
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

// The picker's notice over the bar: "Try again" is whole above it.
static const char* check_notice_over_bar(const SimHost&, const Shell& shell) {
    if (!shell.shows_bar()) {
        return "the bar does not show";
    }
    if (!says(MSG_TRY_AGAIN)) {
        return "the notice has no \"Try again\"";
    }

    return cuts_button(lv_screen_active()) ? "\"Try again\" is cut" : nullptr;
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
// and the speaker answered.
static void open_now_playing(SimHost& host, Shell& shell, const char* playback, const char* speaker = SPEAKER) {
    host.model().speaker_id = "speaker-garage";
    answer_speakers(host);
    host.answer_playback(200, playback);
    expect(host.get_model().playback.state == LoadState::Loaded, "the playback fixture was not read as a playback");
    host.answer_volume(200, speaker);
    expect(host.get_model().volume.is_known(), "the speaker fixture was not read as a speaker");

    shell.open();
}

// The covers held are the loaded track's and no other: the now-playing screen's at its size, and
// the bar's at its own for as long as the bar shows.
static const char* check_cover(const SimHost& host, const Shell& shell) {
    const auto& playback = host.get_model().playback;
    const auto shows_track = playback.state == LoadState::Loaded && playback.track;
    const auto wanted = shows_track ? playback.track->artwork_id : std::string();

    for (const auto& fetch : host.get_artwork_fetches()) {
        if (fetch.size != COVER_SIZE && fetch.size != BAR_COVER_SIZE) {
            return "a cover was fetched at a size neither the screen nor the bar shows it at";
        }
        if (fetch.artwork_id != wanted && host.is_artwork_shown(fetch.artwork_id, fetch.size)) {
            return "the cover of another track than the loaded one is held";
        }
    }

    if (!wanted.empty() && !host.is_artwork_shown(wanted, COVER_SIZE)) {
        return "the loaded track's cover is not shown";
    }

    const auto in_bar = host.is_artwork_shown(wanted, BAR_COVER_SIZE);
    if (shell.shows_bar()) {
        return in_bar || wanted.empty() ? nullptr : "the bar does not show the loaded track's cover";
    }

    return in_bar ? "the bar holds a cover while it is hidden" : nullptr;
}

static const char* check_now_playing(const SimHost& host, const Shell& shell) {
    if (!shows<NowPlayingView>(shell)) {
        return "the display is not on the now-playing screen";
    }
    if (shell.shows_bar()) {
        return "the now-playing screen shows the bar";
    }

    return check_cover(host, shell);
}

static void now_playing(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);

    const auto& playback = host.get_model().playback;
    expect(playback.play_state == PlayState::Playing, "the playing fixture does not play");
    expect(playback.can_play && playback.can_pause && playback.can_next && playback.can_previous,
           "the playing fixture's actions were not read");
    expect(playback.track == Track{.title = "Svefn-g-englar",
                                   .artist = "Sigur Rós",
                                   .album = "Ágætis byrjun",
                                   .artwork_id = ARTWORK_PLAYING},
           "the playing fixture's track was not read as it is");
    expect(host.get_artwork_fetches() == std::vector<ArtworkFetch>{{ARTWORK_PLAYING, COVER_SIZE}},
           "the playing track's cover was not fetched once, at the screen's size");

    const auto& volume = host.get_model().volume;
    expect(volume.level == 24 && !volume.muted && volume.shown() == 24, "the speaker fixture's volume was not read");
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
    expect(playback.track && playback.track->artist == "Český rozhlas Vltava" && playback.track->album.empty() &&
               playback.track->artwork_id.empty(),
           "the radio fixture's track was not read as it is");

    host.tap(NEXT.x, NEXT.y);
}

static const char* check_radio(const SimHost& host, const Shell&) {
    if (!host.get_artwork_fetches().empty()) {
        return "a cover was fetched for a track that has none";
    }

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

static const char* check_refused_said(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_refused(host, shell)) {
        return problem;
    }

    return says(MSG_COMMAND_NOT_TAKEN) ? nullptr : "the now-playing screen does not say that the command was not taken";
}

// A scene that runs this long is drawn after a view has stopped saying that a command was not
// taken.
static constexpr uint32_t AFTER_NOTICE_MS = NOT_TAKEN_NOTICE_MS + 1000;

static const char* check_refused_later(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_refused(host, shell)) {
        return problem;
    }
    if (says(MSG_COMMAND_NOT_TAKEN)) {
        return "the now-playing screen still says that the command was not taken";
    }

    return check_now_playing(host, shell);
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
    if (host.get_model().volume.state != LoadState::Loading) {
        return "the pick did not ask for the volume";
    }

    return check_now_playing(host, shell);
}

// No list was ever answered: the stored choice stands, and the display is not held on the picker.
static void now_playing_unreachable(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    host.model().speakers.state = LoadState::Unreachable;
    host.model().playback.state = LoadState::Unreachable;
    host.model().volume.state = LoadState::Unreachable;

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
                              R"({"title":"","artist":"","album":null,"artwork_id":null,"duration_ms":"long",)"
                              R"("lyrics":[]}})");
    expect(playback.state == LoadState::Loaded && playback.is_playing() && playback.can_next && !playback.can_pause,
           "a playback was refused for what the display does not show");

    // A text is kept up to 200 bytes, and never cut inside a character.
    const auto long_title = std::string(199, 'a');
    const auto long_body = R"({"state":"paused","actions":[],"track":{"title":")" + long_title +
                           R"(éé","artist":"","album":null,"artwork_id":null}})";
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

// A touch on "Try again" asks for the playback and the volume again.
static void now_playing_retry(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    answer_speakers(host);
    host.model().playback.state = LoadState::Unreachable;
    host.model().volume.state = LoadState::Unreachable;

    shell.open();

    host.tap(TRY_AGAIN.x, TRY_AGAIN.y);
}

static const char* check_playback_retry(const SimHost& host, const Shell&) {
    if (host.get_model().volume.state != LoadState::Loading) {
        return "the touch did not ask for the volume again";
    }

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

static const char* check_bar(const SimHost& host, const Shell& shell) {
    if (!shows<SpeakerPickerView>(shell)) {
        return "the display is not on the speaker picker";
    }
    if (!shell.shows_bar()) {
        return "the picker reached from the now-playing screen does not show the bar";
    }

    return check_cover(host, shell);
}

static const char* check_speakers_open(const SimHost& host, const Shell& shell) {
    if (!shows<SpeakerPickerView>(shell)) {
        return "the touch did not open the speaker picker";
    }

    return check_bar(host, shell);
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
    host.answer_next_volume(200, SPEAKER_GROUPED);

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
    if (!model.volume.is_known() || model.volume.level != 31 || model.volume.muted) {
        return "the pick did not fetch the volume of the speaker's group";
    }

    return check_now_playing(host, shell);
}

// A touch on up shows in the level at once, and the backend's answer leaves it showing.
static void now_playing_volume_up(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);
    host.answer_volume_changes(200, VOLUME_26);

    host.tap(VOLUME_UP.x, VOLUME_UP.y);
}

static const char* check_volume_up(const SimHost& host, const Shell&) {
    const auto& volume = host.get_model().volume;

    if (host.get_volume_changes() != std::vector<int>{Volume::STEP}) {
        return "the touch did not send one step up";
    }

    return volume.level == 26 && volume.shown() == 26 && !volume.is_changing() ? nullptr
                                                                               : "the level is not the one answered";
}

// A burst of presses: one change is on its way, and the presses since wait to go as one. The
// level shows where they lead.
static void now_playing_volume_burst(SimHost& host, Shell& shell) {
    // Five presses on up and one on down while the first is on its way are one change after it,
    // and the level ends where they led.
    {
        SimHost burst;
        burst.answer_volume(200, SPEAKER);

        for (auto i = 0; i < 5; i++) {
            burst.change_volume(Volume::STEP);
        }
        burst.change_volume(-Volume::STEP);

        const auto& volume = burst.get_model().volume;
        expect(burst.get_volume_changes() == std::vector<int>{2}, "a burst sent more than its first press");
        expect(volume.shown() == 32, "a burst does not show the level pressed to");

        // An answer from before the change, which a volume_changed fetched, leaves the presses
        // showing.
        burst.answer_volume(200, SPEAKER);
        expect(volume.shown() == 32, "a level answered during a burst dropped the presses");

        burst.answer_volume_change(200, VOLUME_26);
        expect(burst.get_volume_changes() == std::vector<int>{2, 6}, "the presses of a burst did not go as one change");
        expect(volume.shown() == 32, "the level moved while the burst's second change was on its way");

        burst.answer_volume_change(200, R"({"muted":false,"volume":32})");
        expect(burst.get_volume_changes().size() == 2 && !volume.is_changing() && volume.level == 32,
               "a burst did not end at the level pressed to");

        // Presses that cancel each other send nothing.
        burst.change_volume(Volume::STEP);
        burst.change_volume(Volume::STEP);
        burst.change_volume(-Volume::STEP);
        burst.answer_volume_change(200, VOLUME_34);
        expect(burst.get_volume_changes().size() == 3 && !volume.is_changing(), "cancelled presses sent a change");

        // The speaker's own answer is the level, whatever the presses led to.
        burst.change_volume(Volume::STEP);
        burst.answer_volume_change(200, R"({"muted":true,"volume":50})");
        expect(volume.shown() == 50 && volume.muted, "the answer to a change is not the level shown");
    }

    // The level stays within 0 to 100, and no press goes past either end.
    {
        Volume volume;
        std::string error;
        const auto answer = [&volume, &error](const char* body) {
            return read_volume_change(200, body, strlen(body), volume, error);
        };
        expect(!answer(R"({"muted":false,"volume":3})") && volume.shown() == 3, "the answer to a change was not read");

        volume.pressed(-Volume::STEP);
        volume.pressed(-Volume::STEP);
        expect(volume.shown() == 0 && volume.waiting == -3, "the level went under 0");
        expect(!volume.allows(-Volume::STEP) && volume.allows(Volume::STEP), "down is live at 0");
        expect(volume.next_change() == -3 && !volume.next_change(), "a second change went while one was on its way");

        expect(!answer(R"({"muted":false,"volume":99})"), "the answer to a change was not read");
        volume.change_taken();
        volume.pressed(Volume::STEP);
        expect(volume.shown() == 100 && volume.waiting == 1, "the level went over 100");
        expect(volume.allows(-Volume::STEP) && !volume.allows(Volume::STEP), "up is live at 100");
    }

    open_now_playing(host, shell, PLAYING);

    host.tap(VOLUME_UP.x, VOLUME_UP.y);
    host.tap(VOLUME_UP.x, VOLUME_UP.y);
}

static const char* check_volume_burst(const SimHost& host, const Shell&) {
    const auto& volume = host.get_model().volume;

    if (host.get_volume_changes() != std::vector<int>{Volume::STEP}) {
        return "two touches did not send one change";
    }
    if (volume.sending != Volume::STEP || volume.waiting != Volume::STEP) {
        return "the second touch does not wait for the first one's answer";
    }

    return volume.shown() == 28 ? nullptr : "the level is not the one pressed to";
}

// The group is muted: the level shows as not in effect.
static void now_playing_volume_muted(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PAUSED, SPEAKER_MUTED);

    expect(host.get_model().volume.muted, "the muted fixture is not muted");

    // The group's volume and mute are shown, not the speaker's own.
    Volume volume;
    std::string error;
    expect(!read_speaker_volume(200, SPEAKER_GROUPED, strlen(SPEAKER_GROUPED), volume, error) && volume.level == 31 &&
               !volume.muted,
           "the group's volume was not read");
}

// At the top of the range up is unavailable, and a touch on it sends nothing.
static void now_playing_volume_limit(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING, SPEAKER_LOUDEST);

    host.tap(VOLUME_UP.x, VOLUME_UP.y);
}

static const char* check_volume_limit(const SimHost& host, const Shell&) {
    return host.get_volume_changes().empty() ? nullptr : "up sent a change at the top of the range";
}

// A change the backend refuses: the screen says it was not taken, the presses since are dropped,
// and the volume fetched again shows.
static void now_playing_volume_refused(SimHost& host, Shell& shell) {
    // Anything but a 200 with a volume is a change not taken, and leaves the level as it was.
    Volume volume;
    std::string error;
    expect(!read_speaker_volume(200, SPEAKER, strlen(SPEAKER), volume, error), "the speaker fixture was not read");
    expect(read_volume_change(502, ERROR_SPEAKER, strlen(ERROR_SPEAKER), volume, error) &&
               error.starts_with("The speaker Garage did not answer"),
           "a 502 was not read as a change refused");
    expect(read_volume_change(400, ERROR_VALIDATION, strlen(ERROR_VALIDATION), volume, error) && error.empty(),
           "a bare array was not read as a change refused");
    expect(read_volume_change(204, "", 0, volume, error), "a 204 was read as a change taken");
    for (const auto body : UNREADABLE_VOLUMES) {
        expect(read_volume_change(200, body, strlen(body), volume, error) && error.empty(),
               "an unreadable answer to a change was not refused");
        expect(read_volume_change(502, body, strlen(body), volume, error) && error.empty(),
               "an unreadable error body gave a message");
    }
    expect(volume.is_known() && volume.level == 24, "a change refused changed the level");

    volume.pressed(-Volume::STEP);
    expect(volume.next_change() == -Volume::STEP, "a press did not go");
    volume.pressed(-Volume::STEP);
    volume.change_not_taken();
    expect(!volume.is_changing() && volume.shown() == 24 && volume.changes_not_taken == 1,
           "a change refused left presses waiting");

    open_now_playing(host, shell, PLAYING);
    host.answer_volume_changes(502, ERROR_SPEAKER);
    host.answer_next_volume(200, SPEAKER_MUTED);

    host.tap(VOLUME_DOWN.x, VOLUME_DOWN.y);
}

static const char* check_volume_refused(const SimHost& host, const Shell&) {
    const auto& volume = host.get_model().volume;

    if (host.get_volume_changes() != std::vector<int>{-Volume::STEP}) {
        return "the touch did not send one step down";
    }
    if (volume.changes_not_taken != 1 || volume.is_changing()) {
        return "the refused change does not count as not taken";
    }

    return volume.is_known() && volume.muted ? nullptr : "the volume was not fetched again";
}

// Error bodies that are not the envelope, and answers that are not a speaker: each leaves the
// level unknown, and the last one answered in place. What the document allows is taken, whatever
// the display does not show holds.
static void now_playing_volume_unknown(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);

    const auto& volume = host.get_model().volume;

    for (const auto body : UNREADABLE_SPEAKERS) {
        host.answer_volume(200, SPEAKER);
        host.answer_volume(200, body);
        expect(volume.state == LoadState::Failed && volume.level == 24, "an unreadable speaker was not refused");

        host.answer_volume(502, body);
        expect(volume.state == LoadState::Failed, "an unreadable error body was read as a speaker");
    }

    host.answer_volume(200, R"({"group":{"volume":0,"muted":true,"member_ids":7},"id":null,"volume":"loud"})");
    expect(volume.is_known() && volume.level == 0 && volume.muted,
           "a speaker was refused for what the display does not show");

    std::string error;
    Volume gone;
    expect(read_speaker_volume(404, ERROR_SPEAKER_NOT_FOUND, strlen(ERROR_SPEAKER_NOT_FOUND), gone, error) &&
               error == "No speaker has the id speaker-garage" && gone.state == LoadState::Failed,
           "a 404 was not read as a failure");

    host.answer_volume(400, ERROR_VALIDATION);
    expect(volume.state == LoadState::Failed, "a bare array was not read as a failure");
}

static const char* check_volume_unknown(const SimHost& host, const Shell&) {
    const auto& volume = host.get_model().volume;

    return volume.allows(Volume::STEP) && volume.allows(-Volume::STEP) ? nullptr
                                                                       : "an unknown level leaves no press to make";
}

// The level could not be fetched: a touch on up sends one step without it, and nothing more goes
// until that one is answered. The answer brings the level.
static void now_playing_volume_blind(SimHost& host, Shell& shell) {
    {
        SimHost blind;
        blind.model().volume.state = LoadState::Unreachable;

        blind.change_volume(-Volume::STEP);
        expect(!blind.get_model().volume.allows(Volume::STEP) && !blind.get_model().volume.allows(-Volume::STEP),
               "a second step can go while the level is unknown");

        blind.answer_volume_change(200, VOLUME_26);
        expect(blind.get_volume_changes() == std::vector<int>{-Volume::STEP} && blind.get_model().volume.shown() == 26,
               "the answer to a step did not bring the level");
    }

    open_now_playing(host, shell, PLAYING);
    host.model().volume.state = LoadState::Unreachable;

    host.tap(VOLUME_UP.x, VOLUME_UP.y);
    host.tap(VOLUME_UP.x, VOLUME_UP.y);
}

static const char* check_volume_blind(const SimHost& host, const Shell&) {
    return host.get_volume_changes() == std::vector<int>{Volume::STEP} ? nullptr : "two touches did not send one step";
}

// A cover as the backend answers it, read as the device reads it.
static Artwork read_cover(int status, const std::vector<uint8_t>& body, int32_t size = COVER_SIZE) {
    const auto bytes = std::make_shared<std::vector<uint8_t>>(body);

    Artwork artwork;
    const auto problem = read_artwork(status, ArtworkBody(bytes, bytes->data()), bytes->size(), size, artwork);
    expect(!problem == (artwork != nullptr), "an answer was refused and read, or neither");

    return artwork;
}

// The cache keeps what fits 1.5 MiB, four covers: the least recently used goes first, and an image
// a view holds never.
static void expect_cache_rules() {
    static constexpr size_t COVER_BYTES = 12 + COVER_SIZE * COVER_SIZE * 2;

    ArtworkCache cache;
    std::map<std::string, std::weak_ptr<const lv_image_dsc_t>> images;

    const auto add = [&cache, &images](const char* id) {
        const auto cover = read_cover(200, SimHost::make_artwork(id, COVER_SIZE));
        images[id] = cover;

        return cache.add(id, COVER_SIZE, cover, COVER_BYTES);
    };
    const auto freed = [&images](const char* id) { return images[id].expired(); };
    const auto use = [&cache](const char* id) { return cache.find(id, COVER_SIZE); };

    expect(add("a") && add("b") && add("c") && add("d"), "four covers do not fit the cache");
    expect(cache.get_bytes() == 4 * COVER_BYTES && !freed("a"), "the cache does not keep four covers");
    expect(!cache.find("a", 80) && !use("z"), "the cache found an image it does not have");
    expect(use("a") && use("b") && use("c") && use("d"), "the cache lost a cover");

    // The fifth takes the place of the one used longest ago.
    expect(add("e") && freed("a") && !freed("b"), "the least recently used cover was not the one freed");
    expect(cache.get_bytes() == 4 * COVER_BYTES, "the cache does not count what it freed");

    // A cover that was used since is no longer that one.
    expect(use("b") && add("f") && freed("c") && !freed("b"), "a cover that was used was freed before an older one");

    // A cover a view holds stays, however long ago it was used.
    {
        const auto held = use("d");
        expect(use("e") && use("b") && use("f"), "the cache lost a cover");
        expect(cache.is_pinned("d", COVER_SIZE) && !cache.is_pinned("e", COVER_SIZE), "a held cover is not pinned");
        expect(add("g") && freed("e") && !freed("d"), "a cover on screen was freed");

        // With every cover held there is no room: the new one is not kept, and nothing is freed.
        const std::vector<Artwork> held_too = {use("g"), use("f"), use("b")};
        expect(!add("h") && freed("h"), "a cover was kept with the cache full of covers on screen");
        expect(!freed("d") && !freed("g") && !freed("f") && !freed("b") && cache.get_bytes() == 4 * COVER_BYTES,
               "a cover on screen was freed");
    }

    expect(add("h") && freed("d"), "a cover no view holds any more was not freed");
    expect(!cache.add("i", COVER_SIZE, read_cover(200, SimHost::make_artwork("i", COVER_SIZE)),
                      ArtworkCache::MAX_BYTES + 1),
           "an image larger than the cache was kept");
}

// Two requests for one image that are both on their way get the same image, and a later one gets
// it from the cache.
static void expect_one_image_for_two_requests() {
    SimHost host;
    host.hold_artwork();

    const auto scope = std::make_shared<int>(0);
    Artwork images[3];
    const auto request = [&](Artwork& image) {
        host.request_artwork(ARTWORK_PLAYING, COVER_SIZE, scope, [&image](Artwork artwork) { image = artwork; });
    };

    request(images[0]);
    request(images[1]);
    expect(!images[0] && !images[1], "artwork was handed over before the backend answered");

    host.answer_artwork();
    expect(images[0] && images[0] == images[1], "two requests for one image did not get the same one");

    request(images[2]);
    expect(images[2] == images[0] && host.get_artwork_fetches().size() == 2, "a cached image was fetched again");
}

// With the cache full of covers that are on screen, one more is fetched and not kept: its asker is
// told there is none.
static void expect_no_cover_beside_a_full_screen() {
    SimHost host;

    const auto scope = std::make_shared<int>(0);
    std::vector<Artwork> images;
    bool answered = false;
    const auto request = [&](const char* id) {
        answered = false;
        host.request_artwork(id, COVER_SIZE, scope, [&](Artwork artwork) {
            answered = true;
            images.push_back(std::move(artwork));
        });
    };

    for (const auto id : {"a", "b", "c", "d"}) {
        request(id);
        expect(images.back() != nullptr, "a cover that fits the cache was not handed over");
    }

    request("e");
    expect(answered && !images.back(), "a cover was kept with the cache full of covers on screen");
    expect(host.get_artwork_fetches().size() == 5 && host.is_artwork_shown("a", COVER_SIZE),
           "the covers on screen did not stay");

    // One of them leaves the screen, and the next cover takes its place.
    images[0].reset();
    request("e");
    expect(images.back() != nullptr && !host.is_artwork_shown("a", COVER_SIZE), "a cover off screen was not freed");
}

// A view that closes takes its request with it.
static void expect_no_cover_for_a_closed_view() {
    SimHost host;
    host.hold_artwork();

    Shell shell(&host);
    shell.begin(lv_display_get_default());

    host.model().speaker_id = "speaker-garage";
    host.answer_playback(200, PLAYING);
    shell.open();
    expect(host.get_artwork_fetches().size() == 1, "the now-playing screen did not ask for its cover");

    shell.replace(std::make_unique<LoadingView>(&host, MSG_STARTING));
    host.answer_artwork();
    expect(!host.is_artwork_shown(ARTWORK_PLAYING, COVER_SIZE), "a view that closed was handed its cover");
}

// Another track, with another cover: the placeholder shows while the cover is on its way, a cover
// that is no longer wanted is not waited for, and one the display has already is not fetched again.
static void now_playing_cover_change(SimHost& host, Shell& shell) {
    expect_cache_rules();
    expect_one_image_for_two_requests();
    expect_no_cover_beside_a_full_screen();
    expect_no_cover_for_a_closed_view();

    open_now_playing(host, shell, PLAYING);
    expect(host.is_artwork_shown(ARTWORK_PLAYING, COVER_SIZE), "the playing track's cover is not shown");

    const auto shown = [&host, &shell](const char* playback) {
        host.answer_playback(200, playback);
        shell.update();

        return check_cover(host, shell);
    };

    // The next track's cover is on its way: the one of the track before it does not stay.
    host.hold_artwork();
    expect(shown(PAUSED) != nullptr, "a cover was shown before the backend answered it");
    expect(!host.is_artwork_shown(ARTWORK_PLAYING, COVER_SIZE), "the cover of the track before stayed on screen");

    // A radio stream before it arrived: the answer that comes is nobody's.
    expect(!shown(RADIO), "a radio stream shows a cover");
    host.answer_artwork();
    expect(!shown(RADIO), "a cover that was no longer wanted was shown");

    // Back on the first track: its cover is in the cache.
    expect(!shown(PLAYING), "a cover the display has was not shown at once");
    expect(host.get_artwork_fetches().size() == 2, "a cover the display has was fetched again");

    // A failure hides the cover with the track, and the next answer brings both back.
    host.answer_playback(502, ERROR_SPEAKER);
    shell.update();
    expect(!host.is_artwork_shown(ARTWORK_PLAYING, COVER_SIZE), "a cover stayed on screen beside an error");
    expect(!shown(PLAYING), "the cover did not come back with the track");

    expect(shown(PAUSED) != nullptr, "a cover was shown before the backend answered it");
    host.answer_artwork();
}

static const char* check_cover_change(const SimHost& host, const Shell& shell) {
    const std::vector<ArtworkFetch> fetches = {
        {ARTWORK_PLAYING, COVER_SIZE},
        {ARTWORK_PAUSED, COVER_SIZE},
        {ARTWORK_PAUSED, COVER_SIZE},
    };
    if (host.get_artwork_fetches() != fetches) {
        return "the covers were not fetched as the tracks changed";
    }

    return check_cover(host, shell);
}

// An image that fails its checks, or an answer that is not an image, keeps the placeholder.
static void now_playing_cover_refused(SimHost& host, Shell& shell) {
    static constexpr uint8_t WITNESSED_HEADER[] = {0x19, 0x12, 0, 0, 0x90, 0x01, 0x90, 0x01, 0x20, 0x03, 0, 0};

    const auto good = SimHost::make_artwork(ARTWORK_PLAYING, COVER_SIZE);
    expect(good.size() == 320012 && !memcmp(good.data(), WITNESSED_HEADER, sizeof(WITNESSED_HEADER)),
           "the generated cover is not in the form the backend answers");

    const auto cover = read_cover(200, good);
    expect(cover && cover->header.w == 400 && cover->header.h == 400 && cover->header.stride == 800 &&
               cover->header.cf == LV_COLOR_FORMAT_RGB565 && cover->data_size == 320000,
           "a cover in the backend's form was not read as it is");

    const auto with = [&good](size_t index, uint8_t value) {
        auto damaged = good;
        damaged[index] = value;

        return damaged;
    };
    const auto text = [](const char* body) { return std::vector<uint8_t>(body, body + strlen(body)); };

    // The magic byte, an RGB888 image, a compressed one and another flag, a width, a height and a
    // stride that are not the size's.
    const std::vector<uint8_t> damaged[] = {
        with(0, 0x18),
        with(1, 0x0F),
        with(2, 0x08),
        with(3, 0x01),
        with(4, 0x8F),
        with(6, 0x8F),
        with(8, 0x90),
        with(9, 0x01),
        std::vector<uint8_t>(good.begin(), good.end() - 1),
        std::vector<uint8_t>(good.begin(), good.begin() + 12),
        std::vector<uint8_t>(good.begin(), good.begin() + 11),
        {},
        SimHost::make_artwork(ARTWORK_PLAYING, 80),
        text(ERROR_SPEAKER),
        text("<html><body>502 Bad Gateway</body></html>"),
    };
    for (const auto& body : damaged) {
        expect(!read_cover(200, body), "an image that fails its checks was taken");
    }

    auto longer = good;
    longer.push_back(0);
    expect(!read_cover(200, longer), "an image longer than its pixels was taken");
    expect(!read_cover(200, good, 80), "an image of another size than the one asked for was taken");

    // No picture has the id, the size is refused, the speaker did not give the picture.
    expect(!read_cover(404, text(ERROR_SPEAKER_NOT_FOUND)) && !read_cover(400, text(ERROR_VALIDATION)) &&
               !read_cover(502, text(ERROR_SPEAKER)) && !read_cover(404, good),
           "an error status was read as an image");

    const auto plays = [&host, &shell](const char* playback) {
        host.answer_playback(200, playback);
        shell.update();
    };
    const auto fetched = [&host]() { return host.get_artwork_fetches().size(); };

    // An image that fails its checks: the placeholder stays, and nothing asks again by itself.
    host.answer_artwork_with(200, with(2, 0x08));
    open_now_playing(host, shell, PLAYING);
    shell.update();
    expect(fetched() == 1 && !host.is_artwork_shown(ARTWORK_PLAYING, COVER_SIZE),
           "an image that fails its checks is shown, or was asked for again with nothing changed");

    // The backend out of reach: the next change of what plays asked again, to no avail.
    host.set_artwork_reachable(false);
    plays(PLAYING);
    expect(fetched() == 2 && !host.is_artwork_shown(ARTWORK_PLAYING, COVER_SIZE),
           "a cover the backend did not give was not asked for again when what plays changed");

    // The album's next track has the same cover, and brings it.
    host.set_artwork_reachable(true);
    host.answer_artwork_as_made();
    plays(PLAYING_LAST);
    expect(fetched() == 3 && host.is_artwork_shown(ARTWORK_PLAYING, COVER_SIZE),
           "the cover did not come with the album's next track");

    // No picture has the next track's id.
    host.answer_artwork_with(404, text(ERROR_SPEAKER_NOT_FOUND));
    plays(PAUSED);
}

static const char* check_cover_refused(const SimHost& host, const Shell& shell) {
    const std::vector<ArtworkFetch> fetches = {
        {ARTWORK_PLAYING, COVER_SIZE},
        {ARTWORK_PLAYING, COVER_SIZE},
        {ARTWORK_PLAYING, COVER_SIZE},
        {ARTWORK_PAUSED, COVER_SIZE},
    };
    if (host.get_artwork_fetches() != fetches) {
        return "the covers were not asked for as the tracks changed";
    }
    if (host.is_artwork_shown(ARTWORK_PLAYING, COVER_SIZE) || host.is_artwork_shown(ARTWORK_PAUSED, COVER_SIZE)) {
        return "a cover is shown for a track whose picture the backend did not give";
    }

    return shows<NowPlayingView>(shell) ? nullptr : "the display is not on the now-playing screen";
}

// Nothing is fetched while the screen is dark, and the cover of what plays comes once it wakes.
static void now_playing_cover_wake(SimHost& host, Shell& shell) {
    host.set_screen_lit(false);

    open_now_playing(host, shell, PLAYING);

    const auto plays = [&host, &shell](const char* playback) {
        host.answer_playback(200, playback);
        shell.update();
    };

    // The screen wakes on a radio stream: the cover asked for in the dark is nobody's by then.
    plays(RADIO);
    expect(host.get_artwork_fetches().empty(), "artwork was fetched while the screen was dark");
    host.set_screen_lit(true);
    expect(host.get_artwork_fetches().empty(), "a cover that was no longer wanted was fetched when the screen woke");

    // The tracks go on through the next dark: only the cover of the last one waits for the screen.
    host.set_screen_lit(false);
    plays(PLAYING);
    plays(RADIO);
    plays(PAUSED);
    plays(PLAYING_LAST);
    expect(host.get_artwork_fetches().empty(), "artwork was fetched while the screen was dark");
    expect(host.count_artwork_waiting() == 1, "covers that are no longer wanted wait for the screen");
    expect(!host.is_artwork_shown(ARTWORK_PLAYING, COVER_SIZE), "a cover is shown that was never fetched");

    host.set_screen_lit(true);
}

static const char* check_cover_wake(const SimHost& host, const Shell& shell) {
    if (host.get_artwork_fetches() != std::vector<ArtworkFetch>{{ARTWORK_PLAYING, COVER_SIZE}}) {
        return "the cover of what plays was not fetched once when the screen woke";
    }

    return check_cover(host, shell);
}

// The picker reached from the now-playing screen, which is where the bar shows.
static void open_bar(SimHost& host, Shell& shell, const char* playback, const char* speaker = SPEAKER) {
    open_now_playing(host, shell, playback, speaker);
    expect(!shell.shows_bar(), "the now-playing screen shows the bar");

    shell.open_speaker_picker();
    expect(shell.shows_bar(), "the picker reached from the now-playing screen does not show the bar");
}

// The bar under the picker: the cover at its own size, beside the one the now-playing screen
// under the picker still holds.
static void bar(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);

    const std::vector<ArtworkFetch> fetches = {
        {ARTWORK_PLAYING, COVER_SIZE},
        {ARTWORK_PLAYING, BAR_COVER_SIZE},
    };
    expect(host.get_artwork_fetches() == fetches, "the bar's cover was not fetched once, at the bar's size");

    static constexpr uint8_t WITNESSED_HEADER[] = {0x19, 0x12, 0, 0, 0x50, 0, 0x50, 0, 0xA0, 0, 0, 0};
    const auto cover = SimHost::make_artwork(ARTWORK_PLAYING, BAR_COVER_SIZE);
    expect(cover.size() == 12812 && !memcmp(cover.data(), WITNESSED_HEADER, sizeof(WITNESSED_HEADER)),
           "the generated cover of the bar is not in the form the backend answers");
}

// Paused, with texts longer than their places.
static void bar_paused(SimHost& host, Shell& shell) { open_bar(host, shell, PAUSED); }

// A radio stream: the station under the title, the placeholder, pause live and Next unavailable.
// A touch on Next sends nothing.
static void bar_radio(SimHost& host, Shell& shell) {
    open_bar(host, shell, RADIO);

    host.tap(BAR_NEXT.x, BAR_NEXT.y);
}

static const char* check_bar_radio(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_radio(host, shell)) {
        return problem;
    }
    if (!host.get_model().playback.can_pause) {
        return "the radio stream does not allow a pause";
    }

    return check_bar(host, shell);
}

static const char* check_nothing_sent(const SimHost& host, const Shell& shell) {
    if (!host.get_commands().empty()) {
        return "a control that is not live sent a command";
    }
    if (!host.get_volume_changes().empty()) {
        return "a volume control that is not live sent a change";
    }

    return shell.shows_bar() ? nullptr : "the bar does not show";
}

// Nothing is loaded: neither play nor next is live.
static void bar_nothing(SimHost& host, Shell& shell) {
    open_bar(host, shell, NOTHING_LOADED);

    host.tap(BAR_PLAY_PAUSE.x, BAR_PLAY_PAUSE.y);
    host.tap(BAR_NEXT.x, BAR_NEXT.y);
}

// The playback and the volume are on their way: no control is live.
static void bar_loading(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    answer_speakers(host);

    shell.open();
    shell.open_speaker_picker();

    host.tap(BAR_PLAY_PAUSE.x, BAR_PLAY_PAUSE.y);
    host.tap(BAR_VOLUME_UP.x, BAR_VOLUME_UP.y);
}

// The backend cannot be reached: the bar says so, no command goes, and one step of the volume
// goes without the level.
static void bar_unreachable(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    host.model().speakers.state = LoadState::Unreachable;
    host.model().playback.state = LoadState::Unreachable;
    host.model().volume.state = LoadState::Unreachable;

    shell.open();
    shell.open_speaker_picker();

    host.tap(BAR_PLAY_PAUSE.x, BAR_PLAY_PAUSE.y);
    host.tap(BAR_VOLUME_UP.x, BAR_VOLUME_UP.y);
}

static const char* check_bar_unreachable(const SimHost& host, const Shell& shell) {
    if (!host.get_commands().empty()) {
        return "a control that is not live sent a command";
    }
    if (host.get_volume_changes() != std::vector<int>{Volume::STEP}) {
        return "the touch did not send one step without the level";
    }

    return shell.shows_bar() && says(MSG_BACKEND_UNREACHABLE) ? nullptr : "the bar does not say what is wrong";
}

// An error after a playback: the bar says so in the track's place, without its cover, and the
// commands the last playback listed are not live.
static void bar_error(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);
    host.answer_playback(502, ERROR_SPEAKER);

    host.tap(BAR_NEXT.x, BAR_NEXT.y);
}

static const char* check_bar_error(const SimHost& host, const Shell& shell) {
    if (!host.get_commands().empty()) {
        return "a control that is not live sent a command";
    }
    if (host.is_artwork_shown(ARTWORK_PLAYING, BAR_COVER_SIZE)) {
        return "the bar shows a cover beside an error";
    }

    return shell.shows_bar() && says(MSG_BACKEND_FAILED) ? nullptr : "the bar does not say what is wrong";
}

// A touch on the bar's pause shows at once, and the backend's 204 leaves it showing.
static void bar_press(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);
    host.answer_commands(204, "");

    host.tap(BAR_PLAY_PAUSE.x, BAR_PLAY_PAUSE.y);
}

// A touch on the bar's Next shows while the backend has not answered it.
static void bar_next(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);

    host.tap(BAR_NEXT.x, BAR_NEXT.y);
}

// A command the speaker refuses: the bar says it was not taken, and the playback fetched again no
// longer lists it.
static void bar_refused(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);
    host.answer_commands(409, ERROR_NOT_AVAILABLE);
    host.answer_next_playback(200, PLAYING_LAST);

    host.tap(BAR_NEXT.x, BAR_NEXT.y);
}

static const char* check_bar_refused(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_refused(host, shell)) {
        return problem;
    }
    if (!says(MSG_COMMAND_NOT_TAKEN)) {
        return "the bar does not say that the command was not taken";
    }

    return check_bar(host, shell);
}

// Once it has said so for a while, the bar says what plays again.
static const char* check_bar_refused_later(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_refused(host, shell)) {
        return problem;
    }
    if (says(MSG_COMMAND_NOT_TAKEN)) {
        return "the bar still says that the command was not taken";
    }
    if (!says("Svefn-g-englar")) {
        return "the bar does not say what plays again";
    }

    return check_bar(host, shell);
}

// What the bar said is said once: back on the now-playing screen it is not said again.
static void bar_refused_back(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);
    host.answer_commands(409, ERROR_NOT_AVAILABLE);
    host.answer_next_playback(200, PLAYING_LAST);

    host.tap(BAR_NEXT.x, BAR_NEXT.y);
    host.tap(FIRST_SPEAKER.x, FIRST_SPEAKER.y);
}

static const char* check_bar_refused_back(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_refused(host, shell)) {
        return problem;
    }
    if (says(MSG_COMMAND_NOT_TAKEN)) {
        return "the now-playing screen says again what the bar said";
    }

    return check_now_playing(host, shell);
}

// What the now-playing screen said is said once: the bar that shows next does not say it again.
static void bar_after_refused(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);
    host.answer_commands(409, ERROR_NOT_AVAILABLE);
    host.answer_next_playback(200, PLAYING_LAST);

    host.tap(NEXT.x, NEXT.y);
    host.tap(SPEAKER_BUTTON.x, SPEAKER_BUTTON.y);
}

static const char* check_bar_after_refused(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_refused(host, shell)) {
        return problem;
    }
    if (says(MSG_COMMAND_NOT_TAKEN)) {
        return "the bar says again what the now-playing screen said";
    }

    return check_bar(host, shell);
}

// A touch on the bar's up shows in the level at once, and the backend's answer leaves it showing.
static void bar_volume_up(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);
    host.answer_volume_changes(200, VOLUME_26);

    host.tap(BAR_VOLUME_UP.x, BAR_VOLUME_UP.y);
}

// A burst of presses on the bar: one change is on its way, and the press since waits.
static void bar_volume_burst(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);

    host.tap(BAR_VOLUME_UP.x, BAR_VOLUME_UP.y);
    host.tap(BAR_VOLUME_UP.x, BAR_VOLUME_UP.y);
}

// The group is muted: the level shows as not in effect.
static void bar_volume_muted(SimHost& host, Shell& shell) { open_bar(host, shell, PAUSED, SPEAKER_MUTED); }

// At the top of the range the bar's up is unavailable, and a touch on it sends nothing.
static void bar_volume_limit(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING, SPEAKER_LOUDEST);

    host.tap(BAR_VOLUME_UP.x, BAR_VOLUME_UP.y);
}

// A change the backend refuses: the bar says it was not taken, and the volume fetched again shows.
static void bar_volume_refused(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);
    host.answer_volume_changes(502, ERROR_SPEAKER);
    host.answer_next_volume(200, SPEAKER_MUTED);

    host.tap(BAR_VOLUME_DOWN.x, BAR_VOLUME_DOWN.y);
}

static const char* check_bar_volume_refused(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_volume_refused(host, shell)) {
        return problem;
    }

    return says(MSG_COMMAND_NOT_TAKEN) ? nullptr : "the bar does not say that the change was not taken";
}

// Another track while the bar shows: the bar's cover follows it, and the bar lets go of its cover
// when it hides.
static void bar_cover_change(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);

    host.answer_playback(200, PAUSED);
    shell.update();
    expect(host.is_artwork_shown(ARTWORK_PAUSED, BAR_COVER_SIZE) &&
               !host.is_artwork_shown(ARTWORK_PLAYING, BAR_COVER_SIZE),
           "the bar's cover did not follow the track");

    // Back on the now-playing screen, which was not kept current under the picker.
    shell.back();
    shell.update();
    expect(!check_now_playing(host, shell), "the now-playing screen did not come back with the track's cover");

    // The bar's cover is in the cache when it shows again.
    shell.open_speaker_picker();
}

static const char* check_bar_cover_change(const SimHost& host, const Shell& shell) {
    const std::vector<ArtworkFetch> fetches = {
        {ARTWORK_PLAYING, COVER_SIZE},
        {ARTWORK_PLAYING, BAR_COVER_SIZE},
        {ARTWORK_PAUSED, BAR_COVER_SIZE},
        {ARTWORK_PAUSED, COVER_SIZE},
    };
    if (host.get_artwork_fetches() != fetches) {
        return "the covers were not fetched as the track changed under the bar";
    }

    return check_bar(host, shell);
}

const std::vector<Scene>& get_scenes() {
    static const std::vector<Scene> scenes = {
        {"loading", loading, check_no_bar},
        {"loading-update", loading_update, check_no_bar},
        {"no-backend", no_backend, check_no_bar},
        {"speakers", speakers},
        {"speakers-none", speakers_none, check_held},
        {"speakers-missing", speakers_missing, check_missing},
        {"speakers-missing-unreachable", speakers_missing_unreachable, check_missing},
        {"speakers-pick", speakers_pick, check_pick},
        {"speakers-many", speakers_many},
        {"speakers-loading", speakers_loading},
        {"speakers-empty", speakers_empty, check_empty},
        {"speakers-unreachable", speakers_unreachable, check_notice_over_bar},
        {"speakers-error", speakers_error, check_notice_over_bar},
        {"speakers-unreadable", speakers_unreadable},
        {"speakers-retry", speakers_retry, check_retry},
        {"speakers-reconnecting", speakers_reconnecting},
        {"now-playing", now_playing, check_now_playing},
        {"now-playing-paused", now_playing_paused},
        {"now-playing-radio", now_playing_radio, check_radio},
        {"now-playing-nothing", now_playing_nothing},
        {"now-playing-press", now_playing_press, check_press},
        {"now-playing-next", now_playing_next, check_next},
        {"now-playing-refused", now_playing_refused, check_refused_said},
        {"now-playing-refused-later", now_playing_refused, check_refused_later, AFTER_NOTICE_MS},
        {"now-playing-loading", now_playing_loading, check_loading},
        {"now-playing-unreachable", now_playing_unreachable, check_now_playing},
        {"now-playing-error", now_playing_error},
        {"now-playing-unreadable", now_playing_unreadable},
        {"now-playing-retry", now_playing_retry, check_playback_retry},
        {"now-playing-reconnecting", now_playing_reconnecting},
        {"now-playing-speakers", now_playing_speakers, check_speakers_open},
        {"now-playing-same-speaker", now_playing_same_speaker, check_same_speaker},
        {"now-playing-other-speaker", now_playing_other_speaker, check_other_speaker},
        {"now-playing-volume-up", now_playing_volume_up, check_volume_up},
        {"now-playing-volume-burst", now_playing_volume_burst, check_volume_burst},
        {"now-playing-volume-muted", now_playing_volume_muted},
        {"now-playing-volume-limit", now_playing_volume_limit, check_volume_limit},
        {"now-playing-volume-refused", now_playing_volume_refused, check_volume_refused},
        {"now-playing-volume-unknown", now_playing_volume_unknown, check_volume_unknown},
        {"now-playing-volume-blind", now_playing_volume_blind, check_volume_blind},
        {"now-playing-cover-change", now_playing_cover_change, check_cover_change},
        {"now-playing-cover-refused", now_playing_cover_refused, check_cover_refused},
        {"now-playing-cover-wake", now_playing_cover_wake, check_cover_wake},
        {"bar", bar, check_bar},
        {"bar-paused", bar_paused, check_bar},
        {"bar-radio", bar_radio, check_bar_radio},
        {"bar-nothing", bar_nothing, check_nothing_sent},
        {"bar-loading", bar_loading, check_nothing_sent},
        {"bar-unreachable", bar_unreachable, check_bar_unreachable},
        {"bar-error", bar_error, check_bar_error},
        {"bar-press", bar_press, check_press},
        {"bar-next", bar_next, check_next},
        {"bar-refused", bar_refused, check_bar_refused},
        {"bar-refused-later", bar_refused, check_bar_refused_later, AFTER_NOTICE_MS},
        {"bar-refused-back", bar_refused_back, check_bar_refused_back},
        {"bar-after-refused", bar_after_refused, check_bar_after_refused},
        {"bar-volume-up", bar_volume_up, check_volume_up},
        {"bar-volume-burst", bar_volume_burst, check_volume_burst},
        {"bar-volume-muted", bar_volume_muted, check_bar},
        {"bar-volume-limit", bar_volume_limit, check_volume_limit},
        {"bar-volume-refused", bar_volume_refused, check_bar_volume_refused},
        {"bar-cover-change", bar_cover_change, check_bar_cover_change},
    };

    return scenes;
}
