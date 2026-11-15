#include "scenes.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "LoadingView.h"
#include "Messages.h"
#include "SpeakerPickerView.h"

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

// A fixture that does not read as its scene says fails the render.
static void expect(bool condition, const char* what) {
    if (!condition) {
        fprintf(stderr, "render: %s\n", what);
        exit(1);
    }
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

    shell.replace(std::make_unique<SpeakerPickerView>(&host));
}

static void speakers_none(SimHost& host, Shell& shell) {
    answer_speakers(host);

    shell.replace(std::make_unique<SpeakerPickerView>(&host));
}

// The stored speaker is not in the list: nothing is marked, and the choice stays stored.
static void speakers_missing(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-shed";
    answer_speakers(host);

    shell.replace(std::make_unique<SpeakerPickerView>(&host));
}

static const char* check_missing(const SimHost& host) {
    return host.get_model().speaker_id == "speaker-shed" ? nullptr : "the list changed the stored choice";
}

// A touch on the third row picks its speaker.
static void speakers_pick(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-garage";
    answer_speakers(host);

    shell.replace(std::make_unique<SpeakerPickerView>(&host));

    host.tap(400, 360);
}

static const char* check_pick(const SimHost& host) {
    return host.get_model().speaker_id == "speaker-bathroom" ? nullptr : "the touch did not pick the third speaker";
}

static void speakers_many(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-guests";
    host.answer_speakers(200, SPEAKERS_MANY);
    expect(host.get_model().speakers.speakers.size() == 5, "the long speakers fixture does not hold five speakers");

    shell.replace(std::make_unique<SpeakerPickerView>(&host));
}

static void speakers_loading(SimHost& host, Shell& shell) { shell.replace(std::make_unique<SpeakerPickerView>(&host)); }

static void speakers_empty(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-library";
    host.answer_speakers(200, SPEAKERS_EMPTY);

    const auto& list = host.get_model().speakers;
    expect(list.state == LoadState::Loaded && list.speakers.empty(), "an empty list was not read as a list");

    shell.replace(std::make_unique<SpeakerPickerView>(&host));
}

static const char* check_empty(const SimHost& host) {
    return host.get_model().speaker_id == "speaker-library" ? nullptr : "an empty list changed the stored choice";
}

static void speakers_unreachable(SimHost& host, Shell& shell) {
    host.model().speaker_id = "speaker-library";
    host.model().speakers.state = LoadState::Unreachable;

    shell.replace(std::make_unique<SpeakerPickerView>(&host));
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

    shell.replace(std::make_unique<SpeakerPickerView>(&host));
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

    shell.replace(std::make_unique<SpeakerPickerView>(&host));
}

// A touch on "Try again" asks for the speakers again.
static void speakers_retry(SimHost& host, Shell& shell) {
    host.model().speakers.state = LoadState::Unreachable;

    shell.replace(std::make_unique<SpeakerPickerView>(&host));

    host.tap(400, 372);
}

static const char* check_retry(const SimHost& host) {
    return host.get_model().speakers.state == LoadState::Loading ? nullptr : "the touch did not ask again";
}

static void speakers_reconnecting(SimHost& host, Shell& shell) {
    host.model().stream_state = StreamState::Disconnected;
    host.model().stream_down_since = lv_tick_get() - 10 * 1000;
    host.model().speaker_id = "speaker-library";
    answer_speakers(host);

    shell.replace(std::make_unique<SpeakerPickerView>(&host));
}

const std::vector<Scene>& get_scenes() {
    static const std::vector<Scene> scenes = {
        {"loading", loading},
        {"loading-update", loading_update},
        {"no-backend", no_backend},
        {"speakers", speakers},
        {"speakers-none", speakers_none},
        {"speakers-missing", speakers_missing, check_missing},
        {"speakers-pick", speakers_pick, check_pick},
        {"speakers-many", speakers_many},
        {"speakers-loading", speakers_loading},
        {"speakers-empty", speakers_empty, check_empty},
        {"speakers-unreachable", speakers_unreachable},
        {"speakers-error", speakers_error},
        {"speakers-unreadable", speakers_unreadable},
        {"speakers-retry", speakers_retry, check_retry},
        {"speakers-reconnecting", speakers_reconnecting},
    };

    return scenes;
}
