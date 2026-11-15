#include "Answers.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cJSON.h"

// No list on the display builds more than 200 rows, and the picker builds one per speaker.
static constexpr size_t MAX_SPEAKERS = 200;

// What the backend says went wrong is shown as it is, up to this many bytes.
static constexpr size_t MAX_ERROR_LENGTH = 200;

using Json = std::unique_ptr<cJSON, void (*)(cJSON*)>;

static Json parse(const char* body, size_t length) { return Json(cJSON_ParseWithLength(body, length), cJSON_Delete); }

static const char* get_string(const cJSON* object, const char* name) {
    const auto item = cJSON_GetObjectItemCaseSensitive(object, name);

    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

// The `error` of the error envelope, cut at a character boundary. Empty when the body is not the
// envelope: a validation error, for one, is answered as a bare array.
static std::string read_error(const cJSON* root) {
    if (!cJSON_IsObject(root)) {
        return {};
    }

    const auto error = get_string(root, "error");
    if (!error) {
        return {};
    }

    std::string result = error;
    if (result.length() > MAX_ERROR_LENGTH) {
        auto end = MAX_ERROR_LENGTH;
        while (end > 0 && (uint8_t(result[end]) & 0xC0) == 0x80) {
            end--;
        }
        result.resize(end);
    }

    return result;
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
        list.speakers = std::move(speakers);
    }

    return problem;
}
