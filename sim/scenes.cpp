#include "scenes.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "AlbumsView.h"
#include "ArtistsView.h"
#include "Controls.h"
#include "LoadingView.h"
#include "Messages.h"
#include "NowPlayingView.h"
#include "PlaylistsView.h"
#include "QueueView.h"
#include "SpeakerPickerView.h"
#include "Theme.h"
#include "TracksView.h"
#include "api/Answers.h"
#include "api/Artwork.h"
#include "api/Requests.h"
#include "cJSON.h"
#include "fonts/icons.h"

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

// The same speakers once the bathroom's has left the library's group and joined the garage's.
static const char SPEAKERS_REGROUPED[] = R"({"data":[
{"group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage","speaker-bathroom"],"muted":false,
 "volume":25},"id":"speaker-garage","model":"Play:3","muted":false,"name":"Garage","volume":24},
{"group":{"coordinator_id":"speaker-library","member_ids":["speaker-library"],"muted":false,"volume":35},
 "id":"speaker-library","model":"Playbase","muted":false,"name":"Bibliothèque","volume":35},
{"group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage","speaker-bathroom"],"muted":false,
 "volume":25},"id":"speaker-bathroom","model":"Play:1","muted":true,"name":"Żółta łazienka","volume":27}
]})";

// The same speakers, each playing alone.
static const char SPEAKERS_ALONE[] = R"({"data":[
{"group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage"],"muted":false,"volume":24},
 "id":"speaker-garage","model":"Play:3","muted":false,"name":"Garage","volume":24},
{"group":{"coordinator_id":"speaker-library","member_ids":["speaker-library"],"muted":false,"volume":35},
 "id":"speaker-library","model":"Playbase","muted":false,"name":"Bibliothèque","volume":35},
{"group":{"coordinator_id":"speaker-bathroom","member_ids":["speaker-bathroom"],"muted":true,"volume":27},
 "id":"speaker-bathroom","model":"Play:1","muted":true,"name":"Żółta łazienka","volume":27}
]})";

// The same speakers with the library's in the garage's group, which the garage's leads.
static const char SPEAKERS_GARAGE_LEADS[] = R"({"data":[
{"group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage","speaker-library"],"muted":false,
 "volume":29},"id":"speaker-garage","model":"Play:3","muted":false,"name":"Garage","volume":24},
{"group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage","speaker-library"],"muted":false,
 "volume":29},"id":"speaker-library","model":"Playbase","muted":false,"name":"Bibliothèque","volume":35},
{"group":{"coordinator_id":"speaker-bathroom","member_ids":["speaker-bathroom"],"muted":true,"volume":27},
 "id":"speaker-bathroom","model":"Play:1","muted":true,"name":"Żółta łazienka","volume":27}
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

// Names with characters the fonts draw (U+2010 the hyphen, U+02BC the modifier apostrophe, U+201E
// the low quote) and two they do not (U+266B and U+2192).
static const char SPEAKERS_GLYPHS[] = R"({"data":[
{"id":"speaker-garage","name":"Garage‐Werkbank","model":"Play:3","volume":24,"muted":false,
 "group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage"],"volume":24,"muted":false}},
{"id":"speaker-studio","name":"Studio ♫ʼ74 „B”","model":"One","volume":10,"muted":false,
 "group":{"coordinator_id":"speaker-studio","member_ids":["speaker-studio"],"volume":10,"muted":false}},
{"id":"speaker-shed","name":"Shed→","model":"Era 100","volume":12,"muted":false,
 "group":{"coordinator_id":"speaker-shed","member_ids":["speaker-shed"],"volume":12,"muted":false}}
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
    R"({"data":[{"id":"speaker-garage","name":"Garage","group":{"coordinator_id":"speaker-garage",)"
    R"("member_ids":["speaker-garage"]}},{"id":"speaker-library"}]})",
    R"({"data":[{"id":"speaker-garage","name":"Garage"}]})",
    R"({"data":[{"id":"speaker-garage","name":"Garage","group":null}]})",
    R"({"data":[{"id":"speaker-garage","name":"Garage","group":["speaker-garage"]}]})",
    R"({"data":[{"id":"speaker-garage","name":"Garage","group":{"member_ids":["speaker-garage"]}}]})",
    R"({"data":[{"id":"speaker-garage","name":"Garage","group":{"coordinator_id":null,)"
    R"("member_ids":["speaker-garage"]}}]})",
    R"({"data":[{"id":"speaker-garage","name":"Garage","group":{"coordinator_id":"speaker-garage"}}]})",
    R"({"data":[{"id":"speaker-garage","name":"Garage","group":{"coordinator_id":"speaker-garage",)"
    R"("member_ids":null}}]})",
    R"({"data":[{"id":"speaker-garage","name":"Garage","group":{"coordinator_id":"speaker-garage",)"
    R"("member_ids":"speaker-garage"}}]})",
    R"({"data":[{"id":"speaker-garage","name":"Garage","group":{"coordinator_id":"speaker-garage",)"
    R"("member_ids":["speaker-garage",7]}}]})",
    R"({"data":[{"id":"speaker-garage","name":"Garage","group":{"coordinator_id":"speaker-garage",)"
    R"("member_ids":["speaker-garage"]}},{"id":"speaker-library","name":"Bibliothèque","group":)"
    R"({"coordinator_id":"speaker-library","member_ids":[null]}}]})",
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

// Texts with the characters the library's titles hold beyond Latin-1 and Latin Extended-A, which
// the fonts draw: U+2010 the hyphen, U+201E the low quote, U+2032 and U+2033 the primes, U+02BC the
// modifier apostrophe, U+2082, U+1D62 and U+2C7C subscripts, U+2202, U+2206, U+2211, U+221A and
// U+2212 signs of maths, U+207B and U+2070 superscripts. And three characters they do not: U+2192,
// U+266B and U+6771.
static const char PLAYING_GLYPHS[] =
    R"({"actions":["play","pause","next","previous"],"queue_length":5,"queue_position":2,"shuffle":false,)"
    R"("source":"queue","state":"playing","track":{"album":"Signs 東∂ ∆ ∑ √ − 10⁻⁰",)"
    R"("artist":"Ensemble ʼ67 feat. x₂ᵢⱼ","artwork_id":"YXJ0d29yay9nbHlwaHM","duration_ms":451000,)"
    R"("title":"Rock‐a‐bye „Night” →7′30″♫","track_id":"3301"}})";

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

static const char SPEAKER_LOUDEST_MUTED[] =
    R"({"group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage"],"muted":true,"volume":100},)"
    R"("id":"speaker-garage","model":"Play:3","muted":true,"name":"Garage","volume":100})";

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

// Where the bar's cover, its texts, and the volume's level between down and up are; and a point of
// the texts above their lines.
static constexpr lv_point_t BAR_COVER = {48, 433};
static constexpr lv_point_t BAR_TEXTS = {226, 433};
static constexpr lv_point_t BAR_ABOVE_TEXTS = {226, 392};
static constexpr lv_point_t BAR_VOLUME_LEVEL = {664, 433};

// Where the speaker picker's first two rows are.
static constexpr lv_point_t FIRST_SPEAKER = {400, 163};
static constexpr lv_point_t SECOND_SPEAKER = {400, 263};

// Where the group controls of those rows are, and a point of the second row beside its control's
// circle: under it, as near as the row's edge.
static constexpr lv_point_t FIRST_GROUP_CONTROL = {709, 163};
static constexpr lv_point_t SECOND_GROUP_CONTROL = {709, 263};
static constexpr lv_point_t BESIDE_SECOND_GROUP_CONTROL = {709, 302};

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

// The labels on screen that say this.
static void find_labels(lv_obj_t* obj, const char* text, std::vector<lv_obj_t*>& found) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), text)) {
        found.push_back(obj);
    }

    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        find_labels(lv_obj_get_child(obj, int32_t(i)), text, found);
    }
}

// How many spinners are on screen.
static int count_spinners(const lv_obj_t* obj) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return 0;
    }

    int count = lv_obj_check_type(obj, &lv_spinner_class);
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        count += count_spinners(lv_obj_get_child(obj, int32_t(i)));
    }

    return count;
}

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
    expect(list.speakers[1] == Speaker{.id = "speaker-library",
                                       .name = "Bibliothèque",
                                       .member_ids = {"speaker-library", "speaker-bathroom"}},
           "the speakers fixture's second speaker was not read as it is");
}

// Which speakers play in the chosen speaker's group is read from the list: a group of two, from
// the speaker that leads it and from the other one.
static void expect_group_members() {
    SimHost grouped;
    const auto& model = grouped.get_model();

    grouped.answer_speakers(200, SPEAKERS);
    expect(!model.in_group("speaker-library"), "a speaker plays in a group while no speaker is chosen");

    grouped.model().speaker_id = "speaker-library";
    expect(model.in_group("speaker-bathroom") && model.in_group("speaker-library") && !model.in_group("speaker-garage"),
           "the group of two was not read from the speaker that leads it");

    grouped.model().speaker_id = "speaker-bathroom";
    expect(model.in_group("speaker-library") && !model.in_group("speaker-garage"),
           "the group of two was not read from the speaker that does not lead it");

    grouped.model().speaker_id = "speaker-garage";
    expect(!model.in_group("speaker-library") && !model.in_group("speaker-bathroom"),
           "a speaker that plays alone has others in its group");

    grouped.model().speaker_id = "speaker-shed";
    expect(!model.in_group("speaker-library"), "a speaker plays in the group of a speaker the list lacks");
}

// A change of a group is pending from the press, when its speaker takes no second one, until a
// list shows the group as asked or the time limit has passed. Any answer but a 204 is a change
// not taken.
static void expect_group_change_rules() {
    {
        SimHost group;
        group.model().speaker_id = "speaker-garage";
        group.answer_speakers(200, SPEAKERS);

        const auto& model = group.get_model();
        const auto& grouping = model.grouping;

        group.add_to_group("speaker-bathroom");
        expect(
            group.get_group_changes() == std::vector<GroupChangeRequest>{{"speaker-bathroom", "speaker-garage", true}},
            "an add did not send the speaker into the chosen speaker's group");
        expect(!grouping.allows("speaker-bathroom") && grouping.allows("speaker-library"),
               "a change on its way does not hold its own speaker, and no other");

        // A list from before the speakers reported the new group settles nothing.
        group.answer_speakers(200, SPEAKERS);
        expect(!grouping.allows("speaker-bathroom"), "a list that does not show a change settled it");

        // A pick of another speaker leaves the change pending. The bathroom plays in the library's
        // group, which is not the one it was sent into.
        group.choose_speaker("speaker-library");
        group.answer_speakers(200, SPEAKERS);
        expect(model.in_group("speaker-bathroom") && !grouping.allows("speaker-bathroom"),
               "a change was settled by another group than the one it was sent for");

        group.answer_speakers(200, SPEAKERS_REGROUPED);
        expect(grouping.allows("speaker-bathroom"), "a list that shows an add did not settle it");

        group.choose_speaker("speaker-garage");
        expect(model.in_group("speaker-bathroom"), "the list that settled an add does not show the group");

        // A remove the backend took waits for its list as well.
        group.answer_group_changes(204, "");
        group.remove_from_group("speaker-bathroom");
        expect(group.get_group_changes().size() == 2 &&
                   group.get_group_changes().back() == GroupChangeRequest{"speaker-bathroom", "speaker-garage", false},
               "a remove did not send the speaker out of the chosen speaker's group");

        const auto taken = grouping.find("speaker-bathroom");
        expect(taken && !taken->adds && taken->taken_at, "a change the backend took is not pending");

        group.answer_speakers(200, SPEAKERS_REGROUPED);
        expect(!grouping.allows("speaker-bathroom"), "a list that still shows the speaker settled its remove");

        // A list that cannot be read settles nothing.
        group.answer_speakers(500, ERROR_ENVELOPE);
        expect(!grouping.allows("speaker-bathroom"), "an error settled a change");

        group.answer_speakers(200, SPEAKERS);
        expect(grouping.allows("speaker-bathroom") && !model.in_group("speaker-bathroom"),
               "a list that shows a remove did not settle it");
        expect(model.not_taken() == 0, "a change that was settled counts as not taken");
    }

    // Anything but a 204 is a change not taken: it is counted, its speaker is held no longer, and
    // the speakers are fetched again.
    {
        std::string error;
        expect(!read_command(204, "", 0, error), "a 204 was not read as a change taken");
        expect(read_command(404, ERROR_SPEAKER_NOT_FOUND, strlen(ERROR_SPEAKER_NOT_FOUND), error) &&
                   error == "No speaker has the id speaker-garage",
               "a 404 was not read as a change refused");
        expect(read_command(400, ERROR_VALIDATION, strlen(ERROR_VALIDATION), error) && error.empty(),
               "a bare array was not read as a change refused");
        expect(read_command(200, "{}", 2, error), "a 200 was read as a change taken");

        SimHost refused;
        refused.model().speaker_id = "speaker-garage";
        refused.answer_speakers(200, SPEAKERS);
        refused.answer_group_changes(502, ERROR_SPEAKER);
        refused.answer_next_speakers(200, SPEAKERS_REGROUPED);

        const auto& model = refused.get_model();

        refused.add_to_group("speaker-library");
        expect(model.grouping.allows("speaker-library"), "a change refused is still pending");
        expect(model.grouping.changes_not_taken == 1 && model.not_taken() == 1,
               "a change refused does not count as not taken");
        expect(model.in_group("speaker-bathroom"), "a change refused did not fetch the speakers again");
    }

    // The time limit of a change the backend took runs from its answer, on LVGL's tick, which
    // wraps.
    {
        static constexpr uint32_t LIMIT = Grouping::TIME_LIMIT_MS;

        Grouping grouping;

        const auto first = grouping.sent("speaker-bathroom", "speaker-garage", true, 0);
        expect(!grouping.timed_out(ANSWER_TIME_LIMIT_MS - 1) && !grouping.allows("speaker-bathroom"),
               "a change on its way ran out of time before its answer's time limit");

        grouping.taken(first, 1000);
        expect(!grouping.timed_out(1000 + LIMIT - 1) && !grouping.allows("speaker-bathroom"),
               "a change was settled before its time limit");
        expect(grouping.timed_out(1000 + LIMIT) && grouping.allows("speaker-bathroom"),
               "the time limit did not settle a change");
        expect(!grouping.timed_out(1000 + LIMIT) && grouping.changes_not_taken == 0,
               "a change settled by the time limit is settled twice, or counts as not taken");

        const auto wrapped = grouping.sent("speaker-bathroom", "speaker-garage", false, UINT32_MAX - 99);
        grouping.taken(wrapped, UINT32_MAX - 99);
        expect(!grouping.timed_out(LIMIT - 101) && grouping.timed_out(LIMIT - 100),
               "the time limit does not hold across the tick's wrap");

        // The answer to a change that a list settled first is not the answer to a later change
        // of the same speaker.
        SpeakerList list;
        expect(!read_speakers(200, SPEAKERS_REGROUPED, strlen(SPEAKERS_REGROUPED), list),
               "the regrouped fixture was not read as a list");

        const auto settled = grouping.sent("speaker-bathroom", "speaker-garage", true, 0);
        grouping.listed(list);
        expect(grouping.allows("speaker-bathroom"), "a list did not settle a change on its way");

        const auto later = grouping.sent("speaker-bathroom", "speaker-garage", false, 0);
        grouping.taken(settled, 0);
        grouping.not_taken(settled);
        expect(!grouping.allows("speaker-bathroom") && !grouping.find("speaker-bathroom")->taken_at &&
                   grouping.changes_not_taken == 1,
               "the answer to a settled change was taken for a later change's");

        grouping.not_taken(later);
        expect(grouping.allows("speaker-bathroom") && grouping.changes_not_taken == 2,
               "a change that was not answered is still pending");
    }
}

static void speakers(SimHost& host, Shell& shell) {
    expect_group_members();
    expect_group_change_rules();

    host.model().speaker_id = "speaker-library";
    answer_speakers(host);

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));
}

static const char* check_held(const SimHost& host, const Shell& shell) {
    if (!shows<SpeakerPickerView>(shell)) {
        return "the display is not held on the speaker picker";
    }
    if (!says(MSG_SPEAKERS_PICK)) {
        return "the picker the display is held on does not say to pick a speaker";
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

// A speaker's name is read as a text of the backend's, the library's characters in it drawn and
// the ones the fonts do not draw left out; the bar shows a track with them too.
static void speakers_glyphs(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    host.answer_speakers(200, SPEAKERS_GLYPHS);

    const auto& speakers = host.get_model().speakers.speakers;
    expect(speakers.size() == 3 && speakers[0].name == "Garage‐Werkbank" && speakers[1].name == "Studio ʼ74 „B”" &&
               speakers[2].name == "Shed",
           "the names were not read as the fonts draw them");

    host.answer_playback(200, PLAYING_GLYPHS);
    host.answer_volume(200, SPEAKER);

    shell.open();
    shell.open_speaker_picker();
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

    // What the document allows is taken, whatever the display does not show holds.
    host.answer_speakers(200, R"({"data":[{"id":"speaker-garage","name":"Garage","model":7,"volume":"loud",)"
                              R"("group":{"coordinator_id":"speaker-garage","member_ids":["speaker-garage"],)"
                              R"("volume":null}}]})");
    expect(list.state == LoadState::Loaded && list.speakers.size() == 1,
           "a list was refused for what the display does not show");

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

// A change of a group that the backend took and no list shows: once its time limit has passed it
// is settled, and the speakers are fetched again.
static void speakers_group_time_limit(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    answer_speakers(host);

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));

    host.answer_group_changes(204, "");
    host.add_to_group("speaker-bathroom");
    expect(!host.get_model().grouping.allows("speaker-bathroom"), "a change the backend took is not pending");

    host.answer_next_speakers(200, SPEAKERS_REGROUPED);
}

static const char* check_group_time_limit(const SimHost& host, const Shell&) {
    const auto& model = host.get_model();

    if (!model.grouping.allows("speaker-bathroom")) {
        return "the time limit did not settle the change";
    }
    if (model.not_taken()) {
        return "a change settled by the time limit counts as not taken";
    }

    return model.in_group("speaker-bathroom") ? nullptr : "the speakers were not fetched again after the time limit";
}

// The picker over a list that has the chosen speaker: every other row carries a group control.
static void open_groups(SimHost& host, Shell& shell, const char* speakers, const char* chosen) {
    host.model().speaker_id = chosen;
    host.answer_speakers(200, speakers);
    expect(host.get_model().speakers.state == LoadState::Loaded && host.get_model().speakers.speakers.size() == 3,
           "the speakers fixture was not read as a list of three");

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));
}

// What the picker shows of the groups and what its group controls did: the speaker still chosen,
// how many rows are ticked, the chosen speaker's among them, how many show a change pending, and
// the changes sent.
static const char* check_groups(const SimHost& host, const Shell& shell, const char* chosen, size_t ticked, int pending,
                                const std::vector<GroupChangeRequest>& sent) {
    if (!shows<SpeakerPickerView>(shell)) {
        return "the display is not on the speaker picker";
    }
    if (host.get_model().speaker_id != chosen) {
        return "a press on a group control picked a speaker";
    }
    if (!says(MSG_SPEAKERS_PICK_OR_GROUP)) {
        return "the picker does not say what its group controls do";
    }

    std::vector<lv_obj_t*> ticks;
    find_labels(lv_screen_active(), ICON_CHECK, ticks);
    if (ticks.size() != ticked) {
        return "the rows do not tick the speakers of the chosen speaker's group, and no other";
    }
    if (count_spinners(lv_screen_active()) != pending) {
        return "the rows do not show the changes that are pending, and no other";
    }

    return host.get_group_changes() == sent ? nullptr : "the group controls did not send what was pressed, and no more";
}

// Every speaker plays alone: each row but the chosen speaker's offers to add its speaker. The
// chosen speaker's row carries no control: a touch where one would be sends nothing.
static void speakers_group_alone(SimHost& host, Shell& shell) {
    open_groups(host, shell, SPEAKERS_ALONE, "speaker-garage");

    host.tap(FIRST_GROUP_CONTROL.x, FIRST_GROUP_CONTROL.y);
}

static const char* check_group_alone(const SimHost& host, const Shell& shell) {
    return check_groups(host, shell, "speaker-garage", 1, 0, {});
}

// A group of two that the chosen speaker leads: the other speaker's row is ticked.
static void speakers_group_led(SimHost& host, Shell& shell) {
    open_groups(host, shell, SPEAKERS_GARAGE_LEADS, "speaker-garage");
}

static const char* check_group_led(const SimHost& host, const Shell& shell) {
    return check_groups(host, shell, "speaker-garage", 2, 0, {});
}

// A group of two that the other speaker leads: its row is ticked all the same.
static void speakers_group_follows(SimHost& host, Shell& shell) {
    open_groups(host, shell, SPEAKERS_GARAGE_LEADS, "speaker-library");
}

static const char* check_group_follows(const SimHost& host, const Shell& shell) {
    return check_groups(host, shell, "speaker-library", 2, 0, {});
}

// A touch on the control of a speaker outside the group adds it, and picks nothing. The change is
// on its way: the row shows it pending, and a second touch, beside the spinner, sends nothing and
// picks nothing.
static void speakers_group_add(SimHost& host, Shell& shell) {
    open_groups(host, shell, SPEAKERS_ALONE, "speaker-garage");

    host.tap(SECOND_GROUP_CONTROL.x, SECOND_GROUP_CONTROL.y);
    host.tap(BESIDE_SECOND_GROUP_CONTROL.x, BESIDE_SECOND_GROUP_CONTROL.y);
}

static const char* check_group_add(const SimHost& host, const Shell& shell) {
    return check_groups(host, shell, "speaker-garage", 1, 1, {{"speaker-library", "speaker-garage", true}});
}

// A touch on the control of a speaker of the group removes it. The backend took the change and no
// list shows it yet: the row shows it pending, and a second touch sends nothing.
static void speakers_group_remove(SimHost& host, Shell& shell) {
    open_groups(host, shell, SPEAKERS_GARAGE_LEADS, "speaker-garage");
    host.answer_group_changes(204, "");

    host.tap(SECOND_GROUP_CONTROL.x, SECOND_GROUP_CONTROL.y);
    host.tap(BESIDE_SECOND_GROUP_CONTROL.x, BESIDE_SECOND_GROUP_CONTROL.y);
}

static const char* check_group_remove(const SimHost& host, const Shell& shell) {
    const auto change = host.get_model().grouping.find("speaker-library");
    if (!change || !change->taken_at) {
        return "a remove the backend took is not pending";
    }

    return check_groups(host, shell, "speaker-garage", 1, 1, {{"speaker-library", "speaker-garage", false}});
}

// The speaker that leads the group is removed as any other: the change goes to it.
static void speakers_group_remove_leader(SimHost& host, Shell& shell) {
    open_groups(host, shell, SPEAKERS_GARAGE_LEADS, "speaker-library");

    host.tap(FIRST_GROUP_CONTROL.x, FIRST_GROUP_CONTROL.y);
}

static const char* check_group_remove_leader(const SimHost& host, const Shell& shell) {
    return check_groups(host, shell, "speaker-library", 1, 1, {{"speaker-garage", "speaker-library", false}});
}

// A change sent for the group of the speaker that was chosen then, pending still under another
// choice: its row shows it pending and takes no press.
static void speakers_group_pending_elsewhere(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-bathroom";
    host.answer_speakers(200, SPEAKERS_ALONE);
    host.add_to_group("speaker-garage");
    host.choose_speaker("speaker-library");

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));

    host.tap(FIRST_GROUP_CONTROL.x, FIRST_GROUP_CONTROL.y);
}

static const char* check_group_pending_elsewhere(const SimHost& host, const Shell& shell) {
    return check_groups(host, shell, "speaker-library", 1, 1, {{"speaker-garage", "speaker-bathroom", true}});
}

// The list of the picker, which its rows are in: found by a speaker's name.
static lv_obj_t* find_speaker_list(const char* name) {
    std::vector<lv_obj_t*> names;
    find_labels(lv_screen_active(), name, names);
    expect(names.size() == 1, "the picker does not list the speaker once");

    return lv_obj_get_parent(lv_obj_get_parent(names[0]));
}

static constexpr int32_t ONE_ROW = 100;

// A list scrolled one row down, so its second row is where the first was: a touch on that row's
// control adds its speaker, and the list stays where it was scrolled to.
static void speakers_group_scrolled(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-attic";
    host.answer_speakers(200, SPEAKERS_MANY);

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));

    lv_obj_scroll_to_y(find_speaker_list("Attic"), ONE_ROW, LV_ANIM_OFF);

    host.tap(FIRST_GROUP_CONTROL.x, FIRST_GROUP_CONTROL.y);
}

static const char* check_group_scrolled(const SimHost& host, const Shell& shell) {
    if (lv_obj_get_scroll_y(find_speaker_list("Attic")) != ONE_ROW) {
        return "the list did not stay where it was scrolled to";
    }

    return check_groups(host, shell, "speaker-attic", 1, 1, {{"speaker-garage", "speaker-attic", true}});
}

// A change of a group that is not answered is settled once ANSWER_TIME_LIMIT_MS have passed since
// it was sent, on LVGL's tick, which wraps, and counts as neither taken nor not taken. A change the
// backend took keeps its own limit from the answer. The answer to a settled change is not the
// answer to a later change of the same speaker.
static void expect_group_answer_time_limit_rules() {
    static constexpr uint32_t LIMIT = ANSWER_TIME_LIMIT_MS;

    Grouping grouping;

    const auto first = grouping.sent("speaker-bathroom", "speaker-garage", true, 1000);
    expect(!grouping.timed_out(1000 + LIMIT - 1) && grouping.is_pending(first),
           "a change on its way was settled before its time limit");
    expect(grouping.timed_out(1000 + LIMIT) && !grouping.is_pending(first) && grouping.allows("speaker-bathroom"),
           "the time limit did not settle a change on its way");
    expect(!grouping.timed_out(1000 + LIMIT) && grouping.changes_not_taken == 0,
           "a change settled by its time limit is settled twice, or counts as not taken");

    const auto later = grouping.sent("speaker-bathroom", "speaker-garage", false, 1000 + LIMIT);
    grouping.taken(first, 1000 + LIMIT);
    expect(grouping.is_pending(later) && !grouping.find("speaker-bathroom")->taken_at,
           "the answer to a change settled by its time limit was taken for a later change's");

    // Taken just before its time limit, a change waits for its list from the answer.
    grouping.taken(later, 1000 + 2 * LIMIT - 1);
    expect(!grouping.timed_out(1000 + 2 * LIMIT - 2 + Grouping::TIME_LIMIT_MS) && grouping.is_pending(later),
           "a change the backend took was settled by the time limit of a change on its way");
    expect(grouping.timed_out(1000 + 2 * LIMIT - 1 + Grouping::TIME_LIMIT_MS) && !grouping.is_pending(later),
           "a change the backend took was not settled by its time limit");

    const auto wrapped = grouping.sent("speaker-bathroom", "speaker-garage", true, UINT32_MAX - 99);
    expect(!grouping.timed_out(LIMIT - 101) && grouping.timed_out(LIMIT - 100) && !grouping.is_pending(wrapped),
           "the time limit of a change on its way does not hold across the tick's wrap");
}

// A change of a group that is never answered: once its time limit has passed it is settled without
// a notice, the speakers are fetched again, and the control takes a press again. The list fetched
// shows the speaker added after all, so the press removes it, and the row shows that pending.
static void speakers_group_no_answer(SimHost& host, Shell& shell) {
    expect_group_answer_time_limit_rules();

    open_groups(host, shell, SPEAKERS_ALONE, "speaker-garage");
    host.answer_next_speakers(200, SPEAKERS_GARAGE_LEADS);

    host.tap(SECOND_GROUP_CONTROL.x, SECOND_GROUP_CONTROL.y);
    host.wait(ANSWER_TIME_LIMIT_MS);
    host.tap(SECOND_GROUP_CONTROL.x, SECOND_GROUP_CONTROL.y);
}

static const char* check_group_no_answer(const SimHost& host, const Shell& shell) {
    if (host.get_model().not_taken()) {
        return "a change settled by its time limit counts as not taken";
    }

    return check_groups(host, shell, "speaker-garage", 1, 1,
                        {{"speaker-library", "speaker-garage", true}, {"speaker-library", "speaker-garage", false}});
}

// The answer to a change of a group comes after its time limit: the change is settled by then, and
// the answer, a refusal, changes nothing.
static void speakers_group_late_answer(SimHost& host, Shell& shell) {
    open_groups(host, shell, SPEAKERS_ALONE, "speaker-garage");
    host.answer_next_speakers(200, SPEAKERS_ALONE);

    host.tap(SECOND_GROUP_CONTROL.x, SECOND_GROUP_CONTROL.y);
    host.answer_after(ANSWER_TIME_LIMIT_MS + 500, [&host]() { host.answer_group_change(502, ERROR_SPEAKER); });
}

static const char* check_group_late_answer(const SimHost& host, const Shell& shell) {
    if (host.count_unanswered_group_changes()) {
        return "the change was not answered";
    }
    if (host.get_model().not_taken()) {
        return "a refusal that came after the time limit counts as not taken";
    }

    return check_groups(host, shell, "speaker-garage", 1, 0, {{"speaker-library", "speaker-garage", true}});
}

// While no speaker is chosen no row carries a group control: a touch where one would be picks
// the row's speaker.
static void speakers_none_pick(SimHost& host, Shell& shell) {
    answer_speakers(host);

    shell.open();

    host.tap(FIRST_GROUP_CONTROL.x, FIRST_GROUP_CONTROL.y);
}

static const char* check_none_pick(const SimHost& host, const Shell&) {
    if (!host.get_group_changes().empty()) {
        return "a row carries a group control while no speaker is chosen";
    }

    return host.get_model().speaker_id == "speaker-garage" ? nullptr : "the touch did not pick the first speaker";
}

// The same while the list lacks the chosen speaker.
static void speakers_missing_pick(SimHost& host, Shell& shell) {
    open_before_list(host, shell, "speaker-shed");

    answer_speakers(host);

    host.tap(SECOND_GROUP_CONTROL.x, SECOND_GROUP_CONTROL.y);
}

static const char* check_missing_pick(const SimHost& host, const Shell&) {
    if (!host.get_group_changes().empty()) {
        return "a row carries a group control while the list lacks the chosen speaker";
    }

    return host.get_model().speaker_id == "speaker-library" ? nullptr : "the touch did not pick the second speaker";
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

// The library's characters draw in the fonts, and a character they do not draw is left out where
// the text is read, as are bytes that are not UTF-8.
static void now_playing_glyphs(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING_GLYPHS);

    const auto& track = host.get_model().playback.track;
    expect(track && track->title == "Rock‐a‐bye „Night” 7′30″", "the title was not read as the fonts draw it");
    expect(track && track->artist == "Ensemble ʼ67 feat. x₂ᵢⱼ", "the artist was not read as the fonts draw it");
    expect(track && track->album == "Signs ∂ ∆ ∑ √ − 10⁻⁰", "the album was not read as the fonts draw it");

    // A stray continuation byte, a lead byte no sequence starts with, a lead byte the byte after it
    // does not continue, and a sequence cut short.
    const std::string broken = R"({"actions":[],"state":"stopped","track":{"album":"A)"
                               "\x80"
                               R"(B","artist":"C)"
                               "\xFF"
                               R"(D","artwork_id":null,"title":"E)"
                               "\xC3"
                               R"(F)"
                               "\xE2\x80"
                               R"("}})";
    Playback read;
    expect(!read_playback(200, broken.c_str(), broken.length(), read), "a playback with bytes not UTF-8 was refused");
    expect(read.track && read.track->album == "AB" && read.track->artist == "CD" && read.track->title == "EF",
           "bytes that are not UTF-8 were not left out");

    // A text is cut at a character, to 200 bytes: the first "é" ends at the 200th.
    const auto long_title = std::string(198, 'a') + "é";
    const auto long_body = std::string(R"({"actions":[],"state":"stopped","track":{"album":null,"artist":"F",)") +
                           R"("artwork_id":null,"title":")" + long_title + "é" + R"("}})";
    expect(!read_playback(200, long_body.c_str(), long_body.length(), read),
           "a playback with a long title was refused");
    expect(read.track && read.track->title == long_title, "a long title was not cut at a character to 200 bytes");
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
static constexpr uint32_t AFTER_NOTICE_MS = NOTICE_MS + 1000;

// A scene of this many touches runs this long: what the last one changed is drawn.
static constexpr uint32_t after_touches_ms(uint32_t touches) { return touches * SimHost::TAP_INTERVAL_MS + 300; }

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

// Whether the "Reconnecting" marker shows in the screen's top corner at this side.
static const char* check_marker(bool right) {
    lv_obj_update_layout(lv_screen_active());

    std::vector<lv_obj_t*> found;
    find_labels(lv_screen_active(), MSG_RECONNECTING, found);
    if (found.size() != 1) {
        return "the marker does not show";
    }

    lv_area_t area;
    lv_obj_get_coords(lv_obj_get_parent(found[0]), &area);
    if (right ? area.x2 != 800 - 1 - Theme::GAP : area.x1 != Theme::GAP) {
        return right ? "the marker is not at the top right" : "the marker is not at the top left";
    }

    return nullptr;
}

// Over the cover's corner, clear of the buttons at the top right.
static const char* check_now_playing_reconnecting(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_now_playing(host, shell)) {
        return problem;
    }

    return check_marker(false);
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
        const auto change = volume.next_change(0);
        expect(change && change->step == -3 && !volume.next_change(0), "a second change went while one was on its way");

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
    if (!volume.sending || volume.sending->step != Volume::STEP || volume.waiting != Volume::STEP) {
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

static const char UNMUTE_STEP_UP[] = R"({"delta":2,"muted":false})";
static const char UNMUTE_ALONE[] = R"({"delta":0,"muted":false})";
static const char STEP_UP[] = R"({"delta":2})";
static const char STEP_DOWN[] = R"({"delta":-2})";

// While the group shows muted, a press on up unmutes it with its step, in one change; down leaves
// the mute. The unmute is a change of its own: steps that come to none still send it. A change
// that is settled without an answer drops the unmute with its steps.
static void expect_unmute_rules() {
    using Bodies = std::vector<std::string>;

    // Up, and up again while the first change is on its way: one unmute, with the first change's
    // step; the step since goes without one.
    {
        SimHost host;
        host.answer_volume(200, SPEAKER_MUTED);

        host.change_volume(Volume::STEP);
        const auto& volume = host.get_model().volume;
        expect(host.get_volume_change_bodies() == Bodies{UNMUTE_STEP_UP}, "up on a muted group did not unmute it");
        expect(!volume.shown_muted() && volume.is_changing(), "the unmute does not show while it is on its way");

        // A volume fetched while the unmute is on its way is from before it.
        host.answer_volume(200, SPEAKER_MUTED);
        expect(!volume.shown_muted(), "a volume fetched during an unmute shows the group muted again");

        host.change_volume(Volume::STEP);
        host.answer_volume_change(200, R"({"muted":false,"volume":26})");
        expect(host.get_volume_change_bodies() == Bodies{UNMUTE_STEP_UP, STEP_UP},
               "a press on up after the unmute sent a second one");

        host.answer_volume_change(200, R"({"muted":false,"volume":28})");
        expect(!volume.is_changing() && !volume.muted && volume.level == 28, "the unmute did not end unmuted");
    }

    // Down while muted, then up and down before the down's answer: the steps come to none, and
    // the unmute goes alone.
    {
        SimHost host;
        host.answer_volume(200, SPEAKER_MUTED);

        host.change_volume(-Volume::STEP);
        host.change_volume(Volume::STEP);
        host.change_volume(-Volume::STEP);
        const auto& volume = host.get_model().volume;
        expect(host.get_volume_change_bodies() == Bodies{STEP_DOWN}, "down on a muted group sent an unmute");
        expect(!volume.shown_muted() && volume.waiting == 0, "the unmute pressed after a down does not show");

        host.answer_volume_change(200, R"({"muted":true,"volume":22})");
        expect(host.get_volume_change_bodies() == Bodies{STEP_DOWN, UNMUTE_ALONE},
               "an unmute whose steps came to none was not sent");
        expect(!volume.shown_muted(), "the unmute on its way does not show");
    }

    // Muted at the top of the range: up takes a press, which unmutes and steps none.
    {
        static const char MUTED_LOUDEST[] = R"({"muted":true,"volume":100})";
        Volume volume;
        std::string error;
        expect(!read_volume_change(200, MUTED_LOUDEST, strlen(MUTED_LOUDEST), volume, error) && volume.shown_muted(),
               "the answer to a change was not read");
        expect(volume.allows(Volume::STEP) && volume.allows(-Volume::STEP), "up is not live on a group muted at 100");

        volume.pressed(Volume::STEP);
        const auto change = volume.next_change(0);
        expect(change && change->step == 0 && change->unmutes, "up at 100 did not send the unmute alone");
        expect(!volume.allows(Volume::STEP) && volume.shown() == Volume::MAX_LEVEL,
               "up is live at 100 once the unmute is on its way");
    }

    // The unmute waiting behind a change goes with it when that change is not taken, or is
    // settled by its time limit; the group then shows muted again, and the late answer is not
    // read.
    {
        static const char MUTED[] = R"({"muted":true,"volume":24})";
        Volume volume;
        std::string error;
        expect(!read_volume_change(200, MUTED, strlen(MUTED), volume, error), "the answer to a change was not read");

        volume.pressed(-Volume::STEP);
        const auto down = volume.next_change(1000);
        volume.pressed(Volume::STEP);
        expect(down && !down->unmutes && volume.unmuting && !volume.shown_muted(), "the unmute does not wait");
        volume.change_not_taken();
        expect(!volume.unmuting && volume.shown_muted() && !volume.next_change(1000),
               "a change not taken left its unmute waiting");

        volume.pressed(Volume::STEP);
        const auto up = volume.next_change(2000);
        expect(up && up->unmutes, "up did not unmute");
        expect(volume.timed_out(2000 + ANSWER_TIME_LIMIT_MS) && volume.shown_muted() && !volume.is_changing() &&
                   !volume.is_sending(up->id),
               "the time limit did not settle an unmute");
    }

    // While the level is not known the mute is not shown, and up does not unmute.
    {
        Volume volume;
        volume.state = LoadState::Failed;
        volume.muted = true;

        volume.pressed(Volume::STEP);
        const auto change = volume.next_change(0);
        expect(change && !change->unmutes, "up unmuted a group whose level is not known");
    }
}

// A press on up while the group is muted: the change carries the unmute, and the level shows in
// effect while it is on its way.
static void now_playing_volume_unmute(SimHost& host, Shell& shell) {
    expect_unmute_rules();

    open_now_playing(host, shell, PAUSED, SPEAKER_MUTED);

    host.tap(VOLUME_UP.x, VOLUME_UP.y);
}

static const char* check_volume_unmute(const SimHost& host, const Shell&) {
    const auto& volume = host.get_model().volume;

    if (host.get_volume_change_bodies() != std::vector<std::string>{UNMUTE_STEP_UP}) {
        return "the touch did not send one step up with the unmute";
    }

    return !volume.shown_muted() && volume.shown() == 26 ? nullptr : "the unmute does not show at once";
}

// A group muted at 100: up takes one press, sent as a step of none with the unmute, and is not
// live once the unmute is on its way.
static void now_playing_volume_unmute_loudest(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PAUSED, SPEAKER_LOUDEST_MUTED);

    host.tap(VOLUME_UP.x, VOLUME_UP.y);
    host.tap(VOLUME_UP.x, VOLUME_UP.y);
}

static const char* check_volume_unmute_loudest(const SimHost& host, const Shell&) {
    const auto& volume = host.get_model().volume;

    if (host.get_volume_change_bodies() != std::vector<std::string>{UNMUTE_ALONE}) {
        return "the touches did not send one unmute without a step";
    }

    return !volume.shown_muted() && volume.shown() == Volume::MAX_LEVEL ? nullptr : "the unmute does not show at once";
}

// Down on a muted group leaves the mute.
static void now_playing_volume_muted_down(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PAUSED, SPEAKER_MUTED);
    host.answer_volume_changes(200, R"({"muted":true,"volume":22})");

    host.tap(VOLUME_DOWN.x, VOLUME_DOWN.y);
}

static const char* check_volume_muted_down(const SimHost& host, const Shell&) {
    const auto& volume = host.get_model().volume;

    if (host.get_volume_change_bodies() != std::vector<std::string>{STEP_DOWN}) {
        return "the touch did not send one step down without an unmute";
    }

    return volume.shown_muted() && volume.shown() == 22 ? nullptr : "down did not leave the group muted";
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
    const auto change = volume.next_change(0);
    expect(change && change->step == -Volume::STEP, "a press did not go");
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

// A change of the volume that is not answered is settled once ANSWER_TIME_LIMIT_MS have passed
// since it was sent, on LVGL's tick, which wraps: the steps pressed since are dropped with it, and
// it counts as neither taken nor not taken. Its id tells it from a later change, also one for
// another speaker.
static void expect_volume_answer_time_limit_rules() {
    static constexpr uint32_t LIMIT = ANSWER_TIME_LIMIT_MS;

    Volume volume;
    std::string error;
    expect(!read_volume_change(200, VOLUME_26, strlen(VOLUME_26), volume, error),
           "the answer to a change was not read");

    volume.pressed(Volume::STEP);
    const auto first = volume.next_change(1000);
    volume.pressed(Volume::STEP);
    expect(first && !volume.timed_out(1000 + LIMIT - 1) && volume.is_sending(first->id) && volume.shown() == 30,
           "a change on its way was settled before its time limit");
    expect(volume.timed_out(1000 + LIMIT) && !volume.is_changing() && volume.shown() == 26,
           "the time limit did not settle a change, or left the presses since waiting");
    expect(!volume.timed_out(1000 + LIMIT) && volume.changes_not_taken == 0,
           "a change settled by its time limit is settled twice, or counts as not taken");

    volume.pressed(Volume::STEP);
    const auto later = volume.next_change(1000 + LIMIT);
    expect(later && !volume.is_sending(first->id) && volume.is_sending(later->id),
           "the answer to a change settled by its time limit would be taken for a later change's");

    volume.speaker_changed();
    volume.pressed(Volume::STEP);
    const auto other = volume.next_change(UINT32_MAX - 99);
    expect(other && !volume.is_sending(first->id) && !volume.is_sending(later->id) && volume.is_sending(other->id),
           "the answer to a change for the speaker before would be taken for a later change's");
    expect(!volume.timed_out(LIMIT - 101) && volume.timed_out(LIMIT - 100),
           "the time limit of a change does not hold across the tick's wrap");
}

// A change of the volume that is never answered: once its time limit has passed it is settled
// without a notice, the level is fetched again, and a touch on up goes at once again.
static void now_playing_volume_no_answer(SimHost& host, Shell& shell) {
    expect_volume_answer_time_limit_rules();

    open_now_playing(host, shell, PLAYING);
    host.answer_next_volume(200, SPEAKER_GROUPED);

    host.tap(VOLUME_UP.x, VOLUME_UP.y);
    host.wait(ANSWER_TIME_LIMIT_MS);
    host.tap(VOLUME_UP.x, VOLUME_UP.y);
}

static const char* check_volume_no_answer(const SimHost& host, const Shell&) {
    const auto& volume = host.get_model().volume;

    if (host.get_volume_changes() != std::vector<int>{Volume::STEP, Volume::STEP}) {
        return "the touch after the time limit did not go at once";
    }
    if (volume.changes_not_taken) {
        return "a change settled by its time limit counts as not taken";
    }
    if (volume.level != 31) {
        return "the volume was not fetched again after the time limit";
    }

    return volume.shown() == 33 ? nullptr : "the level is not the one pressed to";
}

// The answer to a change of the volume comes after its time limit, while a later change is on its
// way: it changes nothing, and the later change still waits for its own.
static void now_playing_volume_late_answer(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);
    host.answer_next_volume(200, SPEAKER_GROUPED);

    host.tap(VOLUME_UP.x, VOLUME_UP.y);
    host.wait(ANSWER_TIME_LIMIT_MS);
    host.tap(VOLUME_UP.x, VOLUME_UP.y);
    host.answer_after(ANSWER_TIME_LIMIT_MS + 600, [&host]() { host.answer_volume_change(200, VOLUME_34); });
}

static const char* check_volume_late_answer(const SimHost& host, const Shell& shell) {
    if (host.count_unanswered_volume_changes() != 1) {
        return "the first change was not answered, or the second was";
    }
    if (!host.get_model().volume.sending) {
        return "the late answer settled the change on its way";
    }

    return check_volume_no_answer(host, shell);
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

// The bar's up on a muted group: the change carries the unmute.
static void bar_volume_unmute(SimHost& host, Shell& shell) {
    open_bar(host, shell, PAUSED, SPEAKER_MUTED);

    host.tap(BAR_VOLUME_UP.x, BAR_VOLUME_UP.y);
}

static const char* check_bar_volume_unmute(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_volume_unmute(host, shell)) {
        return problem;
    }

    return check_bar(host, shell);
}

// The bar's up on a group muted at 100: one press, sent as a step of none with the unmute.
static void bar_volume_unmute_loudest(SimHost& host, Shell& shell) {
    open_bar(host, shell, PAUSED, SPEAKER_LOUDEST_MUTED);

    host.tap(BAR_VOLUME_UP.x, BAR_VOLUME_UP.y);
    host.tap(BAR_VOLUME_UP.x, BAR_VOLUME_UP.y);
}

static const char* check_bar_volume_unmute_loudest(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_volume_unmute_loudest(host, shell)) {
        return problem;
    }

    return check_bar(host, shell);
}

// The bar's down on a muted group leaves the mute.
static void bar_volume_muted_down(SimHost& host, Shell& shell) {
    open_bar(host, shell, PAUSED, SPEAKER_MUTED);
    host.answer_volume_changes(200, R"({"muted":true,"volume":22})");

    host.tap(BAR_VOLUME_DOWN.x, BAR_VOLUME_DOWN.y);
}

static const char* check_bar_volume_muted_down(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_volume_muted_down(host, shell)) {
        return problem;
    }

    return check_bar(host, shell);
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

// A touch on the bar's cover, from the speakers screen: the screen closes, and the now-playing
// screen under it is the top view again.
static void bar_open_cover(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);

    host.tap(BAR_COVER.x, BAR_COVER.y);
}

static const char* check_bar_opened(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_now_playing(host, shell)) {
        return problem;
    }

    return shell.count_views() == 1 ? nullptr : "the views over the now-playing screen did not close";
}

// A touch on the bar's texts, from the speakers screen.
static void bar_open_texts(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);

    host.tap(BAR_TEXTS.x, BAR_TEXTS.y);
}

// The speakers screen whose list failed to load has its way back by the bar, which takes a touch
// above its texts' lines too.
static void bar_open_failed_list(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);
    host.answer_speakers(500, ERROR_ENVELOPE);
    shell.update();
    expect(says(MSG_TRY_AGAIN) && shell.shows_bar(), "the speakers screen does not show its failed list over the bar");

    host.tap(BAR_ABOVE_TEXTS.x, BAR_ABOVE_TEXTS.y);
}

// The now-playing screen is not the root, as it is not once a home screen is: with none in the
// stack a touch on the bar opens one on top, and with one under the top view it closes the views
// over it and none under it.
static void bar_open_not_root(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    answer_speakers(host);
    host.answer_playback(200, PLAYING);
    host.answer_volume(200, SPEAKER);

    shell.replace(std::make_unique<SpeakerPickerView>(&host, &shell));
    shell.update();
    expect(shell.shows_bar(), "the speakers screen at the root does not show the bar");

    host.tap(BAR_COVER.x, BAR_COVER.y);
    host.tap(SPEAKER_BUTTON.x, SPEAKER_BUTTON.y);
    host.tap(BAR_TEXTS.x, BAR_TEXTS.y);
}

static const char* check_bar_open_not_root(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_now_playing(host, shell)) {
        return problem;
    }

    return shell.count_views() == 2 ? nullptr : "the views were not closed down to the now-playing screen";
}

// The bar's controls, and the volume's level between down and up, open nothing: each does only
// what it does.
static void bar_controls(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);
    host.answer_commands(204, "");
    host.answer_volume_changes(200, VOLUME_26);

    host.tap(BAR_PLAY_PAUSE.x, BAR_PLAY_PAUSE.y);
    host.tap(BAR_NEXT.x, BAR_NEXT.y);
    host.tap(BAR_VOLUME_UP.x, BAR_VOLUME_UP.y);
    host.tap(BAR_VOLUME_DOWN.x, BAR_VOLUME_DOWN.y);
    host.tap(BAR_VOLUME_LEVEL.x, BAR_VOLUME_LEVEL.y);
}

static const char* check_bar_controls(const SimHost& host, const Shell& shell) {
    if (host.get_commands() != std::vector<Command>{Command::Pause, Command::Next}) {
        return "the bar's play or pause and next did not send their commands";
    }
    if (host.get_volume_changes() != std::vector<int>{Volume::STEP, -Volume::STEP}) {
        return "the bar's up and down did not send their steps";
    }
    if (shell.count_views() != 2) {
        return "a touch on the bar's controls changed the views";
    }

    return check_bar(host, shell);
}

// A change of a group that the backend refuses: the bar says it was not taken, and the row is
// pending no longer and shows the list as it is.
static void speakers_group_not_taken(SimHost& host, Shell& shell) {
    open_bar(host, shell, PLAYING);
    host.answer_group_changes(502, ERROR_SPEAKER);
    host.answer_next_speakers(200, SPEAKERS);

    host.tap(SECOND_GROUP_CONTROL.x, SECOND_GROUP_CONTROL.y);
}

static const char* check_group_refused(const SimHost& host, const Shell& shell) {
    if (host.get_model().not_taken() != 1) {
        return "the refused change does not count as one not taken";
    }
    if (!host.get_model().grouping.allows("speaker-library")) {
        return "the refused change is still pending";
    }

    return check_groups(host, shell, "speaker-garage", 1, 0, {{"speaker-library", "speaker-garage", true}});
}

static const char* check_group_not_taken(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_group_refused(host, shell)) {
        return problem;
    }
    if (!says(MSG_COMMAND_NOT_TAKEN)) {
        return "the bar does not say that the change was not taken";
    }

    return check_bar(host, shell);
}

// Once it has said so for a while, the bar says what plays again.
static const char* check_group_not_taken_later(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_group_refused(host, shell)) {
        return problem;
    }
    if (says(MSG_COMMAND_NOT_TAKEN)) {
        return "the bar still says that the change was not taken";
    }
    if (!says("Svefn-g-englar")) {
        return "the bar does not say what plays again";
    }

    return check_bar(host, shell);
}

// An artist of a fixture: its title, and the letter it files under.
using ArtistFixture = std::pair<std::string, char>;

// The library's artists in the order the backend gives them: by the title Plex sorts each by,
// without an accent, a leading "The" or case, those that file under '#' first. Every letter the
// live library has is here, 23 of them, with long and accented names, a U+2010 hyphen, and a
// character the fonts do not draw (U+266B).
static const std::vector<ArtistFixture> LIBRARY = {
    {"10 000 Maniacs", '#'},
    {"2Cellos", '#'},
    {"Anne Sofie von Otter, Les Musiciens du Louvre, Marc Minkowski & Chœur des Musiciens du Louvre", 'A'},
    {"Ásgeir", 'A'},
    {"The Beatles", 'B'},
    {"Björk", 'B'},
    {"Bløf", 'B'},
    {"Céline Dion", 'C'},
    {"Chœur de l’Opéra national de Paris", 'C'},
    {"Daft Punk", 'D'},
    {"Eivør", 'E'},
    {"Émilie Simon", 'E'},
    {"Fleetwood Mac", 'F'},
    {"Gåte", 'G'},
    {"Hania Rani", 'H'},
    {"Ibeyi", 'I'},
    {"Jay‐Z", 'J'},
    {"Jóhann Jóhannsson", 'J'},
    {"Kraftwerk", 'K'},
    {"Łona i Webber", 'L'},
    {"Lykke Li", 'L'},
    {"Mariza", 'M'},
    {"Mø", 'M'},
    {"Múm", 'M'},
    {"Nils Frahm", 'N'},
    {"Ólafur Arnalds", 'O'},
    {"Portishead", 'P'},
    {"Röyksopp♫", 'R'},
    {"Sigur Rós", 'S'},
    {"Stromae", 'S'},
    {"Tinariwen", 'T'},
    {"Väsen", 'V'},
    {"Yann Tiersen", 'Y'},
    {"Žagar", 'Z'},
};

// How many artists the library has in the scenes that page: three pages, the last one short.
static constexpr size_t MANY_ARTISTS = 250;
static constexpr size_t FOUR_PAGES_OF_ARTISTS = 350;

// Artists enough to page through, each named for its place in the list from 1, and filing under
// the letter its name starts with.
static std::vector<ArtistFixture> make_many_artists(size_t count = MANY_ARTISTS) {
    std::vector<ArtistFixture> artists;
    for (size_t i = 0; i < count; i++) {
        const auto letter = char('A' + i * 26 / count);

        char title[32];
        snprintf(title, sizeof(title), "%c Artist %03zu", letter, i + 1);
        artists.emplace_back(title, letter);
    }

    return artists;
}

// The answer to a list's GET for page `number`, PAGE_SIZE to a page, of a list of `total` items in
// the order `sort_by` and `sort_dir` name, its data `data`, in the shape the deployed backend gives
// it.
static std::string make_page_body(cJSON* data, uint32_t number, size_t total, const char* sort_by,
                                  const char* sort_dir) {
    const auto root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);

    const auto meta = cJSON_AddObjectToObject(root, "meta");
    cJSON_AddNumberToObject(meta, "page", number);
    cJSON_AddNumberToObject(meta, "per_page", PAGE_SIZE);
    cJSON_AddStringToObject(meta, "sort_by", sort_by);
    cJSON_AddStringToObject(meta, "sort_dir", sort_dir);
    cJSON_AddNumberToObject(meta, "total", double(total));
    cJSON_AddNumberToObject(meta, "total_pages", double((total + PAGE_SIZE - 1) / PAGE_SIZE));

    const auto text = cJSON_PrintUnformatted(root);
    std::string body(text);
    cJSON_free(text);
    cJSON_Delete(root);

    return body;
}

// The answer to GET /api/library/artists for page `number` of `artists`, its data the `count`
// artists from `first`. The ids are made up, and every third artist has no picture.
static std::string make_artists_body(const std::vector<ArtistFixture>& artists, size_t first, size_t count,
                                     uint32_t number) {
    const auto data = cJSON_CreateArray();
    for (auto i = first; i < first + count; i++) {
        const auto artist = cJSON_CreateObject();

        if (i % 3) {
            cJSON_AddStringToObject(artist, "artwork_id", ("YXJ0aXN0LyIi" + std::to_string(i)).c_str());
        } else {
            cJSON_AddNullToObject(artist, "artwork_id");
        }

        const char letter[] = {artists[i].second, 0};
        cJSON_AddStringToObject(artist, "id", ("artist-" + std::to_string(i + 1)).c_str());
        cJSON_AddStringToObject(artist, "index_letter", letter);
        cJSON_AddStringToObject(artist, "title", artists[i].first.c_str());
        cJSON_AddItemToArray(data, artist);
    }

    return make_page_body(data, number, artists.size(), "sort_title", "asc");
}

// Every page of `items`, as the backend answers each, its body made by `make_body`.
template <typename Fixture>
static std::vector<std::string> make_pages(const std::vector<Fixture>& items,
                                           std::string (*make_body)(const std::vector<Fixture>& items, size_t first,
                                                                    size_t count, uint32_t number)) {
    std::vector<std::string> pages;
    for (size_t first = 0; first < items.size() || pages.empty(); first += PAGE_SIZE) {
        pages.push_back(
            make_body(items, first, std::min(PAGE_SIZE, uint32_t(items.size() - first)), uint32_t(pages.size() + 1)));
    }

    return pages;
}

static std::vector<std::string> make_artist_pages(const std::vector<ArtistFixture>& artists) {
    return make_pages(artists, make_artists_body);
}

static const char ERROR_PLEX[] =
    R"({"code":null,"correlationId":null,"details":{"message":"The request could not be completed"},)"
    R"("error":"Plex did not answer within the time it is given, and the library could not be read"})";

// Pages of artists the API's document does not allow.
static const char* const UNREADABLE_ARTISTS[] = {
    "",
    "<html><body>502 Bad Gateway</body></html>",
    R"({"data":[)",
    "null",
    "[]",
    R"({"data":[]})",
    R"({"data":[],"meta":null})",
    R"({"data":[],"meta":{"per_page":100,"total":0}})",
    R"({"data":[],"meta":{"page":0,"per_page":100,"total":0}})",
    R"({"data":[],"meta":{"page":1.5,"per_page":100,"total":0}})",
    R"({"data":[],"meta":{"page":"1","per_page":100,"total":0}})",
    R"({"data":[],"meta":{"page":1,"total":0}})",
    R"({"data":[],"meta":{"page":1,"per_page":20,"total":0}})",
    R"({"data":[],"meta":{"page":1,"per_page":100}})",
    R"({"data":[],"meta":{"page":1,"per_page":100,"total":-1}})",
    R"({"data":[],"meta":{"page":1,"per_page":100,"total":1e30}})",
    R"({"meta":{"page":1,"per_page":100,"total":0}})",
    R"({"data":null,"meta":{"page":1,"per_page":100,"total":0}})",
    R"({"data":{"id":"artist-1","index_letter":"A","title":"ABBA"},"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"artist-1","index_letter":"A","title":"ABBA"}],"meta":{"page":1,"per_page":100,"total":0}})",
    R"({"data":[{"id":"artist-101","index_letter":"A","title":"ABBA"}],"meta":{"page":2,"per_page":100,)"
    R"("total":100}})",
    R"({"data":["artist-1"],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"index_letter":"A","title":"ABBA"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"","index_letter":"A","title":"ABBA"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":7,"index_letter":"A","title":"ABBA"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"artist-1","index_letter":"A"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"artist-1","index_letter":"A","title":null}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"artist-1","title":"ABBA"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"artist-1","index_letter":null,"title":"ABBA"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"artist-1","index_letter":"","title":"ABBA"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"artist-1","index_letter":"a","title":"ABBA"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"artist-1","index_letter":"AB","title":"ABBA"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"artist-1","index_letter":"Ä","title":"ÄBBA"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"artist-1","index_letter":"A","title":"ABBA"},{"id":"artist-2"}],)"
    R"("meta":{"page":1,"per_page":100,"total":2}})",
    R"({"data":[{"id":"artist-2","index_letter":"B","title":"Björk"},{"id":"artist-1","index_letter":"A",)"
    R"("title":"ABBA"}],"meta":{"page":1,"per_page":100,"total":2}})",
    R"({"data":[{"id":"artist-2","index_letter":"A","title":"ABBA"},{"id":"artist-1","index_letter":"#",)"
    R"("title":"10cc"}],"meta":{"page":1,"per_page":100,"total":2}})",
};

// Where the artists screen's back button and the button that opens its letters are, and the now
// playing screen's library button.
static constexpr lv_point_t BACK = {64, 48};
static constexpr lv_point_t JUMP = {728, 136};
static constexpr lv_point_t LIBRARY_BUTTON = {568, 56};

// Where the list's top is, on a screen with the bar.
static constexpr int32_t LIST_TOP = 96;

// The centre of the label that says this. A hidden one counts only `with_hidden`.
static std::optional<lv_point_t> find_label_center(lv_obj_t* obj, const char* text, bool with_hidden) {
    if (!with_hidden && lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return std::nullopt;
    }

    if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), text)) {
        lv_area_t area;
        lv_obj_get_coords(obj, &area);

        return lv_point_t{(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
    }

    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        if (const auto found = find_label_center(lv_obj_get_child(obj, int32_t(i)), text, with_hidden)) {
            return found;
        }
    }

    return std::nullopt;
}

// Touches the label that says this, where it is once the Shell has built the screen: one on
// screen, or, `with_hidden`, one that a touch before this one shows.
static void tap_label(SimHost& host, Shell& shell, const char* text, bool with_hidden = false) {
    shell.update();
    lv_obj_update_layout(lv_screen_active());

    const auto center = find_label_center(lv_screen_active(), text, with_hidden);
    expect(center.has_value(), "a label to touch is not on the screen");

    host.tap(center->x, center->y);
}

// The row of the queue's buttons that ends a row of an item, if it has one.
static lv_obj_t* find_queue_buttons(const lv_obj_t* row) {
    const auto last = lv_obj_get_child(row, -1);

    return last && lv_obj_check_type(last, &lv_obj_class) ? last : nullptr;
}

// Whether this object of a list is the row of an item, and not the state of a page past them: it
// holds labels, its title first, and may end in the queue's buttons.
static bool is_row(const lv_obj_t* obj) {
    if (!lv_obj_check_type(obj, &lv_obj_class) || !lv_obj_get_child_count(obj)) {
        return false;
    }

    const auto labels = lv_obj_get_child_count(obj) - (find_queue_buttons(obj) ? 1 : 0);
    if (!labels) {
        return false;
    }

    for (uint32_t i = 0; i < labels; i++) {
        if (!lv_obj_check_type(lv_obj_get_child(obj, int32_t(i)), &lv_label_class)) {
            return false;
        }
    }

    return true;
}

// The list on screen: what scrolls, and holds rows.
static lv_obj_t* find_list(lv_obj_t* obj) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return nullptr;
    }

    const auto count = lv_obj_get_child_count(obj);
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) && count && is_row(lv_obj_get_child(obj, 0))) {
        return obj;
    }
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) && count > 1 && is_row(lv_obj_get_child(obj, 1))) {
        return obj;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (const auto found = find_list(lv_obj_get_child(obj, int32_t(i)))) {
            return found;
        }
    }

    return nullptr;
}

static lv_obj_t* find_list() { return find_list(lv_screen_active()); }

static const char* get_row_title(const lv_obj_t* row) { return lv_label_get_text(lv_obj_get_child(row, 0)); }

// The rows of the list, in order.
static std::vector<lv_obj_t*> get_rows(lv_obj_t* list) {
    std::vector<lv_obj_t*> rows;
    for (uint32_t i = 0; i < lv_obj_get_child_count(list); i++) {
        const auto child = lv_obj_get_child(list, int32_t(i));
        if (is_row(child)) {
            rows.push_back(child);
        }
    }

    return rows;
}

// The first row the list shows from its top, in part or whole.
static lv_obj_t* get_top_row(lv_obj_t* list) {
    lv_area_t shown;
    lv_obj_get_coords(list, &shown);

    for (const auto row : get_rows(list)) {
        lv_area_t area;
        lv_obj_get_coords(row, &area);
        if (area.y2 >= shown.y1) {
            return row;
        }
    }

    return nullptr;
}

// Where a row is on screen: its title, and its top.
struct RowPlace {
    std::string title;
    int32_t top;
};

static RowPlace get_place(const lv_obj_t* row) {
    lv_area_t area;
    lv_obj_get_coords(row, &area);

    return RowPlace{get_row_title(row), area.y1};
}

// Scrolls the list on screen to its end, or to its start, and has the Shell build what that brings
// of the backend's answers. Returns where the row at the top was before they came.
static RowPlace scroll_list(Shell& shell, bool to_end) {
    lv_obj_update_layout(lv_screen_active());
    const auto list = find_list();
    expect(list, "no list is on screen");

    lv_obj_scroll_to_y(list, to_end ? LV_COORD_MAX : 0, LV_ANIM_OFF);
    const auto top = get_place(get_top_row(list));

    shell.update();
    lv_obj_update_layout(lv_screen_active());

    return top;
}

static void open_artists(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);

    shell.open_library();
    expect(shell.shows_bar(), "the artists screen does not show the bar");
}

static void open_library(SimHost& host, Shell& shell, const std::vector<ArtistFixture>& artists) {
    host.artists().serve(200, make_artist_pages(artists));

    open_artists(host, shell);
}

static const char* check_artists_screen(const Shell& shell) {
    if (!shows<ArtistsView>(shell)) {
        return "the display is not on the artists screen";
    }

    return shell.shows_bar() ? nullptr : "the artists screen does not show the bar";
}

// The row of the artist with this title, on screen.
static lv_obj_t* find_row(const char* title) {
    std::vector<lv_obj_t*> found;
    find_labels(lv_screen_active(), title, found);

    return found.size() == 1 ? lv_obj_get_parent(found[0]) : nullptr;
}

// Whether a row's label shows this title: whole, or, where it is longer than the row, its start
// and the dots LVGL writes over its end.
static bool shows_title(const char* label, const std::string& title) {
    const std::string shown = label;
    if (shown == title) {
        return true;
    }

    return shown.ends_with("...") && title.starts_with(shown.substr(0, shown.length() - 3));
}

// The rows of the list have the titles of `titles` from `first`, `count` of them, in order.
static const char* check_titles(const std::vector<std::string>& titles, size_t first, size_t count) {
    const auto list = find_list();
    if (!list) {
        return "the items are not listed";
    }

    const auto rows = get_rows(list);
    if (rows.size() != count) {
        return "the list does not have a row for every item held";
    }

    for (size_t i = 0; i < count; i++) {
        // What the fonts do not draw is left out of a title.
        auto title = titles[first + i];
        if (const auto at = title.find("♫"); at != std::string::npos) {
            title.erase(at);
        }

        if (!shows_title(get_row_title(rows[i]), title)) {
            return "the rows are not the items, in the backend's order";
        }
    }

    return nullptr;
}

// The rows of the list are those of `artists` from `first`, `count` of them.
static const char* check_rows(const std::vector<ArtistFixture>& artists, size_t first, size_t count) {
    std::vector<std::string> titles;
    for (const auto& artist : artists) {
        titles.push_back(artist.first);
    }

    return check_titles(titles, first, count);
}

// The row that was at the top before the list was built again is where it was.
static void expect_in_place(const RowPlace& place, const char* what) {
    const auto row = find_row(place.title.c_str());
    expect(row && get_place(row).top == place.top, what);
}

// The now-playing screen's library button opens the artists, over it, with the bar under them.
static void library_open(SimHost& host, Shell& shell) {
    host.artists().serve(200, make_artist_pages(LIBRARY));
    open_now_playing(host, shell, PLAYING);

    host.tap(LIBRARY_BUTTON.x, LIBRARY_BUTTON.y);
}

static const char* check_library_open(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (shell.count_views() != 2) {
        return "the artists screen did not open over the now-playing screen";
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1}) {
        return "the artists were not asked for once, from the first page";
    }

    return check_rows(LIBRARY, 0, LIBRARY.size());
}

// The artists as the backend sorts them, '#' first, with long and accented names.
static void artists(SimHost& host, Shell& shell) {
    Page<Artist> page;
    const auto body = make_artist_pages(LIBRARY)[0];
    expect(!read_artists(200, body.c_str(), body.length(), page), "the artists fixture was not read as a page");
    expect(page.state == LoadState::Loaded && page.number == 1 && page.total == LIBRARY.size() &&
               page.items.size() == LIBRARY.size(),
           "the artists fixture's page was not read as it is");
    expect(page.items[0] == Artist{.id = "artist-1", .title = "10 000 Maniacs", .index_letter = '#'} &&
               page.items[27] == Artist{.id = "artist-28", .title = "Röyksopp", .index_letter = 'R'},
           "the artists fixture's artists were not read as they are");

    open_library(host, shell, LIBRARY);
}

static const char* check_artists(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1}) {
        return "the artists were not asked for once, from the first page";
    }

    return check_rows(LIBRARY, 0, LIBRARY.size());
}

// The button beside the list opens the letters the artists file under, in order.
static void artists_letters(SimHost& host, Shell& shell) {
    open_library(host, shell, LIBRARY);

    host.tap(JUMP.x, JUMP.y);
}

static const char* check_artists_letters(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists(host, shell)) {
        return problem;
    }

    std::vector<lv_obj_t*> found;
    find_labels(lv_screen_active(), "#", found);
    if (found.size() != 1) {
        return "the letters do not show";
    }

    const auto grid = lv_obj_get_parent(lv_obj_get_parent(found[0]));
    const std::string letters = "#ABCDEFGHIJKLMNOPRSTVYZ";
    if (lv_obj_get_child_count(grid) != letters.size()) {
        return "the letters are not those the artists file under";
    }

    for (uint32_t i = 0; i < lv_obj_get_child_count(grid); i++) {
        const auto button = lv_obj_get_child(grid, int32_t(i));
        if (lv_label_get_text(lv_obj_get_child(button, 0)) != letters.substr(i, 1)) {
            return "the letters are not in order";
        }
        if (lv_obj_get_width(button) < 64 || lv_obj_get_height(button) < 64) {
            return "a letter is smaller than 64 px";
        }
    }

    return nullptr;
}

// A press on a letter brings its first artist to the top of the list.
static void artists_jump(SimHost& host, Shell& shell) {
    open_library(host, shell, LIBRARY);

    host.tap(JUMP.x, JUMP.y);
    tap_label(host, shell, "M", true);
}

static const char* check_artists_jump(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists(host, shell)) {
        return problem;
    }

    const auto row = find_row("Mariza");
    if (!row) {
        return "the letter's first artist is not listed";
    }

    lv_area_t area;
    lv_obj_get_coords(row, &area);
    if (area.y1 != LIST_TOP) {
        return "the letter's first artist is not at the top of the list";
    }

    return says("#") ? "the letters still show" : nullptr;
}

// A letter near the end, whose artists fill less than a screen: the list scrolls to its end, with
// the letter's first artist in view.
static void artists_jump_end(SimHost& host, Shell& shell) {
    open_library(host, shell, LIBRARY);

    host.tap(JUMP.x, JUMP.y);
    tap_label(host, shell, "Y", true);
}

static const char* check_artists_jump_end(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists(host, shell)) {
        return problem;
    }

    const auto list = find_list();
    if (lv_obj_get_scroll_bottom(list) != 0) {
        return "the list is not at its end";
    }

    const auto row = find_row("Yann Tiersen");
    lv_area_t area;
    lv_area_t shown;
    lv_obj_get_coords(row, &area);
    lv_obj_get_coords(list, &shown);
    if (area.y1 < shown.y1 || area.y2 > shown.y2) {
        return "the letter's first artist is not in view";
    }

    return says("#") ? "the letters still show" : nullptr;
}

// The artists are on their way.
static void artists_loading(SimHost& host, Shell& shell) { open_artists(host, shell); }

static const char* check_artists_loading(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (!says(MSG_ARTISTS_LOADING) || says(MSG_JUMP_TO_LETTER)) {
        return "the screen does not say that the artists are on their way";
    }

    return host.artists().get_requests() == std::vector<uint32_t>{1} ? nullptr : "the artists were not asked for";
}

static void artists_unreachable(SimHost& host, Shell& shell) {
    host.artists().serve(SimHost::UNREACHABLE, {""});

    open_artists(host, shell);
}

static const char* check_artists_unreachable(const SimHost&, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }

    return says(MSG_BACKEND_UNREACHABLE) && says(MSG_TRY_AGAIN) ? nullptr
                                                                : "the screen does not say why, with a way to retry";
}

// Plex failed: the backend's own words, cut to two lines.
static void artists_error(SimHost& host, Shell& shell) {
    host.artists().serve(502, {ERROR_PLEX});

    open_artists(host, shell);
}

static const char* check_artists_error(const SimHost&, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }

    return says(MSG_BACKEND_FAILED) && says(MSG_TRY_AGAIN) ? nullptr
                                                           : "the screen does not say why, with a way to retry";
}

// Every page the API's document does not allow is refused whole; what it allows is taken whatever
// the display does not show holds.
static void artists_unreadable(SimHost& host, Shell& shell) {
    Page<Artist> page;

    const auto allowed =
        R"({"data":[{"artwork_id":7,"id":"artist-1","index_letter":"A","title":"ABBA","country":"SE"}],)"
        R"("meta":{"page":1,"per_page":100,"sort_by":null,"total":1}})";
    expect(!read_artists(200, allowed, strlen(allowed), page) && page.items.size() == 1,
           "a page was refused for what the display does not show");

    for (const auto body : UNREADABLE_ARTISTS) {
        page = {};
        expect(read_artists(200, body, strlen(body), page) && page.state == LoadState::Failed && page.items.empty() &&
                   page.error.empty(),
               "an unreadable page was not refused");

        page = {};
        expect(read_artists(502, body, strlen(body), page) && page.state == LoadState::Failed && page.error.empty(),
               "an unreadable error body gave a message");
    }

    // A page of more items than PAGE_SIZE.
    const auto many = make_many_artists();
    const auto long_page = make_artists_body(many, 0, PAGE_SIZE + 1, 1);
    page = {};
    expect(read_artists(200, long_page.c_str(), long_page.length(), page) && page.state == LoadState::Failed,
           "a page longer than PAGE_SIZE was not refused");

    host.artists().serve(200, {UNREADABLE_ARTISTS[std::size(UNREADABLE_ARTISTS) - 1]});
    open_artists(host, shell);
}

static void artists_empty(SimHost& host, Shell& shell) { open_library(host, shell, {}); }

static const char* check_artists_empty(const SimHost&, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (says(MSG_JUMP_TO_LETTER)) {
        return "the screen offers letters of an empty library";
    }

    return says(MSG_ARTISTS_EMPTY) ? nullptr : "the screen does not say that the library has no artists";
}

// Try again, once the backend answers.
static void artists_retry(SimHost& host, Shell& shell) {
    host.artists().serve(SimHost::UNREACHABLE, {""});
    open_artists(host, shell);

    host.artists().serve(200, make_artist_pages(LIBRARY));
    tap_label(host, shell, MSG_TRY_AGAIN);
}

static const char* check_artists_retry(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1, 1}) {
        return "the first page was not asked for again";
    }

    return check_rows(LIBRARY, 0, LIBRARY.size());
}

// Scrolled to its end, the list asks for the next page, and with the third the first goes: at most
// 200 rows are built, and the row at the top stays there as the rows change.
static void artists_pages(SimHost& host, Shell& shell) {
    const auto artists = make_many_artists();
    open_library(host, shell, artists);
    expect(!check_rows(artists, 0, 100), "the first page is not listed");

    scroll_list(shell, true);
    expect(!check_rows(artists, 0, 200), "the second page is not listed after the first");

    expect_in_place(scroll_list(shell, true), "the row at the top moved as the third page came");
}

static const char* check_artists_pages(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1, 2, 3}) {
        return "the pages were not asked for one by one as the list was scrolled";
    }

    return check_rows(make_many_artists(), 100, 150);
}

// Back at the start, the first page is asked for again, and the third goes.
static void artists_pages_back(SimHost& host, Shell& shell) {
    artists_pages(host, shell);

    expect_in_place(scroll_list(shell, false), "the row at the top moved as the first page came");
}

static const char* check_artists_pages_back(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1, 2, 3, 1}) {
        return "the first page was not asked for again at the start";
    }

    return check_rows(make_many_artists(), 0, 200);
}

// The next page does not come: the list says so at its end, with a way to ask again.
static void artists_page_failed(SimHost& host, Shell& shell) {
    const auto artists = make_many_artists();
    host.artists().serve(200, {make_artist_pages(artists)[0]});
    open_artists(host, shell);

    scroll_list(shell, true);
    expect(host.artists().get_requests() == std::vector<uint32_t>{1, 2}, "the second page was not asked for");

    host.artists().answer(SimHost::UNREACHABLE, "");
}

static const char* check_artists_page_failed(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (!says(MSG_BACKEND_UNREACHABLE) || !says(MSG_TRY_AGAIN)) {
        return "the list does not say at its end why the page did not come";
    }
    if (lv_obj_get_scroll_bottom(find_list()) != 0) {
        return "the list is not at its end";
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1, 2}) {
        return "a page that did not come was asked for again without a touch";
    }

    return check_rows(make_many_artists(), 0, 100);
}

// Try again at the list's end, once the backend answers.
static void artists_page_retry(SimHost& host, Shell& shell) {
    artists_page_failed(host, shell);

    host.artists().serve(200, make_artist_pages(make_many_artists()));
    tap_label(host, shell, MSG_TRY_AGAIN);
}

static const char* check_artists_page_retry(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1, 2, 2}) {
        return "the second page was not asked for again";
    }

    return check_rows(make_many_artists(), 0, 200);
}

// The time from the scene's start at which the third page comes, after two touches.
static constexpr uint32_t LATE_PAGE_MS = after_touches_ms(2);

// At the end of the two pages held, the list asks for the third; a jump to the first letter comes
// before it does. The page that comes would drop the rows on screen, so it goes instead.
static void artists_page_late(SimHost& host, Shell& shell) {
    const auto pages = make_artist_pages(make_many_artists());
    host.artists().serve(200, {pages[0], pages[1]});
    open_artists(host, shell);
    scroll_list(shell, true);
    scroll_list(shell, true);

    host.tap(JUMP.x, JUMP.y);
    tap_label(host, shell, "A", true);
    host.answer_after(LATE_PAGE_MS, [&host, page = pages[2]]() { host.artists().answer(200, page.c_str()); });
}

static const char* check_artists_page_late(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1, 2, 3}) {
        return "the third page was not asked for at the end";
    }

    const auto row = find_row("A Artist 001");
    if (!row || get_place(row).top != LIST_TOP) {
        return "the first artist is not at the top of the list as the third page came";
    }

    return check_rows(make_many_artists(), 0, 200);
}

// The list goes to its start before the third page comes, then to its end again: the page that
// went is asked for again, and comes.
static void artists_page_late_again(SimHost& host, Shell& shell) {
    const auto artists = make_many_artists();
    const auto pages = make_artist_pages(artists);
    host.artists().serve(200, {pages[0], pages[1]});
    open_artists(host, shell);
    scroll_list(shell, true);
    scroll_list(shell, true);

    scroll_list(shell, false);
    host.artists().answer(200, pages[2].c_str());
    shell.update();
    expect(!check_rows(artists, 0, 200), "the third page took the place of the first, which is on screen");

    host.artists().serve(200, pages);
    scroll_list(shell, true);
}

static const char* check_artists_page_late_again(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1, 2, 3, 3}) {
        return "the third page was not asked for again at the end";
    }

    return check_rows(make_many_artists(), 100, 150);
}

// Scrolls the list on screen to its end, or to its start, as a drag does before the Shell builds
// what the backend's answers brought.
static void drag_list(bool to_end) {
    lv_obj_update_layout(lv_screen_active());
    const auto list = find_list();
    expect(list, "no list is on screen");

    lv_obj_scroll_to_y(list, to_end ? LV_COORD_MAX : 0, LV_ANIM_OFF);
}

// The 199th of the 250: at the top, the rows on screen run past it into the third page.
static constexpr const char* ROW_BEFORE_THIRD_PAGE = "U Artist 199";

// Back at the start of the second and third pages, the list asks for the first; before it comes,
// the list moves to where the rows on screen run into the third. The page that comes would drop
// them, so it goes instead.
static void artists_page_late_start(SimHost& host, Shell& shell) {
    artists_pages(host, shell);
    host.artists().serve(200, {});

    scroll_list(shell, false);
    lv_obj_update_layout(lv_screen_active());
    const auto row = find_row(ROW_BEFORE_THIRD_PAGE);
    expect(row, "the row before the third page is not listed");
    lv_obj_scroll_to_y(find_list(), lv_obj_get_y(row), LV_ANIM_OFF);

    host.artists().answer(200, make_artist_pages(make_many_artists())[0].c_str());
    shell.update();
}

static const char* check_artists_page_late_start(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1, 2, 3, 1}) {
        return "the first page was not asked for again at the start";
    }

    const auto row = find_row(ROW_BEFORE_THIRD_PAGE);
    if (!row || get_place(row).top != LIST_TOP) {
        return "the row at the top moved as the first page came";
    }

    return check_rows(make_many_artists(), 100, 150);
}

// The list goes from the end of the pages held to their start while the page after them is on its
// way, which goes as it comes. The page before them is then asked for, with no scroll to ask.
static void artists_page_late_ask(SimHost& host, Shell& shell) {
    const auto artists = make_many_artists(FOUR_PAGES_OF_ARTISTS);
    const auto pages = make_artist_pages(artists);
    host.artists().serve(200, {pages[0], pages[1], pages[2]});
    open_artists(host, shell);
    scroll_list(shell, true);
    scroll_list(shell, true);
    scroll_list(shell, true);

    const auto top = scroll_list(shell, false);
    host.artists().answer(200, pages[3].c_str());
    shell.update();
    shell.update();
    lv_obj_update_layout(lv_screen_active());

    expect_in_place(top, "the row at the top moved as the pages came");
}

static const char* check_artists_page_late_ask(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1, 2, 3, 4, 1}) {
        return "the first page was not asked for at the start once the fourth went";
    }

    return check_rows(make_many_artists(FOUR_PAGES_OF_ARTISTS), 0, 200);
}

// The third page comes, and the list goes to its end before it is built with it: the page after
// the third is not asked for until it is, so no more than two pages are built.
static void artists_page_unbuilt(SimHost& host, Shell& shell) {
    const auto pages = make_artist_pages(make_many_artists(FOUR_PAGES_OF_ARTISTS));
    host.artists().serve(200, {pages[0], pages[1]});
    open_artists(host, shell);
    scroll_list(shell, true);
    scroll_list(shell, true);

    host.artists().answer(200, pages[2].c_str());
    host.artists().serve(200, pages);
    drag_list(false);
    drag_list(true);
    shell.update();
}

static const char* check_artists_page_unbuilt(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists_screen(shell)) {
        return problem;
    }
    if (host.artists().get_requests() != std::vector<uint32_t>{1, 2, 3}) {
        return "a page was asked for past one the list was not built with";
    }

    return check_rows(make_many_artists(FOUR_PAGES_OF_ARTISTS), 100, 200);
}

// The back button closes the artists screen.
static void artists_back(SimHost& host, Shell& shell) {
    open_library(host, shell, LIBRARY);

    host.tap(BACK.x, BACK.y);
}

static const char* check_artists_back(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_now_playing(host, shell)) {
        return problem;
    }

    return shell.count_views() == 1 ? nullptr : "the artists screen did not close";
}

// What the now-playing screen said is not said again once a view over it closes, however soon.
static void now_playing_refused_back(SimHost& host, Shell& shell) {
    host.artists().serve(200, make_artist_pages(LIBRARY));
    open_now_playing(host, shell, PLAYING);
    host.answer_commands(409, ERROR_NOT_AVAILABLE);
    host.answer_next_playback(200, PLAYING_LAST);

    host.tap(NEXT.x, NEXT.y);
    host.tap(LIBRARY_BUTTON.x, LIBRARY_BUTTON.y);
    host.tap(BACK.x, BACK.y);
}

static const char* check_now_playing_refused_back(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_refused(host, shell)) {
        return problem;
    }
    if (shell.count_views() != 1) {
        return "the artists screen did not close";
    }
    if (says(MSG_COMMAND_NOT_TAKEN)) {
        return "the now-playing screen still says what it said before the artists screen opened over it";
    }

    return check_now_playing(host, shell);
}

// A touch on the bar's cover, or on its texts, from the artists screen.
static void artists_bar_cover(SimHost& host, Shell& shell) {
    open_library(host, shell, LIBRARY);

    host.tap(BAR_COVER.x, BAR_COVER.y);
}

static void artists_bar_texts(SimHost& host, Shell& shell) {
    open_library(host, shell, LIBRARY);

    host.tap(BAR_TEXTS.x, BAR_TEXTS.y);
}

// The "Reconnecting" marker at the heading's right, clear of the button beside the list.
static void artists_reconnecting(SimHost& host, Shell& shell) {
    host.model().stream_state = StreamState::Disconnected;
    host.model().stream_down_since = lv_tick_get() - 10 * 1000;

    open_library(host, shell, LIBRARY);
}

static const char* check_artists_reconnecting(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_artists(host, shell)) {
        return problem;
    }

    return check_marker(true);
}

// The marker over the now-playing screen's cover moves to the top right as the library opens over
// it while it shows.
static void library_open_reconnecting(SimHost& host, Shell& shell) {
    host.model().stream_state = StreamState::Disconnected;
    host.model().stream_down_since = lv_tick_get() - 10 * 1000;
    host.artists().serve(200, make_artist_pages(LIBRARY));
    open_now_playing(host, shell, PLAYING);

    shell.update();
    expect(!check_marker(false), "the marker does not show over the now-playing screen's cover");

    host.tap(LIBRARY_BUTTON.x, LIBRARY_BUTTON.y);
}

static const char* check_library_open_reconnecting(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_library_open(host, shell)) {
        return problem;
    }

    return check_marker(true);
}

// An album of a fixture: its title, and the year it was released; 0 for an album without a date.
struct AlbumFixture {
    std::string title;
    int year;
};

// Where Sigur Rós is in LIBRARY.
static constexpr size_t SIGUR_ROS = 28;

// Sigur Rós's albums as the backend gives them without a sort: newest first, by release date, the
// albums without a date last. A U+2010 hyphen, a U+2026 ellipsis, and a title longer than its row.
static const std::vector<AlbumFixture> ALBUMS = {
    {"Átta", 2023},
    {"Kveikur", 2013},
    {"Valtari", 2012},
    {"Inni", 2011},
    {"Með suð í eyrum við spilum endalaust", 2008},
    {"Hvarf‐Heim", 2007},
    {"Takk…", 2005},
    {"( )", 2002},
    {"Ágætis byrjun", 1999},
    {"Von", 1997},
    {"Rarities", 0},
};

// The answer to GET /api/library/artists/{artist_id}/albums for page `number` of `albums`, of the
// artist "artist-29", its data the `count` albums from `first`. Every third album has no picture.
static std::string make_albums_body(const std::vector<AlbumFixture>& albums, size_t first, size_t count,
                                    uint32_t number) {
    const auto data = cJSON_CreateArray();
    for (auto i = first; i < first + count; i++) {
        const auto album = cJSON_CreateObject();
        cJSON_AddStringToObject(album, "artist", LIBRARY[SIGUR_ROS].first.c_str());
        cJSON_AddStringToObject(album, "artist_id", "artist-29");

        if (i % 3) {
            cJSON_AddStringToObject(album, "artwork_id", ("YWxidW0vIi" + std::to_string(i)).c_str());
        } else {
            cJSON_AddNullToObject(album, "artwork_id");
        }

        cJSON_AddStringToObject(album, "id", ("album-" + std::to_string(i + 1)).c_str());
        cJSON_AddStringToObject(album, "title", albums[i].title.c_str());
        cJSON_AddNumberToObject(album, "track_count", 9);

        if (albums[i].year) {
            cJSON_AddNumberToObject(album, "year", albums[i].year);
        } else {
            cJSON_AddNullToObject(album, "year");
        }

        cJSON_AddItemToArray(data, album);
    }

    return make_page_body(data, number, albums.size(), "release", "desc");
}

// The tracks of Kveikur, ALBUMS' second, in disc and track order.
static const std::vector<std::string> KVEIKUR = {
    "Yfirborð", "Brennisteinn", "Hrafntinna", "Ísjaki", "Stormur", "Kveikur", "Rafstraumur", "Bláþráður", "Var",
};

// How many tracks a disc of a fixture holds.
static constexpr size_t TRACKS_TO_A_DISC = 74;

// The answer to GET /api/library/albums/{album_id}/tracks for page `number` of `titles`, its data
// the `count` tracks from `first`, TRACKS_TO_A_DISC to a disc.
static std::string make_tracks_body(const std::vector<std::string>& titles, size_t first, size_t count,
                                    uint32_t number) {
    const auto data = cJSON_CreateArray();
    for (auto i = first; i < first + count; i++) {
        const auto track = cJSON_CreateObject();
        cJSON_AddStringToObject(track, "artist", LIBRARY[SIGUR_ROS].first.c_str());
        cJSON_AddStringToObject(track, "artwork_id", "YWxidW0vIi1");
        cJSON_AddNumberToObject(track, "disc", double(i / TRACKS_TO_A_DISC + 1));
        cJSON_AddNumberToObject(track, "duration_ms", double(240000 + i * 1000));
        cJSON_AddStringToObject(track, "id", ("track-" + std::to_string(i + 1)).c_str());
        cJSON_AddNumberToObject(track, "number", double(i % TRACKS_TO_A_DISC + 1));
        cJSON_AddStringToObject(track, "title", titles[i].c_str());
        cJSON_AddItemToArray(data, track);
    }

    return make_page_body(data, number, titles.size(), "position", "asc");
}

// The tracks of an album of two discs and two pages, as large as the largest album the library
// has.
static std::vector<std::string> make_many_tracks() {
    std::vector<std::string> titles;
    for (size_t i = 0; i < 2 * TRACKS_TO_A_DISC; i++) {
        char title[32];
        snprintf(title, sizeof(title), "Track %zu.%02zu", i / TRACKS_TO_A_DISC + 1, i % TRACKS_TO_A_DISC + 1);
        titles.emplace_back(title);
    }

    return titles;
}

// The library's playlists, by title without accents and in one case: an en dash, a U+2010 hyphen,
// and a title longer than its row.
static const std::vector<std::string> PLAYLISTS = {
    "Avondeten",
    "Friday in the workshop",
    "Kerst – rustig",
    "Loud‐ish",
    "Sunday morning, slow and quiet, with coffee and the paper on the table",
    "Zomer",
};

// The answer to GET /api/library/playlists for page `number` of `titles`, its data the `count`
// playlists from `first`.
static std::string make_playlists_body(const std::vector<std::string>& titles, size_t first, size_t count,
                                       uint32_t number) {
    const auto data = cJSON_CreateArray();
    for (auto i = first; i < first + count; i++) {
        const auto playlist = cJSON_CreateObject();
        cJSON_AddStringToObject(playlist, "artwork_id", ("cGxheWxpc3Qv" + std::to_string(i)).c_str());
        cJSON_AddNumberToObject(playlist, "duration_ms", double(3600000 + i * 60000));
        cJSON_AddStringToObject(playlist, "id", ("playlist-" + std::to_string(i + 1)).c_str());
        cJSON_AddStringToObject(playlist, "title", titles[i].c_str());
        cJSON_AddNumberToObject(playlist, "track_count", double(12 + i));
        cJSON_AddItemToArray(data, playlist);
    }

    return make_page_body(data, number, titles.size(), "title", "asc");
}

// Pages of albums, tracks or playlists the API's document does not allow, by what is wrong with
// an item; the page itself is read as an artists' page is, which UNREADABLE_ARTISTS proves.
static const char* const UNREADABLE_ITEMS[] = {
    R"({"data":[],"meta":{"page":1,"per_page":20,"total":0}})",
    R"({"data":["item-1"],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"title":"Von","year":1997}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"","title":"Von","year":1997}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":7,"title":"Von","year":1997}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"item-1","year":1997}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"item-1","title":null,"year":1997}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"item-1","title":"Von","year":1997},{"id":"item-2","year":1999}],)"
    R"("meta":{"page":1,"per_page":100,"total":2}})",
};

// Albums the API's document does not allow, by their year.
static const char* const UNREADABLE_ALBUMS[] = {
    R"({"data":[{"id":"album-1","title":"Von"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"album-1","title":"Von","year":"1997"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"album-1","title":"Von","year":1997.5}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"album-1","title":"Von","year":true}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"data":[{"id":"album-1","title":"Von","year":1e30}],"meta":{"page":1,"per_page":100,"total":1}})",
};

// Every one of `bodies` is refused whole, as a 200 and as an error's body, which then gives no
// message; `allowed`, a page of one item, is taken whatever the display does not show holds.
template <typename Item>
static void expect_refused(const char* (*read)(int status, const char* body, size_t length, Page<Item>& page),
                           const char* allowed, const std::vector<const char*>& bodies) {
    Page<Item> page;
    expect(!read(200, allowed, strlen(allowed), page) && page.items.size() == 1,
           "a page was refused for what the display does not show");

    for (const auto body : bodies) {
        page = {};
        expect(read(200, body, strlen(body), page) && page.state == LoadState::Failed && page.items.empty() &&
                   page.error.empty(),
               "an unreadable page was not refused");

        page = {};
        expect(read(502, body, strlen(body), page) && page.state == LoadState::Failed && page.error.empty(),
               "an unreadable error body gave a message");
    }
}

// Where the artists screen's playlists button is, and the second and third rows of a list.
static constexpr lv_point_t PLAYLISTS_BUTTON = {728, 328};
static constexpr lv_point_t SECOND_ROW = {344, 212};
static constexpr lv_point_t THIRD_ROW = {344, 292};

// The row of the list on screen whose title is this, whole or cut, scrolled into view once the
// Shell has built the screen.
static lv_obj_t* show_row(Shell& shell, const char* title) {
    shell.update();
    lv_obj_update_layout(lv_screen_active());

    const auto list = find_list();
    expect(list, "no list is on screen");

    lv_obj_t* row = nullptr;
    for (const auto candidate : get_rows(list)) {
        if (shows_title(get_row_title(candidate), title)) {
            expect(!row, "two rows have the title to touch");
            row = candidate;
        }
    }
    expect(row, "the row to touch is not listed");

    lv_obj_scroll_to_view(row, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());

    return row;
}

static void tap_center(SimHost& host, const lv_obj_t* obj) {
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    host.tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
}

// Touches the row of the list on screen whose title is this, scrolled into view first.
static void tap_row(SimHost& host, Shell& shell, const char* title) { tap_center(host, show_row(shell, title)); }

// The label of the heading of the screen on top, which the headline's font draws.
static lv_obj_t* find_heading(lv_obj_t* obj) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return nullptr;
    }
    if (lv_obj_check_type(obj, &lv_label_class) &&
        lv_obj_get_style_text_font(obj, LV_PART_MAIN) == Theme::FONT_HEADLINE) {
        return obj;
    }

    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        if (const auto found = find_heading(lv_obj_get_child(obj, int32_t(i)))) {
            return found;
        }
    }

    return nullptr;
}

static const char* check_heading(const char* title) {
    const auto heading = find_heading(lv_screen_active());

    return heading && !strcmp(lv_label_get_text(heading), title) ? nullptr : "the heading is not the item's title";
}

// The screen of type T is on top of `views` views, with the bar under it.
template <typename T>
static const char* check_screen(const Shell& shell, size_t views) {
    if (!shows<T>(shell)) {
        return "the display is not on the screen the scene opens";
    }
    if (!shell.shows_bar()) {
        return "the screen does not show the bar";
    }

    return shell.count_views() == views ? nullptr : "the screen is not over the screens it was opened from";
}

static const char* check_says_unreachable() {
    return says(MSG_BACKEND_UNREACHABLE) && says(MSG_TRY_AGAIN) ? nullptr
                                                                : "the screen does not say why, with a way to retry";
}

static const char* check_says_failed() {
    return says(MSG_BACKEND_FAILED) && says(MSG_TRY_AGAIN) ? nullptr
                                                           : "the screen does not say why, with a way to retry";
}

static std::vector<std::string> get_album_titles() {
    std::vector<std::string> titles;
    for (const auto& album : ALBUMS) {
        titles.push_back(album.title);
    }

    return titles;
}

// The library open, and a press on Sigur Rós.
static void open_albums(SimHost& host, Shell& shell) {
    open_library(host, shell, LIBRARY);

    tap_row(host, shell, LIBRARY[SIGUR_ROS].first.c_str());
}

// A press on an artist opens its albums, newest first, each with its year, over the artists.
static void albums(SimHost& host, Shell& shell) {
    Page<Album> page;
    const auto body = make_pages(ALBUMS, make_albums_body)[0];
    expect(!read_albums(200, body.c_str(), body.length(), page), "the albums fixture was not read as a page");
    expect(page.items.size() == ALBUMS.size() &&
               page.items[0] == Album{.id = "album-1", .title = "Átta", .year = 2023} &&
               page.items[10] == Album{.id = "album-11", .title = "Rarities", .year = std::nullopt},
           "the albums fixture's albums were not read as they are");

    host.albums().serve(200, make_pages(ALBUMS, make_albums_body));
    open_albums(host, shell);
}

// The rows are the albums, in the backend's order, each with its year where it has one.
static const char* check_album_rows() {
    if (const auto problem = check_titles(get_album_titles(), 0, ALBUMS.size())) {
        return problem;
    }

    const auto rows = get_rows(find_list());
    for (size_t i = 0; i < ALBUMS.size(); i++) {
        const auto year = ALBUMS[i].year;
        const auto second = lv_obj_get_child(rows[i], 1);
        const auto has_year = lv_obj_check_type(second, &lv_label_class);
        if (year ? !has_year || lv_label_get_text(second) != std::to_string(year) : has_year) {
            return "an album's row does not show its year, or shows one it has not";
        }
    }

    return nullptr;
}

static const char* check_albums_asked(const SimHost& host, const char* artist_id) {
    if (host.albums().get_requests() != std::vector<uint32_t>{1} ||
        host.albums().get_parent_ids() != std::vector<std::string>{artist_id}) {
        return "the artist's albums were not asked for once, from the first page";
    }

    return nullptr;
}

static const char* check_albums(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 3)) {
        return problem;
    }
    if (const auto problem = check_albums_asked(host, "artist-29")) {
        return problem;
    }
    if (const auto problem = check_heading(LIBRARY[SIGUR_ROS].first.c_str())) {
        return problem;
    }

    return check_album_rows();
}

static void albums_loading(SimHost& host, Shell& shell) { open_albums(host, shell); }

static const char* check_albums_loading(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 3)) {
        return problem;
    }
    if (!says(MSG_ALBUMS_LOADING)) {
        return "the screen does not say that the albums are on their way";
    }

    return check_albums_asked(host, "artist-29");
}

static void albums_unreachable(SimHost& host, Shell& shell) {
    host.albums().serve(SimHost::UNREACHABLE, {""});

    open_albums(host, shell);
}

static const char* check_albums_unreachable(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 3)) {
        return problem;
    }

    return check_says_unreachable();
}

static void albums_error(SimHost& host, Shell& shell) {
    host.albums().serve(502, {ERROR_PLEX});

    open_albums(host, shell);
}

static const char* check_albums_error(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 3)) {
        return problem;
    }

    return check_says_failed();
}

// Every page of albums the API's document does not allow is refused whole.
static void albums_unreadable(SimHost& host, Shell& shell) {
    std::vector<const char*> bodies(std::begin(UNREADABLE_ITEMS), std::end(UNREADABLE_ITEMS));
    bodies.insert(bodies.end(), std::begin(UNREADABLE_ALBUMS), std::end(UNREADABLE_ALBUMS));

    const auto allowed = R"({"data":[{"artist":7,"artwork_id":7,"id":"album-1","label":"Smekkleysa",)"
                         R"("title":"Von","track_count":"nine","year":null}],)"
                         R"("meta":{"page":1,"per_page":100,"sort_by":null,"total":1}})";
    expect_refused(read_albums, allowed, bodies);

    Page<Album> page;
    read_albums(200, allowed, strlen(allowed), page);
    expect(page.items[0] == Album{.id = "album-1", .title = "Von", .year = std::nullopt},
           "an album without a date was not read as one");

    host.albums().serve(200, {bodies.back()});
    open_albums(host, shell);
}

static void albums_empty(SimHost& host, Shell& shell) {
    host.albums().serve(200, make_pages(std::vector<AlbumFixture>{}, make_albums_body));

    open_albums(host, shell);
}

static const char* check_albums_empty(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 3)) {
        return problem;
    }

    return says(MSG_ALBUMS_EMPTY) ? nullptr : "the screen does not say that the artist has no albums";
}

// The albums' end: the album without a date shows no year.
static void albums_end(SimHost& host, Shell& shell) {
    host.albums().serve(200, make_pages(ALBUMS, make_albums_body));
    open_now_playing(host, shell, PLAYING);
    shell.push(std::make_unique<AlbumsView>(&host, &shell, Artist{.id = "artist-29", .title = "Sigur Rós"}));

    scroll_list(shell, true);
}

static const char* check_albums_end(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 2)) {
        return problem;
    }
    if (const auto problem = check_albums_asked(host, "artist-29")) {
        return problem;
    }
    if (lv_obj_get_scroll_bottom(find_list()) != 0) {
        return "the list is not at its end";
    }

    return check_album_rows();
}

// The back button closes the albums, and the artists are as they were.
static void albums_back(SimHost& host, Shell& shell) {
    host.albums().serve(200, make_pages(ALBUMS, make_albums_body));
    open_albums(host, shell);

    host.tap(BACK.x, BACK.y);
}

static const char* check_albums_back(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<ArtistsView>(shell, 2)) {
        return problem;
    }

    return check_rows(LIBRARY, 0, LIBRARY.size());
}

// The albums of the artist with the longest name, on their way: its name is the heading, cut where
// the heading ends.
static void albums_long_name(SimHost& host, Shell& shell) {
    open_library(host, shell, LIBRARY);

    host.tap(THIRD_ROW.x, THIRD_ROW.y);
}

// The heading's title is cut with dots, and runs to the heading's end, or, while the marker shows,
// ends a gap before it.
static const char* check_long_heading(bool marker) {
    lv_obj_update_layout(lv_screen_active());

    const auto heading = find_heading(lv_screen_active());
    if (!heading || !std::string(lv_label_get_text(heading)).ends_with("...")) {
        return "the long heading is not cut with dots";
    }

    lv_area_t area;
    lv_obj_get_coords(heading, &area);
    if (!marker) {
        return area.x2 == 800 - Theme::PADDING - 1 ? nullptr : "the heading does not run to its row's end";
    }

    std::vector<lv_obj_t*> found;
    find_labels(lv_screen_active(), MSG_RECONNECTING, found);
    if (found.size() != 1) {
        return "the marker does not show";
    }

    lv_area_t marker_area;
    lv_obj_get_coords(lv_obj_get_parent(found[0]), &marker_area);

    return area.x2 + 1 + Theme::GAP <= marker_area.x1 ? nullptr : "the heading runs under the marker";
}

static const char* check_albums_long_name(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 3)) {
        return problem;
    }
    if (const auto problem = check_albums_asked(host, "artist-3")) {
        return problem;
    }

    return check_long_heading(false);
}

static void albums_reconnecting(SimHost& host, Shell& shell) {
    host.model().stream_state = StreamState::Disconnected;
    host.model().stream_down_since = lv_tick_get() - 10 * 1000;

    albums_long_name(host, shell);
}

static const char* check_albums_reconnecting(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 3)) {
        return problem;
    }
    if (const auto problem = check_marker(true)) {
        return problem;
    }

    return check_long_heading(true);
}

// The marker goes, and the heading takes its room again.
static void albums_reconnected(SimHost& host, Shell& shell) {
    albums_reconnecting(host, shell);

    host.answer_after(SimHost::TAP_INTERVAL_MS, [&host] {
        host.model().stream_state = StreamState::Connected;
        host.model().revision++;
    });
}

static const char* check_albums_reconnected(const SimHost& host, const Shell& shell) {
    if (says(MSG_RECONNECTING)) {
        return "the marker still shows";
    }

    return check_albums_long_name(host, shell);
}

// A press on a row of a list built from the second and third pages of the artists, past the state
// of the first: the row's own artist opens.
static void artists_pages_open(SimHost& host, Shell& shell) {
    artists_pages(host, shell);

    tap_row(host, shell, "Z Artist 250");
}

static const char* check_artists_pages_open(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 3)) {
        return problem;
    }
    if (const auto problem = check_heading("Z Artist 250")) {
        return problem;
    }

    return check_albums_asked(host, "artist-250");
}

// A press on an album opens its tracks, in disc and track order, over its artist's albums.
static void tracks(SimHost& host, Shell& shell) {
    Page<AlbumTrack> page;
    const auto body = make_pages(KVEIKUR, make_tracks_body)[0];
    expect(!read_tracks(200, body.c_str(), body.length(), page), "the tracks fixture was not read as a page");
    expect(page.items.size() == KVEIKUR.size() && page.items[3] == AlbumTrack{.id = "track-4", .title = "Ísjaki"},
           "the tracks fixture's tracks were not read as they are");

    host.albums().serve(200, make_pages(ALBUMS, make_albums_body));
    host.tracks().serve(200, make_pages(KVEIKUR, make_tracks_body));
    open_albums(host, shell);

    host.tap(SECOND_ROW.x, SECOND_ROW.y);
}

static const char* check_tracks(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<TracksView>(shell, 4)) {
        return problem;
    }
    if (host.tracks().get_requests() != std::vector<uint32_t>{1} ||
        host.tracks().get_parent_ids() != std::vector<std::string>{"album-2"}) {
        return "the album's tracks were not asked for once, from the first page";
    }
    if (const auto problem = check_heading("Kveikur")) {
        return problem;
    }

    return check_titles(KVEIKUR, 0, KVEIKUR.size());
}

// The tracks screen of an album the scene's host answers for, over the now-playing screen.
static void open_tracks(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);

    shell.push(std::make_unique<TracksView>(&host, &shell, Album{.id = "album-12", .title = "Complete Recordings"}));
}

// An album of 148 tracks: the first page, and scrolled to its end, the second, after the first.
static void tracks_pages(SimHost& host, Shell& shell) {
    const auto many = make_many_tracks();
    host.tracks().serve(200, make_pages(many, make_tracks_body));
    open_tracks(host, shell);

    shell.update();
    expect(!check_titles(many, 0, PAGE_SIZE), "the first page of the tracks is not listed");

    expect_in_place(scroll_list(shell, true), "the row at the top moved as the second page came");
}

static const char* check_tracks_pages(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<TracksView>(shell, 2)) {
        return problem;
    }
    if (host.tracks().get_requests() != std::vector<uint32_t>{1, 2} ||
        host.tracks().get_parent_ids() != std::vector<std::string>{"album-12", "album-12"}) {
        return "the album's two pages were not asked for one by one";
    }

    const auto many = make_many_tracks();

    return check_titles(many, 0, many.size());
}

static void tracks_loading(SimHost& host, Shell& shell) { open_tracks(host, shell); }

static const char* check_tracks_loading(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<TracksView>(shell, 2)) {
        return problem;
    }

    return says(MSG_TRACKS_LOADING) ? nullptr : "the screen does not say that the tracks are on their way";
}

static void tracks_unreachable(SimHost& host, Shell& shell) {
    host.tracks().serve(SimHost::UNREACHABLE, {""});

    open_tracks(host, shell);
}

static const char* check_tracks_unreachable(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<TracksView>(shell, 2)) {
        return problem;
    }

    return check_says_unreachable();
}

static void tracks_error(SimHost& host, Shell& shell) {
    host.tracks().serve(502, {ERROR_PLEX});

    open_tracks(host, shell);
}

static const char* check_tracks_error(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<TracksView>(shell, 2)) {
        return problem;
    }

    return check_says_failed();
}

// Every page of tracks the API's document does not allow is refused whole.
static void tracks_unreadable(SimHost& host, Shell& shell) {
    const std::vector<const char*> bodies(std::begin(UNREADABLE_ITEMS), std::end(UNREADABLE_ITEMS));
    const auto allowed = R"({"data":[{"artist":null,"disc":"one","duration_ms":-1,"id":"track-1","number":null,)"
                         R"("title":"Von"}],"meta":{"page":1,"per_page":100,"sort_by":null,"total":1}})";
    expect_refused(read_tracks, allowed, bodies);

    host.tracks().serve(200, {bodies.back()});
    open_tracks(host, shell);
}

static void tracks_empty(SimHost& host, Shell& shell) {
    host.tracks().serve(200, make_pages(std::vector<std::string>{}, make_tracks_body));

    open_tracks(host, shell);
}

static const char* check_tracks_empty(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<TracksView>(shell, 2)) {
        return problem;
    }

    return says(MSG_TRACKS_EMPTY) ? nullptr : "the screen does not say that the album has no tracks";
}

// The back button closes the tracks, and the albums are as they were.
static void tracks_back(SimHost& host, Shell& shell) {
    tracks(host, shell);

    host.tap(BACK.x, BACK.y);
}

static const char* check_tracks_back(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 3)) {
        return problem;
    }

    return check_album_rows();
}

// A touch on the bar's texts, from the tracks, three screens over the now-playing screen.
static void tracks_bar_texts(SimHost& host, Shell& shell) {
    tracks(host, shell);

    host.tap(BAR_TEXTS.x, BAR_TEXTS.y);
}

// The button under the artists' letters opens the playlists, over the artists.
static void playlists(SimHost& host, Shell& shell) {
    Page<Playlist> page;
    const auto body = make_pages(PLAYLISTS, make_playlists_body)[0];
    expect(!read_playlists(200, body.c_str(), body.length(), page), "the playlists fixture was not read as a page");
    expect(page.items.size() == PLAYLISTS.size() &&
               page.items[2] == Playlist{.id = "playlist-3", .title = "Kerst – rustig"},
           "the playlists fixture's playlists were not read as they are");

    host.playlists().serve(200, make_pages(PLAYLISTS, make_playlists_body));
    open_library(host, shell, LIBRARY);

    host.tap(PLAYLISTS_BUTTON.x, PLAYLISTS_BUTTON.y);
}

static const char* check_playlists(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<PlaylistsView>(shell, 3)) {
        return problem;
    }
    if (host.playlists().get_requests() != std::vector<uint32_t>{1}) {
        return "the playlists were not asked for once, from the first page";
    }
    if (const auto problem = check_heading(MSG_PLAYLISTS)) {
        return problem;
    }

    return check_titles(PLAYLISTS, 0, PLAYLISTS.size());
}

// A playlist is never opened: the backend does not list its tracks.
static void playlists_row(SimHost& host, Shell& shell) {
    playlists(host, shell);

    host.tap(SECOND_ROW.x, SECOND_ROW.y);
}

static const char* check_playlists_row(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_playlists(host, shell)) {
        return problem;
    }

    return host.tracks().get_requests().empty() && host.albums().get_requests().empty()
               ? nullptr
               : "a press on a playlist asked for a list";
}

// The playlists are reached whatever came of the artists.
static void playlists_artists_failed(SimHost& host, Shell& shell) {
    host.artists().serve(SimHost::UNREACHABLE, {""});
    host.playlists().serve(200, make_pages(PLAYLISTS, make_playlists_body));
    open_artists(host, shell);
    shell.update();
    expect(!check_says_unreachable(), "the artists screen does not say that the artists did not come");

    host.tap(PLAYLISTS_BUTTON.x, PLAYLISTS_BUTTON.y);
}

// The playlists screen over the now-playing screen.
static void open_playlists(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);

    shell.push(std::make_unique<PlaylistsView>(&host, &shell));
}

static void playlists_loading(SimHost& host, Shell& shell) { open_playlists(host, shell); }

static const char* check_playlists_loading(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<PlaylistsView>(shell, 2)) {
        return problem;
    }

    return says(MSG_PLAYLISTS_LOADING) ? nullptr : "the screen does not say that the playlists are on their way";
}

static void playlists_unreachable(SimHost& host, Shell& shell) {
    host.playlists().serve(SimHost::UNREACHABLE, {""});

    open_playlists(host, shell);
}

static const char* check_playlists_unreachable(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<PlaylistsView>(shell, 2)) {
        return problem;
    }

    return check_says_unreachable();
}

static void playlists_error(SimHost& host, Shell& shell) {
    host.playlists().serve(502, {ERROR_PLEX});

    open_playlists(host, shell);
}

static const char* check_playlists_error(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<PlaylistsView>(shell, 2)) {
        return problem;
    }

    return check_says_failed();
}

// Every page of playlists the API's document does not allow is refused whole.
static void playlists_unreadable(SimHost& host, Shell& shell) {
    const std::vector<const char*> bodies(std::begin(UNREADABLE_ITEMS), std::end(UNREADABLE_ITEMS));
    const auto allowed = R"({"data":[{"artwork_id":null,"duration_ms":null,"id":"playlist-1","title":"Zomer",)"
                         R"("track_count":-1}],"meta":{"page":1,"per_page":100,"sort_by":null,"total":1}})";
    expect_refused(read_playlists, allowed, bodies);

    host.playlists().serve(200, {bodies.back()});
    open_playlists(host, shell);
}

static void playlists_empty(SimHost& host, Shell& shell) {
    host.playlists().serve(200, make_pages(std::vector<std::string>{}, make_playlists_body));

    open_playlists(host, shell);
}

static const char* check_playlists_empty(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<PlaylistsView>(shell, 2)) {
        return problem;
    }

    return says(MSG_PLAYLISTS_EMPTY) ? nullptr : "the screen does not say that the library has no playlists";
}

// The back button closes the playlists, back to the artists.
static void playlists_back(SimHost& host, Shell& shell) {
    playlists(host, shell);

    host.tap(BACK.x, BACK.y);
}

static const char* check_playlists_back(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<ArtistsView>(shell, 2)) {
        return problem;
    }

    return check_rows(LIBRARY, 0, LIBRARY.size());
}

// The answer to a request to queue an item that the backend took: the task that does it.
static const char QUEUE_TAKEN[] = R"({"status":"pending","task_id":"task-7"})";

// A request to queue an item that the backend refuses before any task starts.
static const char ERROR_ITEM_NOT_FOUND[] =
    R"({"code":"ITEM_NOT_FOUND","correlationId":null,"details":{"message":"The request could not be completed"},)"
    R"("error":"Plex has no album with this id"})";

// The words of a queueing task that failed, longer than the bar's two lines.
static const char TASK_ERROR[] = "Plex did not answer in time, and the album was not queued";

// The events of the task QUEUE_TAKEN names, and of another one.
static const char TASK_FAILED[] =
    R"({"data":{"error":"Plex did not answer in time, and the album was not queued"},"event_type":"task_failed",)"
    R"("task_id":"task-7","timestamp":"2026-11-16T10:12:25.123456+00:00"})";
static const char OTHER_TASK_FAILED[] =
    R"({"data":{"error":"The speaker did not answer"},"event_type":"task_failed","task_id":"task-9",)"
    R"("timestamp":"2026-11-16T10:12:25.123456+00:00"})";
static const char TASK_COMPLETED[] =
    R"({"data":{},"event_type":"task_completed","task_id":"task-7","timestamp":"2026-11-16T10:12:25.123456+00:00"})";

// Whether a label on screen shows this text, whole or cut with dots where its place ends.
static bool shows_text(const lv_obj_t* obj, const char* text) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    if (lv_obj_check_type(obj, &lv_label_class) && shows_title(lv_label_get_text(obj), text)) {
        return true;
    }

    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        if (shows_text(lv_obj_get_child(obj, int32_t(i)), text)) {
            return true;
        }
    }

    return false;
}

// The answers to a request to queue an item and the events of its task, as the device reads them,
// and the rules by which the display watches for its own task's end.
static void expect_queue_rules() {
    std::string task_id;
    std::string error;
    expect(!read_queue_answer(202, QUEUE_TAKEN, strlen(QUEUE_TAKEN), task_id, error) && task_id == "task-7",
           "a 202 with a task was not read as a request taken");
    expect(read_queue_answer(204, QUEUE_TAKEN, strlen(QUEUE_TAKEN), task_id, error) &&
               read_queue_answer(200, QUEUE_TAKEN, strlen(QUEUE_TAKEN), task_id, error),
           "an answer other than a 202 was read as a request to queue an item taken");
    expect(read_queue_answer(202, R"({"status":"pending"})", 20, task_id, error), "a 202 without a task_id was taken");
    expect(read_queue_answer(202, R"(["task-7"])", 10, task_id, error), "a 202 that is not an object was taken");
    expect(read_queue_answer(404, ERROR_ITEM_NOT_FOUND, strlen(ERROR_ITEM_NOT_FOUND), task_id, error) &&
               error == "Plex has no album with this id",
           "an error envelope was taken, or its words not read");

    const auto read_event = [](const char* payload, std::optional<TaskEnd>& end) {
        const auto json = cJSON_Parse(payload);
        const auto problem = read_task_event(json, end);
        cJSON_Delete(json);

        return problem;
    };

    std::optional<TaskEnd> end;
    expect(!read_event(TASK_FAILED, end) && end && end->task_id == "task-7" && end->error == TASK_ERROR,
           "a task_failed was not read as its task's failure, in its words");
    expect(!read_event(TASK_COMPLETED, end) && end && end->task_id == "task-7" && !end->error,
           "a task_completed was not read as its task's end");
    expect(!read_event(R"({"data":null,"event_type":"task_started","task_id":"task-7","timestamp":"x"})", end) && !end,
           "a task_started was read as an end");
    expect(
        !read_event(R"({"data":{"text":"Queueing","value":0.5},"event_type":"progress_update","task_id":"t"})", end) &&
            !end,
        "a progress_update was read as an end");
    expect(!read_event(R"({"data":{"error":"Plex♫ failed"},"event_type":"task_failed","task_id":"t"})", end) && end &&
               end->error == "Plex failed",
           "a failure's words were read with a character the fonts do not draw");
    expect(read_event(R"({"event_type":"task_failed","task_id":"t"})", end) &&
               read_event(R"({"data":null,"event_type":"task_failed","task_id":"t"})", end) &&
               read_event(R"({"data":{"error":7},"event_type":"task_failed","task_id":"t"})", end) &&
               read_event(R"({"data":{},"event_type":"task_cancelled","task_id":"t"})", end) &&
               read_event(R"({"data":{},"event_type":"task_completed"})", end) &&
               read_event(R"({"data":{},"task_id":"t"})", end) && read_event(R"(["task_completed"])", end),
           "a task_event the API's document does not allow was read");

    QueueRequest request{.kind = ItemKind::Album, .item_id = "album-\"1\"", .action = QueueAction::Play};
    expect(write_queue_request(request) == R"({"kind":"album","item_id":"album-\"1\"","action":"play"})",
           "an id was not written as a JSON string");

    static constexpr uint32_t LIMIT = ANSWER_TIME_LIMIT_MS;
    Queueing queueing;

    // An add the backend took, whose task then fails and another task's too.
    auto id = queueing.sent(QueueAction::Add, 1000);
    expect(!queueing.task_ended(TaskEnd{.task_id = "task-9", .error = "other"}) && queueing.ended.size() == 1,
           "an end that came while a request was on its way was not held");
    queueing.taken(id, "task-7");
    expect(queueing.adds_taken == 1 && queueing.running == std::vector<std::string>{"task-7"} &&
               queueing.ended.empty() && !queueing.tasks_failed,
           "an add taken was not counted, its task not watched, or another task's end held on");
    expect(!queueing.task_ended(TaskEnd{.task_id = "task-9", .error = "other"}) && queueing.ended.empty() &&
               !queueing.tasks_failed,
           "another task's failure was taken, or held with no request on its way");
    expect(queueing.task_ended(TaskEnd{.task_id = "task-7", .error = "words"}) && queueing.tasks_failed == 1 &&
               queueing.failure == "words" && queueing.running.empty(),
           "the failure of the display's task was not said, or its task watched on");

    // A play whose task completes before the answer comes.
    id = queueing.sent(QueueAction::Play, 2000);
    queueing.task_ended(TaskEnd{.task_id = "task-10"});
    queueing.taken(id, "task-10");
    expect(queueing.running.empty() && queueing.ended.empty() && queueing.adds_taken == 1 && queueing.tasks_failed == 1,
           "a task that completed before its answer is watched, or a play counted as an add");

    // The ends held, and the tasks watched, are bounded.
    id = queueing.sent(QueueAction::Play, 3000);
    for (size_t i = 0; i < Queueing::MAX_TASKS + 1; i++) {
        queueing.task_ended(TaskEnd{.task_id = "other-" + std::to_string(i)});
    }
    expect(queueing.ended.size() == Queueing::MAX_TASKS && queueing.ended[0].task_id == "other-1",
           "the ends held are not bounded, or the oldest did not give way");
    queueing.not_taken(id);
    expect(queueing.ended.empty() && queueing.requests_not_taken == 1,
           "the ends held stayed once no request was on its way");

    for (size_t i = 0; i < Queueing::MAX_TASKS + 1; i++) {
        queueing.taken(queueing.sent(QueueAction::Play, 4000), "task-" + std::to_string(20 + i));
    }
    expect(queueing.running.size() == Queueing::MAX_TASKS && queueing.running[0] == "task-21",
           "the tasks watched are not bounded, or the oldest did not give way");

    // The time limit, across the tick's wrap too.
    id = queueing.sent(QueueAction::Add, UINT32_MAX - 99);
    queueing.task_ended(TaskEnd{.task_id = "task-30"});
    expect(!queueing.timed_out(LIMIT - 101) && queueing.is_sending(id), "a request was settled before its time limit");
    expect(queueing.timed_out(LIMIT - 100) && !queueing.is_sending(id) && queueing.requests_not_taken == 2 &&
               queueing.ended.empty() && queueing.adds_taken == 1,
           "the time limit did not settle a request as not taken, or kept the ends held");
    expect(!queueing.timed_out(LIMIT), "a request was settled twice");
}

// Touches the button that does `action` at the end of the row whose title is this, scrolled into
// view first: Play first, then Shuffle where the row has it, and Add to queue last.
static void tap_queue_button(SimHost& host, Shell& shell, const char* title, QueueAction action) {
    const auto buttons = find_queue_buttons(show_row(shell, title));
    expect(buttons, "the row does not end in the queue's buttons");

    const auto count = int32_t(lv_obj_get_child_count(buttons));
    expect(action != QueueAction::Shuffle || count == 3, "the row has no Shuffle");

    const auto index = action == QueueAction::Play ? 0 : action == QueueAction::Shuffle ? 1 : count - 1;
    tap_center(host, lv_obj_get_child(buttons, index));
}

// The requests the presses sent, and their bodies, as the device writes them.
static const char* check_queue_requests(const SimHost& host, const std::vector<QueueRequest>& requests,
                                        const std::vector<std::string>& bodies) {
    if (host.get_queue_requests() != requests) {
        return "the presses did not ask for the items and actions pressed, in order";
    }

    return host.get_queue_request_bodies() == bodies ? nullptr : "a request's body is not the one the API takes";
}

// The bar under the screen says that the add went, and nothing else was said.
static const char* check_added(const SimHost& host) {
    const auto& queueing = host.get_model().queueing;
    if (queueing.adds_taken != 1 || queueing.requests_not_taken || queueing.tasks_failed) {
        return "the add was not taken alone";
    }

    return says(MSG_ADDED_TO_QUEUE) ? nullptr : "the bar does not say that the add went";
}

// Every row of the list on screen ends in the queue's buttons, three, or two without Shuffle.
static const char* check_queue_buttons(size_t count) {
    for (const auto row : get_rows(find_list())) {
        const auto buttons = find_queue_buttons(row);
        if (!buttons || lv_obj_get_child_count(buttons) != count) {
            return count == 3 ? "a row does not offer Play, Shuffle and Add to queue"
                              : "a row does not offer Play and Add to queue alone";
        }
    }

    return nullptr;
}

// A press on an artist's Play, Shuffle and Add to queue, each on the artist of its row: Play and
// Add name the albums' order, newest first, which Shuffle does not take. None opens the artist,
// and the bar says that the add went.
static void artists_queue(SimHost& host, Shell& shell) {
    expect_queue_rules();

    host.answer_queue_requests(202, QUEUE_TAKEN);
    open_library(host, shell, LIBRARY);

    tap_queue_button(host, shell, LIBRARY[0].first.c_str(), QueueAction::Play);
    tap_queue_button(host, shell, LIBRARY[1].first.c_str(), QueueAction::Shuffle);
    tap_queue_button(host, shell, LIBRARY[2].first.c_str(), QueueAction::Add);
}

static const char* check_artists_queue(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<ArtistsView>(shell, 2)) {
        return problem;
    }
    if (!host.albums().get_requests().empty()) {
        return "a press on a button opened its artist";
    }
    if (const auto problem = check_queue_buttons(3)) {
        return problem;
    }
    if (const auto problem =
            check_queue_requests(host,
                                 {
                                     {.kind = ItemKind::Artist, .item_id = "artist-1", .action = QueueAction::Play},
                                     {.kind = ItemKind::Artist, .item_id = "artist-2", .action = QueueAction::Shuffle},
                                     {.kind = ItemKind::Artist, .item_id = "artist-3", .action = QueueAction::Add},
                                 },
                                 {
                                     R"({"kind":"artist","item_id":"artist-1","action":"play","order":"newest_first"})",
                                     R"({"kind":"artist","item_id":"artist-2","action":"shuffle"})",
                                     R"({"kind":"artist","item_id":"artist-3","action":"add","order":"newest_first"})",
                                 })) {
        return problem;
    }

    return check_added(host);
}

// A drag that starts on a row's Play scrolls the list, and sends nothing.
static void artists_queue_drag(SimHost& host, Shell& shell) {
    open_library(host, shell, LIBRARY);

    const auto play = lv_obj_get_child(find_queue_buttons(show_row(shell, LIBRARY[2].first.c_str())), 0);
    lv_area_t area;
    lv_obj_get_coords(play, &area);
    const lv_point_t from{(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};

    host.drag(from, lv_point_t{from.x, from.y - 160});
}

static const char* check_artists_queue_drag(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<ArtistsView>(shell, 2)) {
        return problem;
    }
    if (!host.get_queue_requests().empty() || !host.albums().get_requests().empty()) {
        return "a drag that started on Play sent it, or opened the artist";
    }

    return lv_obj_get_scroll_y(find_list()) > 0 ? nullptr : "a drag that started on Play did not scroll the list";
}

// The albums screen of Sigur Rós, over the now-playing screen.
static void open_albums_screen(SimHost& host, Shell& shell) {
    host.albums().serve(200, make_pages(ALBUMS, make_albums_body));
    open_now_playing(host, shell, PLAYING);

    shell.push(std::make_unique<AlbumsView>(
        &host, &shell, Artist{.id = "artist-29", .title = LIBRARY[SIGUR_ROS].first, .index_letter = 'S'}));
}

// A press on an album's Play, Shuffle and Add to queue, each on the album of its row. None opens
// the album, and the bar says that the add went.
static void albums_queue(SimHost& host, Shell& shell) {
    host.answer_queue_requests(202, QUEUE_TAKEN);
    open_albums_screen(host, shell);

    tap_queue_button(host, shell, "Átta", QueueAction::Play);
    tap_queue_button(host, shell, "Kveikur", QueueAction::Shuffle);
    tap_queue_button(host, shell, "Valtari", QueueAction::Add);
}

static const char* check_albums_queue(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 2)) {
        return problem;
    }
    if (!host.tracks().get_requests().empty()) {
        return "a press on a button opened its album";
    }
    if (const auto problem = check_queue_buttons(3)) {
        return problem;
    }
    if (const auto problem =
            check_queue_requests(host,
                                 {
                                     {.kind = ItemKind::Album, .item_id = "album-1", .action = QueueAction::Play},
                                     {.kind = ItemKind::Album, .item_id = "album-2", .action = QueueAction::Shuffle},
                                     {.kind = ItemKind::Album, .item_id = "album-3", .action = QueueAction::Add},
                                 },
                                 {
                                     R"({"kind":"album","item_id":"album-1","action":"play"})",
                                     R"({"kind":"album","item_id":"album-2","action":"shuffle"})",
                                     R"({"kind":"album","item_id":"album-3","action":"add"})",
                                 })) {
        return problem;
    }

    return check_added(host);
}

// An album whose title is longer than its row: the title is cut with dots before its year, and the
// year ends before the buttons.
static void albums_long_title(SimHost& host, Shell& shell) {
    open_albums_screen(host, shell);

    show_row(shell, ALBUMS[4].title.c_str());
}

static const char* check_albums_long_title(const SimHost&, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 2)) {
        return problem;
    }

    lv_obj_t* row = nullptr;
    for (const auto candidate : get_rows(find_list())) {
        if (shows_title(get_row_title(candidate), ALBUMS[4].title)) {
            row = candidate;
        }
    }
    if (!row) {
        return "the album with the long title is not listed";
    }

    lv_area_t title;
    lv_area_t year;
    lv_area_t buttons;
    lv_area_t shown;
    lv_obj_get_coords(lv_obj_get_child(row, 0), &title);
    lv_obj_get_coords(lv_obj_get_child(row, 1), &year);
    lv_obj_get_coords(find_queue_buttons(row), &buttons);
    lv_obj_get_coords(find_list(), &shown);

    if (std::string(get_row_title(row)) == ALBUMS[4].title) {
        return "the long title is not cut";
    }
    if (title.x2 >= year.x1 || year.x2 >= buttons.x1) {
        return "the long title runs into its year, or the year into the buttons";
    }

    return title.y1 >= shown.y1 && title.y2 <= shown.y2 ? nullptr : "the row with the long title is not on screen";
}

// A press on a track's Play and Add to queue; a track offers no Shuffle.
static void tracks_queue(SimHost& host, Shell& shell) {
    host.answer_queue_requests(202, QUEUE_TAKEN);
    host.tracks().serve(200, make_pages(KVEIKUR, make_tracks_body));
    open_tracks(host, shell);

    tap_queue_button(host, shell, KVEIKUR[0].c_str(), QueueAction::Play);
    tap_queue_button(host, shell, KVEIKUR[1].c_str(), QueueAction::Add);
}

static const char* check_tracks_queue(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<TracksView>(shell, 2)) {
        return problem;
    }
    if (const auto problem = check_queue_buttons(2)) {
        return problem;
    }
    if (const auto problem =
            check_queue_requests(host,
                                 {
                                     {.kind = ItemKind::Track, .item_id = "track-1", .action = QueueAction::Play},
                                     {.kind = ItemKind::Track, .item_id = "track-2", .action = QueueAction::Add},
                                 },
                                 {
                                     R"({"kind":"track","item_id":"track-1","action":"play"})",
                                     R"({"kind":"track","item_id":"track-2","action":"add"})",
                                 })) {
        return problem;
    }

    return check_added(host);
}

// A press on a playlist's Play, Shuffle and Add to queue, each on the playlist of its row.
static void playlists_queue(SimHost& host, Shell& shell) {
    host.answer_queue_requests(202, QUEUE_TAKEN);
    host.playlists().serve(200, make_pages(PLAYLISTS, make_playlists_body));
    open_playlists(host, shell);

    tap_queue_button(host, shell, PLAYLISTS[0].c_str(), QueueAction::Play);
    tap_queue_button(host, shell, PLAYLISTS[1].c_str(), QueueAction::Shuffle);
    tap_queue_button(host, shell, PLAYLISTS[2].c_str(), QueueAction::Add);
}

static const char* check_playlists_queue(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<PlaylistsView>(shell, 2)) {
        return problem;
    }
    if (const auto problem = check_queue_buttons(3)) {
        return problem;
    }
    if (const auto problem = check_queue_requests(
            host,
            {
                {.kind = ItemKind::Playlist, .item_id = "playlist-1", .action = QueueAction::Play},
                {.kind = ItemKind::Playlist, .item_id = "playlist-2", .action = QueueAction::Shuffle},
                {.kind = ItemKind::Playlist, .item_id = "playlist-3", .action = QueueAction::Add},
            },
            {
                R"({"kind":"playlist","item_id":"playlist-1","action":"play"})",
                R"({"kind":"playlist","item_id":"playlist-2","action":"shuffle"})",
                R"({"kind":"playlist","item_id":"playlist-3","action":"add"})",
            })) {
        return problem;
    }

    return check_added(host);
}

// An add the backend refuses: the bar says that it did not go through, not that it went.
static void queue_not_taken(SimHost& host, Shell& shell) {
    host.answer_queue_requests(404, ERROR_ITEM_NOT_FOUND);
    open_albums_screen(host, shell);

    tap_queue_button(host, shell, "Átta", QueueAction::Add);
}

static const char* check_queue_not_taken(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 2)) {
        return problem;
    }

    const auto& queueing = host.get_model().queueing;
    if (queueing.requests_not_taken != 1 || queueing.adds_taken || !queueing.running.empty()) {
        return "the add refused was taken";
    }

    return says(MSG_COMMAND_NOT_TAKEN) && !says(MSG_ADDED_TO_QUEUE) ? nullptr
                                                                    : "the bar does not say that it did not go through";
}

// The task of a play the backend took fails: the bar says why, in the backend's words, cut to its
// two lines.
static void queue_failed(SimHost& host, Shell& shell) {
    host.answer_queue_requests(202, QUEUE_TAKEN);
    open_albums_screen(host, shell);

    tap_queue_button(host, shell, "Átta", QueueAction::Play);
    host.answer_after(400, [&host]() { host.send_task_event(TASK_FAILED); });
}

static const char* check_task_failed(const SimHost& host) {
    const auto& queueing = host.get_model().queueing;
    if (queueing.tasks_failed != 1 || queueing.failure != TASK_ERROR || !queueing.running.empty() ||
        !queueing.ended.empty() || queueing.requests_not_taken) {
        return "the failure of the display's task was not taken once, or its task is still watched";
    }

    return shows_text(lv_screen_active(), TASK_ERROR) ? nullptr : "the failure is not said in the backend's words";
}

static const char* check_queue_failed(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 2)) {
        return problem;
    }

    return check_task_failed(host);
}

// The failure of the task reaches the display before the answer that names the task: it is said
// once the answer comes.
static void queue_failed_early(SimHost& host, Shell& shell) {
    open_albums_screen(host, shell);

    tap_queue_button(host, shell, "Átta", QueueAction::Play);
    host.answer_after(250, [&host]() { host.send_task_event(TASK_FAILED); });
    host.answer_after(450, [&host]() { host.answer_queue_request(202, QUEUE_TAKEN); });
}

// Another task fails, and the display's own completes: nothing is said, and the task is no longer
// watched.
static void queue_failed_other(SimHost& host, Shell& shell) {
    host.answer_queue_requests(202, QUEUE_TAKEN);
    open_albums_screen(host, shell);

    tap_queue_button(host, shell, "Átta", QueueAction::Play);
    host.answer_after(300, [&host]() { host.send_task_event(OTHER_TASK_FAILED); });
    host.answer_after(400, [&host]() { host.send_task_event(TASK_COMPLETED); });
}

static const char* check_queue_failed_other(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 2)) {
        return problem;
    }

    const auto& queueing = host.get_model().queueing;
    if (queueing.tasks_failed || queueing.requests_not_taken || !queueing.running.empty()) {
        return "another task's failure was taken, or the display's task is still watched";
    }
    if (shows_text(lv_screen_active(), "The speaker did not answer") || says(MSG_COMMAND_NOT_TAKEN)) {
        return "the bar says another task's failure";
    }

    return says("Svefn-g-englar") ? nullptr : "the bar does not say what plays";
}

// A request to queue an item that is not answered: once its time limit has passed the bar says
// that it did not go through, and the answer and the task's failure that come after it change
// nothing.
static void queue_no_answer(SimHost& host, Shell& shell) {
    open_albums_screen(host, shell);

    tap_queue_button(host, shell, "Átta", QueueAction::Add);
    host.answer_after(ANSWER_TIME_LIMIT_MS + 400, [&host]() { host.answer_queue_request(202, QUEUE_TAKEN); });
    host.answer_after(ANSWER_TIME_LIMIT_MS + 500, [&host]() { host.send_task_event(TASK_FAILED); });
}

static const char* check_queue_no_answer(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_screen<AlbumsView>(shell, 2)) {
        return problem;
    }
    if (host.count_unanswered_queue_requests()) {
        return "the late answer did not come";
    }

    const auto& queueing = host.get_model().queueing;
    if (queueing.requests_not_taken != 1 || queueing.adds_taken || queueing.tasks_failed || !queueing.running.empty() ||
        !queueing.sending.empty()) {
        return "the time limit did not settle the request as not taken, or the late answer changed it";
    }

    return says(MSG_COMMAND_NOT_TAKEN) && !says(MSG_ADDED_TO_QUEUE) ? nullptr
                                                                    : "the bar does not say that it did not go through";
}

// A play from the albums, and a touch on the bar for the now-playing screen: the task's failure
// comes there, and the screen says it over the cover.
static void queue_failed_now_playing(SimHost& host, Shell& shell) {
    host.answer_queue_requests(202, QUEUE_TAKEN);
    open_albums_screen(host, shell);

    tap_queue_button(host, shell, "Átta", QueueAction::Play);
    host.tap(BAR_TEXTS.x, BAR_TEXTS.y);
    host.answer_after(600, [&host]() { host.send_task_event(TASK_FAILED); });
}

static const char* check_queue_failed_now_playing(const SimHost& host, const Shell& shell) {
    if (!shows<NowPlayingView>(shell) || shell.count_views() != 1) {
        return "the touch on the bar did not bring the now-playing screen";
    }

    return check_task_failed(host);
}

// An entry of a queue's fixture: its title, and its artist.
using EntryFixture = std::pair<std::string, std::string>;

// The Garage's queue: an album, then tracks of others, among them a title and an artist longer than
// their places, a U+2010 hyphen, and a character the fonts do not draw (U+266B).
static const std::vector<EntryFixture> QUEUE = {
    {"Intro", "Sigur Rós"},
    {"Svefn-g-englar", "Sigur Rós"},
    {"Starálfur", "Sigur Rós"},
    {"Flugufrelsarinn", "Sigur Rós"},
    {"Ný batterí", "Sigur Rós"},
    {"Hjartað hamast (bamm bamm bamm)", "Sigur Rós"},
    {"Viðrar vel til loftárása", "Sigur Rós"},
    {"Olsen Olsen", "Sigur Rós"},
    {"Ágætis byrjun", "Sigur Rós"},
    {"Avalon", "Sigur Rós"},
    {"Near Light", "Ólafur Arnalds"},
    {"Orfeo ed Euridice, Act II: “Che puro ciel, che chiaro sol” – Ballet des ombres heureuses",
     "Anne Sofie von Otter, Les Musiciens du Louvre, Marc Minkowski"},
    {"Run‐away♫", "Jay‐Z"},
};

// The place of the loaded track in QUEUE, near its end: the screen opens scrolled to the end.
static constexpr uint32_t QUEUE_CURRENT = 11;

// How many entries the queue has in the scenes that page, and where its loaded track is: in the
// middle of the second page, and in the third.
static constexpr size_t TWO_PAGES_OF_ENTRIES = 150;
static constexpr uint32_t MID_PAGE_CURRENT = 140;
static constexpr size_t THREE_PAGES_OF_ENTRIES = 250;
static constexpr uint32_t THIRD_PAGE_CURRENT = 230;

// Entries enough to page through, as an artist's albums queued whole are, each named for its place
// in the queue from 1.
static std::vector<EntryFixture> make_long_queue(size_t count) {
    static const char* const ARTISTS[] = {"Sigur Rós", "Ólafur Arnalds", "Hania Rani"};

    std::vector<EntryFixture> entries;
    for (size_t i = 0; i < count; i++) {
        char title[32];
        snprintf(title, sizeof(title), "Track %03zu", i + 1);
        entries.emplace_back(title, ARTISTS[i * 3 / count]);
    }

    return entries;
}

// The answer to GET /api/speakers/{speaker_id}/queue for page `number` of `entries`, its data the
// `count` entries from `first`, with the loaded track at `current`. Every fourth entry is not from
// the Plex library: it has no album, picture or track id.
static std::string make_queue_body(const std::vector<EntryFixture>& entries, size_t first, size_t count,
                                   uint32_t number, std::optional<uint32_t> current) {
    const auto data = cJSON_CreateArray();
    for (auto i = first; i < first + count; i++) {
        const auto entry = cJSON_CreateObject();
        const auto from_plex = i % 4 != 3;

        if (from_plex) {
            cJSON_AddStringToObject(entry, "album", "Ágætis byrjun");
            cJSON_AddStringToObject(entry, "artwork_id", ("dHJhY2sv" + std::to_string(i)).c_str());
        } else {
            cJSON_AddNullToObject(entry, "album");
            cJSON_AddNullToObject(entry, "artwork_id");
        }
        cJSON_AddStringToObject(entry, "artist", entries[i].second.c_str());
        cJSON_AddNumberToObject(entry, "duration_ms", double(240000 + i * 1000));
        cJSON_AddNumberToObject(entry, "position", double(i + 1));
        cJSON_AddStringToObject(entry, "title", entries[i].first.c_str());
        if (from_plex) {
            cJSON_AddStringToObject(entry, "track_id", std::to_string(5000 + i).c_str());
        } else {
            cJSON_AddNullToObject(entry, "track_id");
        }
        cJSON_AddItemToArray(data, entry);
    }

    const auto root = cJSON_Parse(make_page_body(data, number, entries.size(), "position", "asc").c_str());
    if (current) {
        cJSON_AddNumberToObject(root, "current_position", *current);
    } else {
        cJSON_AddNullToObject(root, "current_position");
    }

    const auto text = cJSON_PrintUnformatted(root);
    std::string body(text);
    cJSON_free(text);
    cJSON_Delete(root);

    return body;
}

// Every page of `entries`, as the backend answers each, with the loaded track at `current`, and
// after them `past` pages past the queue's end, as a queue grown shorter answers them.
static std::vector<std::string> make_queue_pages(const std::vector<EntryFixture>& entries,
                                                 std::optional<uint32_t> current, size_t past = 0) {
    std::vector<std::string> pages;
    for (size_t first = 0; first < entries.size() || pages.empty(); first += PAGE_SIZE) {
        pages.push_back(make_queue_body(entries, first, std::min(size_t(PAGE_SIZE), entries.size() - first),
                                        uint32_t(pages.size() + 1), current));
    }
    for (size_t i = 0; i < past; i++) {
        pages.push_back(make_queue_body(entries, 0, 0, uint32_t(pages.size() + 1), current));
    }

    return pages;
}

// A listing the backend could not finish: Plex did not give the pictures of the page's tracks.
static const char ERROR_PLEX_PICTURES[] =
    R"({"code":null,"correlationId":null,"details":{"message":"The request could not be completed"},)"
    R"("error":"Plex failed to give the pictures of the page's tracks"})";
static const char PLEX_PICTURES[] = "Plex failed to give the pictures of the page's tracks";

// Pages of the queue the API's document does not allow, by its current_position or an entry.
static const char* const UNREADABLE_QUEUES[] = {
    R"({"data":[],"meta":{"page":1,"per_page":100,"total":0}})",
    R"({"current_position":0,"data":[],"meta":{"page":1,"per_page":100,"total":0}})",
    R"({"current_position":-1,"data":[],"meta":{"page":1,"per_page":100,"total":0}})",
    R"({"current_position":1.5,"data":[],"meta":{"page":1,"per_page":100,"total":0}})",
    R"({"current_position":"1","data":[],"meta":{"page":1,"per_page":100,"total":0}})",
    R"({"current_position":1e30,"data":[],"meta":{"page":1,"per_page":100,"total":0}})",
    R"({"current_position":null,"data":["Intro"],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"current_position":null,"data":[{"artist":"Sigur Rós"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"current_position":null,"data":[{"artist":"Sigur Rós","title":null}],)"
    R"("meta":{"page":1,"per_page":100,"total":1}})",
    R"({"current_position":null,"data":[{"title":"Intro"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"current_position":null,"data":[{"artist":7,"title":"Intro"}],"meta":{"page":1,"per_page":100,"total":1}})",
    R"({"current_position":null,"data":[{"artist":"Sigur Rós","title":"Intro"}],)"
    R"("meta":{"page":1,"per_page":100,"total":0}})",
};

// The answers to GET /api/speakers/{speaker_id}/queue as the device reads them: what is not shown
// is not read, a position past the queue's end is taken, and so is a page past it, as is one of
// the library's lists; what the API's document does not allow is refused whole.
static void expect_queue_reader_rules() {
    static const char ALLOWED[] =
        R"({"current_position":null,"data":[{"album":null,"artist":"Sigur Rós","artwork_id":null,)"
        R"("duration_ms":null,"position":1,"title":"Intro","track_id":null}],)"
        R"("meta":{"page":1,"per_page":100,"sort_by":"position","sort_dir":"asc","total":1,"total_pages":1}})";
    const QueueEntry intro{.title = "Intro", .artist = "Sigur Rós"};
    QueuePage page;
    expect(!read_queue(200, ALLOWED, strlen(ALLOWED), page) && page.state == LoadState::Loaded &&
               page.items.size() == 1 && page.items[0] == intro && !page.current_position,
           "a page of the queue was refused for what the display does not show");

    static const char PAST_ITS_END[] =
        R"({"current_position":40,"data":[],"meta":{"page":3,"per_page":100,"total":12}})";
    page = {};
    expect(!read_queue(200, PAST_ITS_END, strlen(PAST_ITS_END), page) && page.state == LoadState::Loaded &&
               page.items.empty() && page.number == 3 && page.total == 12 && page.current_position == 40u,
           "a page past the queue's end, or a position past it, was refused");

    static const char ARTISTS_PAST_THEIR_END[] = R"({"data":[],"meta":{"page":3,"per_page":100,"total":150}})";
    Page<Artist> artists;
    expect(!read_artists(200, ARTISTS_PAST_THEIR_END, strlen(ARTISTS_PAST_THEIR_END), artists) &&
               artists.state == LoadState::Loaded && artists.items.empty(),
           "a page past the end of a list of the library's was refused");

    for (const auto body : UNREADABLE_QUEUES) {
        page = {};
        expect(read_queue(200, body, strlen(body), page) && page.state == LoadState::Failed && page.items.empty() &&
                   page.error.empty(),
               "an unreadable page of the queue was not refused");
    }

    page = {};
    expect(read_queue(502, ERROR_PLEX_PICTURES, strlen(ERROR_PLEX_PICTURES), page) && page.state == LoadState::Failed &&
               page.error == PLEX_PICTURES,
           "a listing that failed is not read with the backend's words");
}

// Where the now-playing screen's queue button is.
static constexpr lv_point_t QUEUE_BUTTON = {648, 56};

static const char SPEAKER_EVENT[] = R"({"speaker_id":"speaker-garage"})";
static const char OTHER_SPEAKER_EVENT[] = R"({"speaker_id":"speaker-kitchen"})";

// The queue as the now-playing screen opens it: `entries`, with the loaded track at `current`,
// which the answer at the loaded track holds.
static void open_queue(SimHost& host, Shell& shell, const std::vector<EntryFixture>& entries,
                       std::optional<uint32_t> current) {
    host.queue().serve(200, make_queue_pages(entries, current));
    if (current) {
        host.set_queue_current_page((*current - 1) / PAGE_SIZE + 1);
    }
    open_now_playing(host, shell, PLAYING);

    shell.open_queue();
}

static const char* check_queue_screen(const Shell& shell) {
    if (const auto problem = check_screen<QueueView>(shell, 2)) {
        return problem;
    }

    return check_heading(MSG_QUEUE);
}

// The queue was asked for as `pages` and `locates` say, each for the chosen speaker's group.
static const char* check_queue_asked(const SimHost& host, const std::vector<uint32_t>& pages,
                                     const std::vector<bool>& locates) {
    if (host.queue().get_requests() != pages || host.get_queue_locates() != locates) {
        return "the queue was not asked for at the loaded track first, and then by its pages";
    }

    const auto& ids = host.queue().get_parent_ids();
    return std::all_of(ids.begin(), ids.end(), [](const auto& id) { return id == "speaker-garage"; })
               ? nullptr
               : "the queue of another speaker than the chosen one was asked for";
}

// The rows of the list are the entries of `entries` from `first`, `count` of them, each its title
// and its artist.
static const char* check_queue_rows(const std::vector<EntryFixture>& entries, size_t first, size_t count) {
    std::vector<std::string> titles;
    for (const auto& entry : entries) {
        titles.push_back(entry.first);
    }
    if (const auto problem = check_titles(titles, first, count)) {
        return problem;
    }

    const auto rows = get_rows(find_list());
    for (size_t i = 0; i < count; i++) {
        if (lv_obj_get_child_count(rows[i]) != 2 ||
            !shows_title(lv_label_get_text(lv_obj_get_child(rows[i], 1)), entries[first + i].second)) {
            return "a row does not show its entry's artist";
        }
    }

    return nullptr;
}

// The row of the loaded track, `title`, is the one row marked, in the accent, and is in view, at
// the list's top where `at_top`; with no title, no row is marked.
static const char* check_marked(const char* title, bool at_top = false) {
    const auto list = find_list();
    if (!list) {
        return "the queue is not listed";
    }

    lv_obj_t* marked = nullptr;
    for (const auto row : get_rows(list)) {
        if (lv_obj_get_style_border_width(row, LV_PART_MAIN)) {
            if (marked) {
                return "two rows are marked";
            }
            marked = row;
        }
    }

    if (!title) {
        return marked ? "a row is marked while no track of the queue is loaded" : nullptr;
    }
    if (!marked || !shows_title(get_row_title(marked), title)) {
        return "the loaded track's row is not marked";
    }
    if (!lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(marked, 0), LV_PART_MAIN),
                     Theme::color(Theme::ACCENT))) {
        return "the loaded track's title is not in the accent";
    }

    lv_obj_update_layout(lv_screen_active());
    lv_area_t shown;
    lv_obj_get_coords(list, &shown);
    lv_area_t row;
    lv_obj_get_coords(marked, &row);
    if (row.y1 < shown.y1 || row.y2 > shown.y2) {
        return "the loaded track's row is not in view";
    }

    return !at_top || row.y1 == shown.y1 ? nullptr : "the loaded track's row is not at the top of the list";
}

// The now-playing screen's queue button opens the queue of the chosen speaker's group over it, at
// the loaded track, whose row is marked; near the queue's end the list shows its end. Each row is
// its entry's title and artist, the longer ones cut.
static void queue_screen(SimHost& host, Shell& shell) {
    expect_queue_reader_rules();

    host.queue().serve(200, make_queue_pages(QUEUE, QUEUE_CURRENT));
    host.set_queue_current_page(1);
    open_now_playing(host, shell, PLAYING);

    host.tap(QUEUE_BUTTON.x, QUEUE_BUTTON.y);
}

static const char* check_queue_screen_open(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_queue_asked(host, {1}, {true})) {
        return problem;
    }
    if (const auto problem = check_queue_rows(QUEUE, 0, QUEUE.size())) {
        return problem;
    }
    if (lv_obj_get_scroll_bottom(find_list()) != 0) {
        return "the list does not show its end, where the loaded track is";
    }
    if (says(MSG_QUEUE_ELSEWHERE) || !says(MSG_CLEAR)) {
        return "the queue that plays says it does not, or offers no Clear";
    }

    return check_marked("Near Light");
}

// The loaded track in the middle of the second page: the screen opens on that page, the track's
// row at the top.
static void queue_screen_mid_page(SimHost& host, Shell& shell) {
    open_queue(host, shell, make_long_queue(TWO_PAGES_OF_ENTRIES), MID_PAGE_CURRENT);
}

static const char* check_queue_screen_mid_page(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_queue_asked(host, {2}, {true})) {
        return problem;
    }
    if (const auto problem = check_queue_rows(make_long_queue(TWO_PAGES_OF_ENTRIES), 100, 50)) {
        return problem;
    }

    return check_marked("Track 140", true);
}

// A queue of more than 200, opened in its third page, scrolled to its start: the pages before
// come one by one, and the third goes as the first comes.
static void queue_screen_long(SimHost& host, Shell& shell) {
    open_queue(host, shell, make_long_queue(THREE_PAGES_OF_ENTRIES), THIRD_PAGE_CURRENT);
    shell.update();
    expect(!check_marked("Track 230", true), "the queue did not open at its loaded track");

    expect_in_place(scroll_list(shell, false), "the row at the top moved as the second page came");
    expect_in_place(scroll_list(shell, false), "the row at the top moved as the first page came");
}

static const char* check_queue_screen_long(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_queue_asked(host, {3, 2, 1}, {true, false, false})) {
        return problem;
    }

    return check_queue_rows(make_long_queue(THREE_PAGES_OF_ENTRIES), 0, 200);
}

static void queue_screen_empty(SimHost& host, Shell& shell) { open_queue(host, shell, {}, std::nullopt); }

static const char* check_queue_screen_empty(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (!says(MSG_QUEUE_EMPTY) || find_list()) {
        return "the empty queue is not said to be empty";
    }

    return says(MSG_CLEAR) || says(MSG_QUEUE_ELSEWHERE) ? "the empty queue offers Clear, or says more" : nullptr;
}

// The group plays from another source: no row is marked, and a line under the heading says so.
static void queue_screen_elsewhere(SimHost& host, Shell& shell) { open_queue(host, shell, QUEUE, std::nullopt); }

static const char* check_queue_screen_elsewhere(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_queue_rows(QUEUE, 0, QUEUE.size())) {
        return problem;
    }
    if (!says(MSG_QUEUE_ELSEWHERE) || !says(MSG_CLEAR)) {
        return "the screen does not say that the group plays from another source";
    }

    return check_marked(nullptr);
}

// The listing answered 502: the screen says so, in the backend's words, and the bar under it stays
// live.
static void queue_screen_error(SimHost& host, Shell& shell) {
    host.queue().serve(502, {ERROR_PLEX_PICTURES});
    open_now_playing(host, shell, PLAYING);

    shell.open_queue();
}

static const char* check_queue_screen_error(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_says_failed()) {
        return problem;
    }

    return says(PLEX_PICTURES) ? nullptr : "the screen does not say what the backend said";
}

// Try again, once the backend answers: the queue is asked for at the loaded track again.
static void queue_screen_retry(SimHost& host, Shell& shell) {
    queue_screen_error(host, shell);

    host.queue().serve(200, make_queue_pages(QUEUE, QUEUE_CURRENT));
    host.set_queue_current_page(1);
    tap_label(host, shell, MSG_TRY_AGAIN);
}

static const char* check_queue_screen_retry(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_queue_asked(host, {1, 1}, {true, true})) {
        return problem;
    }

    return check_marked("Near Light");
}

static void queue_screen_unreachable(SimHost& host, Shell& shell) {
    host.queue().serve(SimHost::UNREACHABLE, {""});
    open_now_playing(host, shell, PLAYING);

    shell.open_queue();
}

static const char* check_queue_screen_unreachable(const SimHost&, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }

    return check_says_unreachable();
}

static void queue_screen_unreadable(SimHost& host, Shell& shell) {
    host.queue().serve(200, {UNREADABLE_QUEUES[0]});
    open_now_playing(host, shell, PLAYING);

    shell.open_queue();
}

static const char* check_queue_screen_unreadable(const SimHost&, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }

    return check_says_failed();
}

static void queue_screen_loading(SimHost& host, Shell& shell) {
    open_now_playing(host, shell, PLAYING);

    shell.open_queue();
}

static const char* check_queue_screen_loading(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (!says(MSG_QUEUE_LOADING) || count_spinners(lv_screen_active()) != 1) {
        return "the screen does not say that the queue is on its way";
    }

    return check_queue_asked(host, {1}, {true});
}

// A page before the rows does not come, with a 502: the list's start says so in the backend's
// words, with a way to ask again.
static void queue_screen_page_error(SimHost& host, Shell& shell) {
    const auto pages = make_queue_pages(make_long_queue(THREE_PAGES_OF_ENTRIES), THIRD_PAGE_CURRENT);
    host.set_queue_current_page(3);
    open_now_playing(host, shell, PLAYING);
    shell.open_queue();
    host.queue().answer(200, pages[2].c_str());
    shell.update();

    scroll_list(shell, false);
    host.queue().answer(502, ERROR_PLEX_PICTURES);
    shell.update();
    scroll_list(shell, false);
}

static const char* check_queue_screen_page_error(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_queue_asked(host, {3, 2}, {true, false})) {
        return problem;
    }
    if (!says(MSG_BACKEND_FAILED) || !says(PLEX_PICTURES) || !says(MSG_TRY_AGAIN)) {
        return "the list's start does not say why the page did not come, in the backend's words";
    }

    return check_queue_rows(make_long_queue(THREE_PAGES_OF_ENTRIES), 200, 50);
}

// While the queue shows, the events of the chosen speaker ask for it again, a burst of them once,
// and those of another speaker do not: the entry added shows, and the next track is marked.
static void queue_screen_changed(SimHost& host, Shell& shell) {
    open_queue(host, shell, QUEUE, QUEUE_CURRENT);
    shell.update();

    host.send_speaker_event("queue_changed", OTHER_SPEAKER_EVENT);
    host.send_speaker_event("playback_changed", OTHER_SPEAKER_EVENT);
    shell.update();
    expect(host.queue().get_requests().size() == 1, "an event of another speaker asked for the queue again");

    auto changed = QUEUE;
    changed.emplace_back("Stopp", "Eivør");
    host.queue().serve(200, make_queue_pages(changed, QUEUE_CURRENT + 1));
    host.send_speaker_event("queue_changed", SPEAKER_EVENT);
    host.send_speaker_event("queue_changed", SPEAKER_EVENT);
    host.send_speaker_event("playback_changed", SPEAKER_EVENT);
}

static const char* check_queue_screen_changed(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_queue_asked(host, {1, 1}, {true, false})) {
        return problem;
    }

    auto changed = QUEUE;
    changed.emplace_back("Stopp", "Eivør");
    if (const auto problem = check_queue_rows(changed, 0, changed.size())) {
        return problem;
    }

    return check_marked(changed[QUEUE_CURRENT].first.c_str());
}

// Two pages held, the second and the third, the row at the top in the third: an event asks for the
// third again, which takes the place of both, and that row stays where it is.
static void queue_screen_changed_pages(SimHost& host, Shell& shell) {
    open_queue(host, shell, make_long_queue(THREE_PAGES_OF_ENTRIES), THIRD_PAGE_CURRENT);
    shell.update();
    scroll_list(shell, false);

    const auto row = find_row("Track 205");
    expect(row, "the third page's rows are not listed");
    lv_obj_scroll_to_y(find_list(), lv_obj_get_y(row), LV_ANIM_OFF);
    shell.update();
    lv_obj_update_layout(lv_screen_active());
    expect(get_place(row).top == LIST_TOP, "the row to keep in place is not at the top");

    host.send_speaker_event("queue_changed", SPEAKER_EVENT);
}

static const char* check_queue_screen_changed_pages(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_queue_asked(host, {3, 2, 3}, {true, false, false})) {
        return problem;
    }

    const auto row = find_row("Track 205");
    if (!row || get_place(row).top != LIST_TOP) {
        return "the row at the top moved as its page came again";
    }

    return check_queue_rows(make_long_queue(THREE_PAGES_OF_ENTRIES), 200, 50);
}

// How many entries the queue has after a Play replaced the queue of 250 with a shorter one, and
// where its loaded track is: in its last page, the one before the third.
static constexpr size_t SHORTER_QUEUE = 120;
static constexpr uint32_t SHORTER_QUEUE_CURRENT = 110;

// A Play replaced the queue of 250 while its third page showed: that page, asked for again, is past
// the new queue's end, and the page at the loaded track takes its place, that track's row at the
// top.
static void queue_screen_shrunk(SimHost& host, Shell& shell) {
    open_queue(host, shell, make_long_queue(THREE_PAGES_OF_ENTRIES), THIRD_PAGE_CURRENT);
    shell.update();

    host.queue().serve(200, make_queue_pages(make_long_queue(SHORTER_QUEUE), SHORTER_QUEUE_CURRENT, 1));
    host.set_queue_current_page(2);
    host.send_speaker_event("queue_changed", SPEAKER_EVENT);
}

static const char* check_queue_screen_shrunk(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_queue_asked(host, {3, 3, 2}, {true, false, true})) {
        return problem;
    }
    if (const auto problem = check_queue_rows(make_long_queue(SHORTER_QUEUE), 100, 20)) {
        return problem;
    }

    return check_marked("Track 110", true);
}

// The answer at the loaded track is past the new queue's end too, its position read before the
// queue changed: the queue's last page takes the place of the pages held, and the page before it,
// within a screen of the list's start, comes beside it.
static void queue_screen_shrunk_early(SimHost& host, Shell& shell) {
    open_queue(host, shell, make_long_queue(THREE_PAGES_OF_ENTRIES), THIRD_PAGE_CURRENT);
    shell.update();

    host.queue().serve(200, make_queue_pages(make_long_queue(SHORTER_QUEUE), THIRD_PAGE_CURRENT, 1));
    host.send_speaker_event("queue_changed", SPEAKER_EVENT);
}

static const char* check_queue_screen_shrunk_early(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_queue_asked(host, {3, 3, 3, 2, 1}, {true, false, true, false, false})) {
        return problem;
    }

    return check_queue_rows(make_long_queue(SHORTER_QUEUE), 0, SHORTER_QUEUE);
}

// Clear at the heading's end asks, over the list, to be confirmed.
static void queue_screen_clear_confirm(SimHost& host, Shell& shell) {
    open_queue(host, shell, QUEUE, QUEUE_CURRENT);

    tap_label(host, shell, MSG_CLEAR);
}

static const char* check_queue_screen_clear_confirm(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (!says(MSG_CLEAR_QUEUE) || !says(MSG_CLEAR_QUEUE_STOPS) || !says(MSG_CANCEL)) {
        return "Clear did not ask to be confirmed";
    }

    return host.get_clears().empty() ? nullptr : "Clear cleared the queue before it was confirmed";
}

// The confirmation's Clear, which shows over the list once Clear at the heading's end is touched,
// and which is the second label that says so.
static void tap_confirm_clear(SimHost& host, Shell& shell) {
    shell.update();
    lv_obj_update_layout(lv_screen_active());

    std::vector<lv_point_t> found;
    std::function<void(lv_obj_t*)> find = [&found, &find](lv_obj_t* obj) {
        if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), MSG_CLEAR)) {
            lv_area_t area;
            lv_obj_get_coords(lv_obj_get_parent(obj), &area);
            found.push_back(lv_point_t{(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2});
        }
        for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
            find(lv_obj_get_child(obj, int32_t(i)));
        }
    };
    find(lv_screen_active());
    expect(found.size() == 2, "the confirmation has no Clear");

    host.tap(found[1].x, found[1].y);
}

// Clear, confirmed: the request goes for the chosen speaker's group, and the queue's events, which
// follow it, ask for the queue again, which is empty.
static void queue_screen_clear(SimHost& host, Shell& shell) {
    host.answer_clears(204, "");
    open_queue(host, shell, QUEUE, QUEUE_CURRENT);

    tap_label(host, shell, MSG_CLEAR);
    tap_confirm_clear(host, shell);
    host.answer_after(after_touches_ms(2) - 200, [&host]() {
        host.queue().serve(200, make_queue_pages({}, std::nullopt));
        host.send_speaker_event("queue_changed", SPEAKER_EVENT);
        host.send_speaker_event("playback_changed", SPEAKER_EVENT);
    });
}

static const char* check_queue_screen_clear(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (host.get_clears() != std::vector<std::string>{"speaker-garage"}) {
        return "the confirmed Clear did not clear the chosen speaker's queue, once";
    }
    if (const auto problem = check_queue_asked(host, {1, 1}, {true, false})) {
        return problem;
    }
    if (!says(MSG_QUEUE_EMPTY) || says(MSG_CLEAR_QUEUE) || says(MSG_CLEAR)) {
        return "the cleared queue is not said to be empty, or the confirmation or Clear still shows";
    }

    return host.get_model().clears_not_taken || says(MSG_COMMAND_NOT_TAKEN) ? "the Clear taken was said not to be"
                                                                            : nullptr;
}

// Cancel closes the confirmation, and nothing is cleared.
static void queue_screen_clear_cancel(SimHost& host, Shell& shell) {
    open_queue(host, shell, QUEUE, QUEUE_CURRENT);

    tap_label(host, shell, MSG_CLEAR);
    tap_label(host, shell, MSG_CANCEL, true);
}

static const char* check_queue_screen_clear_cancel(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (says(MSG_CLEAR_QUEUE) || !host.get_clears().empty()) {
        return "Cancel did not close the confirmation, or the queue was cleared";
    }

    return check_marked("Near Light");
}

// The backend refuses the Clear: the confirmation closes at the press, the bar says it did not go
// through, and the queue stays listed.
static void queue_screen_clear_not_taken(SimHost& host, Shell& shell) {
    host.answer_clears(502, ERROR_ENVELOPE);
    open_queue(host, shell, QUEUE, QUEUE_CURRENT);

    tap_label(host, shell, MSG_CLEAR);
    tap_confirm_clear(host, shell);
}

static const char* check_queue_screen_clear_not_taken(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (host.get_clears().size() != 1 || host.get_model().clears_not_taken != 1) {
        return "the Clear refused was not counted once as not taken";
    }
    if (says(MSG_CLEAR_QUEUE)) {
        return "the confirmation did not close at the press";
    }
    if (!says(MSG_COMMAND_NOT_TAKEN)) {
        return "the bar does not say that the Clear did not go through";
    }

    return check_queue_rows(QUEUE, 0, QUEUE.size());
}

// The bar under the queue brings the now-playing screen back to the top, from its cover and from
// its texts.
static void queue_screen_bar_cover(SimHost& host, Shell& shell) {
    open_queue(host, shell, QUEUE, QUEUE_CURRENT);

    host.tap(BAR_COVER.x, BAR_COVER.y);
}

static void queue_screen_bar_texts(SimHost& host, Shell& shell) {
    open_queue(host, shell, QUEUE, QUEUE_CURRENT);

    host.tap(BAR_TEXTS.x, BAR_TEXTS.y);
}

// The "Reconnecting" marker at the heading's right: Clear ends a gap before it.
static void queue_screen_reconnecting(SimHost& host, Shell& shell) {
    host.model().stream_state = StreamState::Disconnected;
    host.model().stream_down_since = lv_tick_get() - 10 * 1000;

    open_queue(host, shell, QUEUE, QUEUE_CURRENT);
}

static const char* check_queue_screen_reconnecting(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_marker(true)) {
        return problem;
    }

    std::vector<lv_obj_t*> clear;
    find_labels(lv_screen_active(), MSG_CLEAR, clear);
    std::vector<lv_obj_t*> marker;
    find_labels(lv_screen_active(), MSG_RECONNECTING, marker);
    if (clear.size() != 1) {
        return "Clear does not show";
    }

    lv_area_t clear_area;
    lv_obj_get_coords(lv_obj_get_parent(clear[0]), &clear_area);
    lv_area_t marker_area;
    lv_obj_get_coords(lv_obj_get_parent(marker[0]), &marker_area);

    return clear_area.x2 + 1 + Theme::GAP <= marker_area.x1 ? nullptr : "Clear runs under the marker";
}

// The event stream is lost while the queue shows, and the group goes on to the next track before it
// is back: that track's event is gone, and the queue, asked for again once the stream is back,
// marks it.
static void queue_screen_reconnected(SimHost& host, Shell& shell) {
    open_queue(host, shell, QUEUE, QUEUE_CURRENT);

    host.answer_after(SimHost::TAP_INTERVAL_MS, [&host]() {
        host.model().stream_state = StreamState::Disconnected;
        host.model().stream_down_since = lv_tick_get();
        host.model().revision++;
    });
    host.answer_after(2 * SimHost::TAP_INTERVAL_MS, [&host]() {
        host.queue().serve(200, make_queue_pages(QUEUE, QUEUE_CURRENT + 1));
        host.model().stream_state = StreamState::Connected;
        host.model().revision++;
    });
}

static const char* check_queue_screen_reconnected(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (const auto problem = check_queue_asked(host, {1, 1}, {true, false})) {
        return problem;
    }

    return check_marked(QUEUE[QUEUE_CURRENT].first.c_str());
}

// The queue opens while the event stream is away, and Clear is confirmed: its events are gone, and
// the queue, asked for again once the stream is back, is empty.
static void queue_screen_reconnected_clear(SimHost& host, Shell& shell) {
    host.model().stream_state = StreamState::Disconnected;
    host.model().stream_down_since = lv_tick_get() - 10 * 1000;
    host.answer_clears(204, "");
    open_queue(host, shell, QUEUE, QUEUE_CURRENT);

    tap_label(host, shell, MSG_CLEAR);
    tap_confirm_clear(host, shell);
    host.answer_after(after_touches_ms(2) - 200, [&host]() {
        host.queue().serve(200, make_queue_pages({}, std::nullopt));
        host.model().stream_state = StreamState::Connected;
        host.model().revision++;
    });
}

static const char* check_queue_screen_reconnected_clear(const SimHost& host, const Shell& shell) {
    if (const auto problem = check_queue_screen(shell)) {
        return problem;
    }
    if (host.get_clears() != std::vector<std::string>{"speaker-garage"}) {
        return "the confirmed Clear did not clear the chosen speaker's queue, once";
    }
    if (const auto problem = check_queue_asked(host, {1, 1}, {true, false})) {
        return problem;
    }

    return says(MSG_QUEUE_EMPTY) && !says(MSG_CLEAR) ? nullptr : "the cleared queue is not said to be empty";
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
        {"speakers-glyphs", speakers_glyphs, check_bar},
        {"speakers-loading", speakers_loading},
        {"speakers-empty", speakers_empty, check_empty},
        {"speakers-unreachable", speakers_unreachable, check_notice_over_bar},
        {"speakers-error", speakers_error, check_notice_over_bar},
        {"speakers-unreadable", speakers_unreadable},
        {"speakers-retry", speakers_retry, check_retry},
        {"speakers-reconnecting", speakers_reconnecting},
        {"speakers-none-pick", speakers_none_pick, check_none_pick},
        {"speakers-missing-pick", speakers_missing_pick, check_missing_pick},
        {"speakers-group-alone", speakers_group_alone, check_group_alone},
        {"speakers-group-led", speakers_group_led, check_group_led},
        {"speakers-group-follows", speakers_group_follows, check_group_follows},
        {"speakers-group-add", speakers_group_add, check_group_add},
        {"speakers-group-remove", speakers_group_remove, check_group_remove},
        {"speakers-group-remove-leader", speakers_group_remove_leader, check_group_remove_leader},
        {"speakers-group-pending-elsewhere", speakers_group_pending_elsewhere, check_group_pending_elsewhere},
        {"speakers-group-scrolled", speakers_group_scrolled, check_group_scrolled},
        {"speakers-group-not-taken", speakers_group_not_taken, check_group_not_taken},
        {"speakers-group-not-taken-later", speakers_group_not_taken, check_group_not_taken_later, AFTER_NOTICE_MS},
        {"speakers-group-time-limit", speakers_group_time_limit, check_group_time_limit,
         Grouping::TIME_LIMIT_MS + 1000},
        {"speakers-group-no-answer", speakers_group_no_answer, check_group_no_answer, ANSWER_TIME_LIMIT_MS + 1000},
        {"speakers-group-late-answer", speakers_group_late_answer, check_group_late_answer,
         ANSWER_TIME_LIMIT_MS + 1000},
        {"now-playing", now_playing, check_now_playing},
        {"now-playing-glyphs", now_playing_glyphs, check_now_playing},
        {"now-playing-paused", now_playing_paused},
        {"now-playing-radio", now_playing_radio, check_radio},
        {"now-playing-nothing", now_playing_nothing},
        {"now-playing-press", now_playing_press, check_press},
        {"now-playing-next", now_playing_next, check_next},
        {"now-playing-refused", now_playing_refused, check_refused_said},
        {"now-playing-refused-later", now_playing_refused, check_refused_later, AFTER_NOTICE_MS},
        {"now-playing-refused-back", now_playing_refused_back, check_now_playing_refused_back, after_touches_ms(3)},
        {"now-playing-loading", now_playing_loading, check_loading},
        {"now-playing-unreachable", now_playing_unreachable, check_now_playing},
        {"now-playing-error", now_playing_error},
        {"now-playing-unreadable", now_playing_unreadable},
        {"now-playing-retry", now_playing_retry, check_playback_retry},
        {"now-playing-reconnecting", now_playing_reconnecting, check_now_playing_reconnecting},
        {"now-playing-speakers", now_playing_speakers, check_speakers_open},
        {"now-playing-same-speaker", now_playing_same_speaker, check_same_speaker},
        {"now-playing-other-speaker", now_playing_other_speaker, check_other_speaker},
        {"now-playing-volume-up", now_playing_volume_up, check_volume_up},
        {"now-playing-volume-burst", now_playing_volume_burst, check_volume_burst},
        {"now-playing-volume-muted", now_playing_volume_muted},
        {"now-playing-volume-limit", now_playing_volume_limit, check_volume_limit},
        {"now-playing-volume-unmute", now_playing_volume_unmute, check_volume_unmute},
        {"now-playing-volume-unmute-loudest", now_playing_volume_unmute_loudest, check_volume_unmute_loudest},
        {"now-playing-volume-muted-down", now_playing_volume_muted_down, check_volume_muted_down},
        {"now-playing-volume-refused", now_playing_volume_refused, check_volume_refused},
        {"now-playing-volume-unknown", now_playing_volume_unknown, check_volume_unknown},
        {"now-playing-volume-blind", now_playing_volume_blind, check_volume_blind},
        {"now-playing-volume-no-answer", now_playing_volume_no_answer, check_volume_no_answer,
         ANSWER_TIME_LIMIT_MS + 1000},
        {"now-playing-volume-late-answer", now_playing_volume_late_answer, check_volume_late_answer,
         ANSWER_TIME_LIMIT_MS + 1000},
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
        {"bar-volume-unmute", bar_volume_unmute, check_bar_volume_unmute},
        {"bar-volume-unmute-loudest", bar_volume_unmute_loudest, check_bar_volume_unmute_loudest},
        {"bar-volume-muted-down", bar_volume_muted_down, check_bar_volume_muted_down},
        {"bar-volume-refused", bar_volume_refused, check_bar_volume_refused},
        {"bar-cover-change", bar_cover_change, check_bar_cover_change},
        {"bar-open-cover", bar_open_cover, check_bar_opened},
        {"bar-open-texts", bar_open_texts, check_bar_opened},
        {"bar-open-failed-list", bar_open_failed_list, check_bar_opened},
        {"bar-open-not-root", bar_open_not_root, check_bar_open_not_root, after_touches_ms(3)},
        {"bar-controls", bar_controls, check_bar_controls, after_touches_ms(5)},
        {"library-open", library_open, check_library_open},
        {"artists", artists, check_artists},
        {"artists-letters", artists_letters, check_artists_letters},
        {"artists-jump", artists_jump, check_artists_jump, after_touches_ms(2)},
        {"artists-jump-end", artists_jump_end, check_artists_jump_end, after_touches_ms(2)},
        {"artists-loading", artists_loading, check_artists_loading},
        {"artists-unreachable", artists_unreachable, check_artists_unreachable},
        {"artists-error", artists_error, check_artists_error},
        {"artists-unreadable", artists_unreadable, check_artists_error},
        {"artists-empty", artists_empty, check_artists_empty},
        {"artists-retry", artists_retry, check_artists_retry},
        {"artists-pages", artists_pages, check_artists_pages},
        {"artists-pages-back", artists_pages_back, check_artists_pages_back},
        {"artists-page-failed", artists_page_failed, check_artists_page_failed},
        {"artists-page-retry", artists_page_retry, check_artists_page_retry},
        {"artists-page-late", artists_page_late, check_artists_page_late, after_touches_ms(3)},
        {"artists-page-late-again", artists_page_late_again, check_artists_page_late_again},
        {"artists-page-late-start", artists_page_late_start, check_artists_page_late_start},
        {"artists-page-late-ask", artists_page_late_ask, check_artists_page_late_ask},
        {"artists-page-unbuilt", artists_page_unbuilt, check_artists_page_unbuilt},
        {"artists-back", artists_back, check_artists_back},
        {"artists-bar-cover", artists_bar_cover, check_bar_opened},
        {"artists-bar-texts", artists_bar_texts, check_bar_opened},
        {"artists-reconnecting", artists_reconnecting, check_artists_reconnecting},
        {"library-open-reconnecting", library_open_reconnecting, check_library_open_reconnecting},
        {"artists-pages-open", artists_pages_open, check_artists_pages_open},
        {"albums", albums, check_albums},
        {"albums-loading", albums_loading, check_albums_loading},
        {"albums-unreachable", albums_unreachable, check_albums_unreachable},
        {"albums-error", albums_error, check_albums_error},
        {"albums-unreadable", albums_unreadable, check_albums_error},
        {"albums-empty", albums_empty, check_albums_empty},
        {"albums-end", albums_end, check_albums_end},
        {"albums-back", albums_back, check_albums_back, after_touches_ms(2)},
        {"albums-long-name", albums_long_name, check_albums_long_name},
        {"albums-reconnecting", albums_reconnecting, check_albums_reconnecting},
        {"albums-reconnected", albums_reconnected, check_albums_reconnected, after_touches_ms(2)},
        {"tracks", tracks, check_tracks, after_touches_ms(2)},
        {"tracks-pages", tracks_pages, check_tracks_pages},
        {"tracks-loading", tracks_loading, check_tracks_loading},
        {"tracks-unreachable", tracks_unreachable, check_tracks_unreachable},
        {"tracks-error", tracks_error, check_tracks_error},
        {"tracks-unreadable", tracks_unreadable, check_tracks_error},
        {"tracks-empty", tracks_empty, check_tracks_empty},
        {"tracks-back", tracks_back, check_tracks_back, after_touches_ms(3)},
        {"tracks-bar-texts", tracks_bar_texts, check_bar_opened, after_touches_ms(3)},
        {"playlists", playlists, check_playlists},
        {"playlists-row", playlists_row, check_playlists_row, after_touches_ms(2)},
        {"playlists-artists-failed", playlists_artists_failed, check_playlists},
        {"playlists-loading", playlists_loading, check_playlists_loading},
        {"playlists-unreachable", playlists_unreachable, check_playlists_unreachable},
        {"playlists-error", playlists_error, check_playlists_error},
        {"playlists-unreadable", playlists_unreadable, check_playlists_error},
        {"playlists-empty", playlists_empty, check_playlists_empty},
        {"playlists-back", playlists_back, check_playlists_back, after_touches_ms(2)},
        {"artists-queue", artists_queue, check_artists_queue, after_touches_ms(3)},
        {"artists-queue-drag", artists_queue_drag, check_artists_queue_drag, 1000},
        {"albums-queue", albums_queue, check_albums_queue, after_touches_ms(3)},
        {"albums-long-title", albums_long_title, check_albums_long_title},
        {"tracks-queue", tracks_queue, check_tracks_queue, after_touches_ms(2)},
        {"playlists-queue", playlists_queue, check_playlists_queue, after_touches_ms(3)},
        {"queue-not-taken", queue_not_taken, check_queue_not_taken, after_touches_ms(1)},
        {"queue-failed", queue_failed, check_queue_failed, 800},
        {"queue-failed-early", queue_failed_early, check_queue_failed, 800},
        {"queue-failed-other", queue_failed_other, check_queue_failed_other, 800},
        {"queue-no-answer", queue_no_answer, check_queue_no_answer, ANSWER_TIME_LIMIT_MS + 1000},
        {"queue-failed-now-playing", queue_failed_now_playing, check_queue_failed_now_playing, 1000},
        {"queue-screen", queue_screen, check_queue_screen_open, after_touches_ms(1)},
        {"queue-screen-mid-page", queue_screen_mid_page, check_queue_screen_mid_page},
        {"queue-screen-long", queue_screen_long, check_queue_screen_long},
        {"queue-screen-empty", queue_screen_empty, check_queue_screen_empty},
        {"queue-screen-elsewhere", queue_screen_elsewhere, check_queue_screen_elsewhere},
        {"queue-screen-error", queue_screen_error, check_queue_screen_error},
        {"queue-screen-retry", queue_screen_retry, check_queue_screen_retry, after_touches_ms(1)},
        {"queue-screen-unreachable", queue_screen_unreachable, check_queue_screen_unreachable},
        {"queue-screen-unreadable", queue_screen_unreadable, check_queue_screen_unreadable},
        {"queue-screen-loading", queue_screen_loading, check_queue_screen_loading},
        {"queue-screen-page-error", queue_screen_page_error, check_queue_screen_page_error},
        {"queue-screen-changed", queue_screen_changed, check_queue_screen_changed},
        {"queue-screen-changed-pages", queue_screen_changed_pages, check_queue_screen_changed_pages},
        {"queue-screen-shrunk", queue_screen_shrunk, check_queue_screen_shrunk},
        {"queue-screen-shrunk-early", queue_screen_shrunk_early, check_queue_screen_shrunk_early},
        {"queue-screen-clear-confirm", queue_screen_clear_confirm, check_queue_screen_clear_confirm,
         after_touches_ms(1)},
        {"queue-screen-clear", queue_screen_clear, check_queue_screen_clear, after_touches_ms(2)},
        {"queue-screen-clear-cancel", queue_screen_clear_cancel, check_queue_screen_clear_cancel, after_touches_ms(2)},
        {"queue-screen-clear-not-taken", queue_screen_clear_not_taken, check_queue_screen_clear_not_taken,
         after_touches_ms(2)},
        {"queue-screen-bar-cover", queue_screen_bar_cover, check_bar_opened, after_touches_ms(1)},
        {"queue-screen-bar-texts", queue_screen_bar_texts, check_bar_opened, after_touches_ms(1)},
        {"queue-screen-reconnecting", queue_screen_reconnecting, check_queue_screen_reconnecting},
        {"queue-screen-reconnected", queue_screen_reconnected, check_queue_screen_reconnected, after_touches_ms(2)},
        {"queue-screen-reconnected-clear", queue_screen_reconnected_clear, check_queue_screen_reconnected_clear,
         after_touches_ms(2)},
    };

    return scenes;
}
