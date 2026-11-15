#include "Answers.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cJSON.h"

// No list on the display builds more than 200 rows, and the picker builds one per speaker.
static constexpr size_t MAX_SPEAKERS = 200;

// A text the backend sends is shown as it is, up to this many bytes.
static constexpr size_t MAX_TEXT_LENGTH = 200;

using Json = std::unique_ptr<cJSON, void (*)(cJSON*)>;

static Json parse(const char* body, size_t length) { return Json(cJSON_ParseWithLength(body, length), cJSON_Delete); }

static const char* get_string(const cJSON* object, const char* name) {
    const auto item = cJSON_GetObjectItemCaseSensitive(object, name);

    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

// A text of the backend's, cut at a character boundary.
static std::string read_text(const char* text) {
    std::string result = text;
    if (result.length() > MAX_TEXT_LENGTH) {
        auto end = MAX_TEXT_LENGTH;
        while (end > 0 && (uint8_t(result[end]) & 0xC0) == 0x80) {
            end--;
        }
        result.resize(end);
    }

    return result;
}

// The `error` of the error envelope. Empty when the body is not the envelope: a validation error,
// for one, is answered as a bare array.
static std::string read_error(const cJSON* root) {
    if (!cJSON_IsObject(root)) {
        return {};
    }

    const auto error = get_string(root, "error");

    return error ? read_text(error) : std::string();
}

static const char* read_speaker_items(const cJSON* root, std::vector<Speaker>& speakers) {
    if (!cJSON_IsObject(root)) {
        return "the body is not a JSON object";
    }

    const auto data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        return "data is not an array";
    }

    const cJSON* item;
    cJSON_ArrayForEach(item, data) {
        if (speakers.size() == MAX_SPEAKERS) {
            break;
        }

        if (!cJSON_IsObject(item)) {
            return "a speaker is not an object";
        }

        const auto id = get_string(item, "id");
        if (!id || !*id) {
            return "a speaker has no id";
        }

        const auto name = get_string(item, "name");
        if (!name) {
            return "a speaker has no name";
        }

        speakers.push_back(Speaker{.id = id, .name = name});
    }

    return nullptr;
}

const char* read_speakers(int status, const char* body, size_t length, SpeakerList& list) {
    const auto root = parse(body, length);

    if (status != 200) {
        list.state = LoadState::Failed;
        list.error = read_error(root.get());

        return "not a 200";
    }

    std::vector<Speaker> speakers;
    const auto problem = read_speaker_items(root.get(), speakers);

    list.error.clear();

    if (problem) {
        list.state = LoadState::Failed;
    } else {
        list.state = LoadState::Loaded;
        list.answered = true;
        list.speakers = std::move(speakers);
    }

    return problem;
}

static bool read_play_state(const char* name, PlayState& state) {
    static constexpr std::pair<const char*, PlayState> STATES[] = {
        {"playing", PlayState::Playing},
        {"paused", PlayState::Paused},
        {"stopped", PlayState::Stopped},
        {"buffering", PlayState::Buffering},
    };

    for (const auto& [state_name, value] : STATES) {
        if (!strcmp(name, state_name)) {
            state = value;
            return true;
        }
    }

    return false;
}

// `source`, `shuffle`, `queue_position`, `queue_length` and a track's `duration_ms`, `artwork_id`
// and `track_id` are not shown, so they are not read: whatever they hold, `null` included, the
// playback is taken.
static const char* read_playback_fields(const cJSON* root, Playback& playback) {
    if (!cJSON_IsObject(root)) {
        return "the body is not a JSON object";
    }

    const auto state = get_string(root, "state");
    if (!state || !read_play_state(state, playback.play_state)) {
        return "state is not one of the API's";
    }

    const auto actions = cJSON_GetObjectItemCaseSensitive(root, "actions");
    if (!cJSON_IsArray(actions)) {
        return "actions is not an array";
    }

    const cJSON* action;
    cJSON_ArrayForEach(action, actions) {
        if (!cJSON_IsString(action)) {
            return "an action is not a string";
        }

        // An action this display has no control for is one it does not offer.
        const auto name = action->valuestring;
        if (!strcmp(name, "play")) {
            playback.can_play = true;
        } else if (!strcmp(name, "pause")) {
            playback.can_pause = true;
        } else if (!strcmp(name, "next")) {
            playback.can_next = true;
        } else if (!strcmp(name, "previous")) {
            playback.can_previous = true;
        }
    }

    const auto track = cJSON_GetObjectItemCaseSensitive(root, "track");
    if (cJSON_IsNull(track)) {
        return nullptr;
    }
    if (!cJSON_IsObject(track)) {
        return "track is neither an object nor null";
    }

    const auto title = get_string(track, "title");
    if (!title) {
        return "the track has no title";
    }

    const auto artist = get_string(track, "artist");
    if (!artist) {
        return "the track has no artist";
    }

    const auto album = cJSON_GetObjectItemCaseSensitive(track, "album");
    if (!cJSON_IsString(album) && !cJSON_IsNull(album)) {
        return "the track's album is neither a string nor null";
    }

    playback.track = Track{
        .title = read_text(title),
        .artist = read_text(artist),
        .album = cJSON_IsString(album) ? read_text(album->valuestring) : std::string(),
    };

    return nullptr;
}

const char* read_playback(int status, const char* body, size_t length, Playback& playback) {
    const auto root = parse(body, length);

    if (status != 200) {
        playback.state = LoadState::Failed;
        playback.error = read_error(root.get());

        return "not a 200";
    }

    Playback answered;
    const auto problem = read_playback_fields(root.get(), answered);

    playback.error.clear();

    if (problem) {
        playback.state = LoadState::Failed;
    } else {
        playback.state = LoadState::Loaded;
        playback.play_state = answered.play_state;
        playback.can_play = answered.can_play;
        playback.can_pause = answered.can_pause;
        playback.can_next = answered.can_next;
        playback.can_previous = answered.can_previous;
        playback.track = std::move(answered.track);
    }

    return problem;
}

const char* read_command(int status, const char* body, size_t length, std::string& error) {
    if (status == 204) {
        error.clear();

        return nullptr;
    }

    error = read_error(parse(body, length).get());

    return "not a 204";
}

// A volume and its mute, which a speaker's group and the answer to a change both hold.
static const char* read_volume_fields(const cJSON* object, Volume& volume) {
    const auto level = cJSON_GetObjectItemCaseSensitive(object, "volume");
    if (!cJSON_IsNumber(level) || level->valuedouble != level->valueint || level->valueint < 0 ||
        level->valueint > Volume::MAX_LEVEL) {
        return "volume is not a whole number from 0 to 100";
    }

    const auto muted = cJSON_GetObjectItemCaseSensitive(object, "muted");
    if (!cJSON_IsBool(muted)) {
        return "muted is not a boolean";
    }

    volume.state = LoadState::Loaded;
    volume.level = level->valueint;
    volume.muted = cJSON_IsTrue(muted);

    return nullptr;
}

// The speaker's own `volume` and `muted`, its `id`, `name` and `model` and its group's members
// are not shown here, so they are not read.
const char* read_speaker_volume(int status, const char* body, size_t length, Volume& volume, std::string& error) {
    const auto root = parse(body, length);
    const char* problem;

    if (status != 200) {
        error = read_error(root.get());
        problem = "not a 200";
    } else {
        error.clear();

        if (!cJSON_IsObject(root.get())) {
            problem = "the body is not a JSON object";
        } else if (const auto group = cJSON_GetObjectItemCaseSensitive(root.get(), "group"); !cJSON_IsObject(group)) {
            problem = "group is not an object";
        } else {
            problem = read_volume_fields(group, volume);
        }
    }

    if (problem) {
        volume.state = LoadState::Failed;
    }

    return problem;
}

const char* read_volume_change(int status, const char* body, size_t length, Volume& volume, std::string& error) {
    const auto root = parse(body, length);

    if (status != 200) {
        error = read_error(root.get());

        return "not a 200";
    }

    error.clear();

    if (!cJSON_IsObject(root.get())) {
        return "the body is not a JSON object";
    }

    return read_volume_fields(root.get(), volume);
}

const char* read_speaker_event(const cJSON* payload, std::string& speaker_id) {
    if (!cJSON_IsObject(payload)) {
        return "the payload is not a JSON object";
    }

    const auto id = get_string(payload, "speaker_id");
    if (!id) {
        return "the payload has no speaker_id";
    }

    speaker_id = id;

    return nullptr;
}
