#include "Answers.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cJSON.h"

// No list on the display builds more than 200 rows, and the picker builds one per speaker.
static constexpr size_t MAX_SPEAKERS = 200;

// A text the backend sends is shown up to this many bytes.
static constexpr size_t MAX_TEXT_LENGTH = 200;

using Json = std::unique_ptr<cJSON, void (*)(cJSON*)>;

static Json parse(const char* body, size_t length) { return Json(cJSON_ParseWithLength(body, length), cJSON_Delete); }

static const char* get_string(const cJSON* object, const char* name) {
    const auto item = cJSON_GetObjectItemCaseSensitive(object, name);

    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

// The UTF-8 character `text` starts with, of `length` bytes at most: its length in bytes, with its
// code point in `code_point`, or 0 where `text` does not start with one: a stray continuation
// byte, a sequence cut short, an overlong one, or one past U+10FFFF.
static size_t decode_utf8(const char* text, size_t length, uint32_t& code_point) {
    const auto lead = uint8_t(text[0]);
    size_t size;
    uint32_t least;

    if (lead < 0x80) {
        code_point = lead;
        return 1;
    } else if ((lead & 0xE0) == 0xC0) {
        size = 2;
        code_point = lead & 0x1F;
        least = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        size = 3;
        code_point = lead & 0x0F;
        least = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        size = 4;
        code_point = lead & 0x07;
        least = 0x10000;
    } else {
        return 0;
    }

    if (size > length) {
        return 0;
    }

    for (size_t i = 1; i < size; i++) {
        const auto byte = uint8_t(text[i]);
        if ((byte & 0xC0) != 0x80) {
            return 0;
        }

        code_point = code_point << 6 | (byte & 0x3F);
    }

    return code_point >= least && code_point <= 0x10FFFF ? size : 0;
}

// A text of the backend's as the display draws it: without the characters the fonts do not draw
// and the bytes that are not UTF-8, and cut at a character to MAX_TEXT_LENGTH bytes.
static std::string read_text(const char* text) {
    const auto length = strlen(text);
    std::string result;

    for (size_t at = 0; at < length;) {
        uint32_t code_point;
        const auto size = decode_utf8(text + at, length - at, code_point);
        if (!size) {
            at++;
            continue;
        }

        if (can_draw(code_point)) {
            if (result.length() + size > MAX_TEXT_LENGTH) {
                break;
            }

            result.append(text + at, size);
        }

        at += size;
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

// The members of a listed speaker's group. The ids are opaque, and go back to the backend as they
// came: they are never cut.
static const char* read_group_members(const cJSON* speaker, std::vector<std::string>& member_ids) {
    const auto group = cJSON_GetObjectItemCaseSensitive(speaker, "group");
    if (!cJSON_IsObject(group)) {
        return "a speaker's group is not an object";
    }

    if (!get_string(group, "coordinator_id")) {
        return "a group has no coordinator_id";
    }

    const auto members = cJSON_GetObjectItemCaseSensitive(group, "member_ids");
    if (!cJSON_IsArray(members)) {
        return "a group's member_ids is not an array";
    }

    const cJSON* member;
    cJSON_ArrayForEach(member, members) {
        if (!cJSON_IsString(member)) {
            return "a group's member is not a string";
        }

        member_ids.emplace_back(member->valuestring);
    }

    return nullptr;
}

// A listed speaker's `model`, its own `volume` and `muted` and its group's are not shown from the
// list, so they are not read.
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

        Speaker speaker{.id = id, .name = read_text(name)};
        if (const auto problem = read_group_members(item, speaker.member_ids)) {
            return problem;
        }

        speakers.push_back(std::move(speaker));
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

// `source`, `shuffle`, `queue_position`, `queue_length` and a track's `duration_ms` and `track_id`
// are not shown, so they are not read: whatever they hold, `null` included, the playback is taken.
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

    const auto artwork_id = cJSON_GetObjectItemCaseSensitive(track, "artwork_id");
    if (!cJSON_IsString(artwork_id) && !cJSON_IsNull(artwork_id)) {
        return "the track's artwork_id is neither a string nor null";
    }

    // The id is opaque, and goes back to the backend as it came: it is never cut.
    playback.track = Track{
        .title = read_text(title),
        .artist = read_text(artist),
        .album = cJSON_IsString(album) ? read_text(album->valuestring) : std::string(),
        .artwork_id = cJSON_IsString(artwork_id) ? artwork_id->valuestring : std::string(),
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

// The task's `status` is not shown, so it is not read.
const char* read_queue_answer(int status, const char* body, size_t length, std::string& task_id, std::string& error) {
    const auto root = parse(body, length);

    if (status != 202) {
        error = read_error(root.get());

        return "not a 202";
    }

    error.clear();

    if (!cJSON_IsObject(root.get())) {
        return "the body is not a JSON object";
    }

    const auto id = get_string(root.get(), "task_id");
    if (!id) {
        return "the answer has no task_id";
    }

    task_id = id;

    return nullptr;
}

// A double holds every whole number up to this one, 2^53, exactly.
static constexpr double MAX_WHOLE_NUMBER = 9007199254740992.0;

// The id of a favourite: a whole number.
static bool read_favourite_number(const cJSON* item, int64_t& id) {
    if (!cJSON_IsNumber(item) || std::fabs(item->valuedouble) > MAX_WHOLE_NUMBER ||
        item->valuedouble != std::trunc(item->valuedouble)) {
        return false;
    }

    id = int64_t(item->valuedouble);

    return true;
}

// The favourite's other keys are not shown where it is marked, so they are not read.
const char* read_favourite_mark(int status, const char* body, size_t length, int64_t& favourite_id,
                                std::string& error) {
    const auto root = parse(body, length);

    if (status != 200 && status != 201) {
        error = read_error(root.get());

        return "not a 200 or a 201";
    }

    error.clear();

    if (!cJSON_IsObject(root.get())) {
        return "the body is not a JSON object";
    }

    if (!read_favourite_number(cJSON_GetObjectItemCaseSensitive(root.get(), "id"), favourite_id)) {
        return "the favourite's id is not a whole number";
    }

    return nullptr;
}

const char* read_favourite_unmark(int status, const char* body, size_t length, std::string& error) {
    if (status == 204) {
        error.clear();

        return nullptr;
    }

    const auto root = parse(body, length);
    error = read_error(root.get());

    const auto code = status == 404 && cJSON_IsObject(root.get()) ? get_string(root.get(), "code") : nullptr;
    if (code && !strcmp(code, "RECORD_NOT_FOUND")) {
        error.clear();

        return nullptr;
    }

    return "not a 204, or a 404 with the code RECORD_NOT_FOUND";
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

// A number of the answer's that must be whole and at least `least`.
static bool read_count(const cJSON* object, const char* name, int least, uint32_t& value) {
    const auto item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || item->valuedouble != item->valueint || item->valueint < least) {
        return false;
    }

    value = uint32_t(item->valueint);

    return true;
}

// A page of a list: `data`, each item read by `read_item`, and of `meta` the page's number, its
// size, which is the PAGE_SIZE asked for, and the list's total, which the items up to this page
// do not pass: a page past the list's end, which a list grown shorter answers, holds none.
// `sort_by`, `sort_dir` and `total_pages` are not used, so they are not read.
template <typename Item>
static const char* read_page_fields(const cJSON* root, Page<Item>& page,
                                    const char* (*read_item)(const cJSON* item, Item& read)) {
    if (!cJSON_IsObject(root)) {
        return "the body is not a JSON object";
    }

    const auto meta = cJSON_GetObjectItemCaseSensitive(root, "meta");
    if (!cJSON_IsObject(meta)) {
        return "meta is not an object";
    }

    if (!read_count(meta, "page", 1, page.number)) {
        return "meta.page is not a whole number from 1";
    }

    uint32_t per_page;
    if (!read_count(meta, "per_page", 1, per_page) || per_page != PAGE_SIZE) {
        return "meta.per_page is not the page size asked for";
    }

    if (!read_count(meta, "total", 0, page.total)) {
        return "meta.total is not a whole number from 0";
    }

    const auto data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        return "data is not an array";
    }

    const auto size = uint32_t(cJSON_GetArraySize(data));
    if (size > PAGE_SIZE) {
        return "data holds more items than a page";
    }
    if (size && uint64_t(page.number - 1) * PAGE_SIZE + size > page.total) {
        return "data holds more items than meta.total leaves for the page";
    }

    const cJSON* item;
    cJSON_ArrayForEach(item, data) {
        Item read;
        if (const auto problem = read_item(item, read)) {
            return problem;
        }

        page.items.push_back(std::move(read));
    }

    return nullptr;
}

// Reads a page of a list into `page`, a Page or one that holds more of the answer, which
// `read_more` reads where it is given.
template <typename PageOf, typename Item>
static const char* read_page(int status, const char* body, size_t length, PageOf& page,
                             const char* (*read_item)(const cJSON* item, Item& read),
                             const char* (*read_more)(const cJSON* root, PageOf& page) = nullptr) {
    const auto root = parse(body, length);

    if (status != 200) {
        page.state = LoadState::Failed;
        page.error = read_error(root.get());

        return "not a 200";
    }

    PageOf answered;
    answered.state = LoadState::Loaded;

    auto problem = read_page_fields(root.get(), answered, read_item);
    if (!problem && read_more) {
        problem = read_more(root.get(), answered);
    }
    if (problem) {
        page.state = LoadState::Failed;
        page.error.clear();

        return problem;
    }

    page = std::move(answered);

    return nullptr;
}

// The `id` and `title` of an item of the library. The id is opaque, and goes back to the backend as
// it came: it is never cut.
static const char* read_item_names(const cJSON* item, std::string& id, std::string& title) {
    if (!cJSON_IsObject(item)) {
        return "an item is not an object";
    }

    const auto read_id = get_string(item, "id");
    if (!read_id || !*read_id) {
        return "an item has no id";
    }

    const auto read_title = get_string(item, "title");
    if (!read_title) {
        return "an item has no title";
    }

    id = read_id;
    title = read_text(read_title);

    return nullptr;
}

// The `favourite_id` of an artist, an album or a playlist: the id of the favourite that marks it,
// or null.
static const char* read_favourite_id(const cJSON* item, std::optional<int64_t>& favourite_id) {
    const auto value = cJSON_GetObjectItemCaseSensitive(item, "favourite_id");
    if (cJSON_IsNull(value)) {
        favourite_id.reset();

        return nullptr;
    }

    int64_t id;
    if (!read_favourite_number(value, id)) {
        return "an item's favourite_id is not a whole number or null";
    }

    favourite_id = id;

    return nullptr;
}

// An artist's `artwork_id` is not shown, so it is not read.
static const char* read_artist(const cJSON* item, Artist& artist) {
    Artist read;
    if (const auto problem = read_item_names(item, read.id, read.title)) {
        return problem;
    }

    const auto letter = get_string(item, "index_letter");
    if (!letter || strlen(letter) != 1 || !(*letter == '#' || (*letter >= 'A' && *letter <= 'Z'))) {
        return "an artist's index_letter is not a letter from A to Z or #";
    }

    read.index_letter = *letter;

    if (const auto problem = read_favourite_id(item, read.favourite_id)) {
        return problem;
    }

    artist = std::move(read);

    return nullptr;
}

// The jump to a letter rests on the order: a letter's artists are together, those under '#' first,
// which ASCII puts before 'A'.
const char* read_artists(int status, const char* body, size_t length, Page<Artist>& page) {
    if (const auto problem = read_page(status, body, length, page, read_artist)) {
        return problem;
    }

    for (size_t i = 1; i < page.items.size(); i++) {
        if (page.items[i].index_letter < page.items[i - 1].index_letter) {
            page = {.state = LoadState::Failed};

            return "the artists are not in the order of their index_letter";
        }
    }

    return nullptr;
}

// An album's `artist_id`, `artist`, `track_count` and `artwork_id` are not shown, so they are not
// read.
static const char* read_album(const cJSON* item, Album& album) {
    Album read;
    if (const auto problem = read_item_names(item, read.id, read.title)) {
        return problem;
    }

    const auto year = cJSON_GetObjectItemCaseSensitive(item, "year");
    if (cJSON_IsNumber(year) && year->valuedouble == year->valueint) {
        read.year = year->valueint;
    } else if (!cJSON_IsNull(year)) {
        return "an album's year is not a whole number or null";
    }

    if (const auto problem = read_favourite_id(item, read.favourite_id)) {
        return problem;
    }

    album = std::move(read);

    return nullptr;
}

const char* read_albums(int status, const char* body, size_t length, Page<Album>& page) {
    return read_page(status, body, length, page, read_album);
}

// Reads the answer to a request for one item of the library, a 200 with the item, as `read_item`
// reads an item of its list.
template <typename Item>
static const char* read_one(int status, const char* body, size_t length, Item& item, std::string& error,
                            const char* (*read_item)(const cJSON* item, Item& read)) {
    const auto root = parse(body, length);

    if (status != 200) {
        error = read_error(root.get());

        return "not a 200";
    }

    error.clear();

    return read_item(root.get(), item);
}

const char* read_one_artist(int status, const char* body, size_t length, Artist& artist, std::string& error) {
    return read_one(status, body, length, artist, error, read_artist);
}

const char* read_one_album(int status, const char* body, size_t length, Album& album, std::string& error) {
    return read_one(status, body, length, album, error, read_album);
}

// A track's `artist`, `disc`, `number`, `duration_ms` and `artwork_id` are not shown, so they are not
// read: the backend gives the tracks in disc and track order.
static const char* read_track(const cJSON* item, AlbumTrack& track) {
    return read_item_names(item, track.id, track.title);
}

const char* read_tracks(int status, const char* body, size_t length, Page<AlbumTrack>& page) {
    return read_page(status, body, length, page, read_track);
}

// A playlist's `track_count`, `duration_ms` and `artwork_id` are not shown, so they are not read.
static const char* read_playlist(const cJSON* item, Playlist& playlist) {
    Playlist read;
    if (const auto problem = read_item_names(item, read.id, read.title)) {
        return problem;
    }
    if (const auto problem = read_favourite_id(item, read.favourite_id)) {
        return problem;
    }

    playlist = std::move(read);

    return nullptr;
}

const char* read_playlists(int status, const char* body, size_t length, Page<Playlist>& page) {
    return read_page(status, body, length, page, read_playlist);
}

static bool read_favourite_kind(const char* name, ItemKind& kind) {
    if (!strcmp(name, "artist")) {
        kind = ItemKind::Artist;
    } else if (!strcmp(name, "album")) {
        kind = ItemKind::Album;
    } else if (!strcmp(name, "playlist")) {
        kind = ItemKind::Playlist;
    } else {
        return false;
    }

    return true;
}

// A key of a favourite's that is a string or null, and present either way. Returns the string, or
// nullptr for null and for a key that is neither, which `present` tells apart.
static const char* get_string_or_null(const cJSON* item, const char* name, bool& present) {
    const auto value = cJSON_GetObjectItemCaseSensitive(item, name);
    present = cJSON_IsString(value) || cJSON_IsNull(value);

    return cJSON_IsString(value) ? value->valuestring : nullptr;
}

// A favourite's `subtitle` and `created_at` are not shown, so they are not read. Its `item_id` and
// `artwork_id` are null while it is not available, and are read only while it is: an available
// one names its item. The ids are opaque, and go back to the backend as they came: they are never
// cut.
static const char* read_favourite(const cJSON* item, Favourite& favourite) {
    if (!cJSON_IsObject(item)) {
        return "a favourite is not an object";
    }

    Favourite read;
    if (!read_favourite_number(cJSON_GetObjectItemCaseSensitive(item, "id"), read.id)) {
        return "a favourite's id is not a whole number";
    }

    const auto kind = get_string(item, "kind");
    if (!kind || !read_favourite_kind(kind, read.kind)) {
        return "a favourite's kind is not artist, album or playlist";
    }

    const auto title = get_string(item, "title");
    if (!title) {
        return "a favourite has no title";
    }

    const auto available = cJSON_GetObjectItemCaseSensitive(item, "available");
    if (!cJSON_IsBool(available)) {
        return "a favourite's available is not a boolean";
    }

    bool present;
    const auto item_id = get_string_or_null(item, "item_id", present);
    if (!present) {
        return "a favourite's item_id is neither a string nor null";
    }

    const auto artwork_id = get_string_or_null(item, "artwork_id", present);
    if (!present) {
        return "a favourite's artwork_id is neither a string nor null";
    }

    read.title = read_text(title);
    read.available = cJSON_IsTrue(available);
    if (read.available) {
        if (!item_id || !*item_id) {
            return "an available favourite has no item_id";
        }

        read.item_id = item_id;
        read.artwork_id = artwork_id ? artwork_id : "";
    }

    favourite = std::move(read);

    return nullptr;
}

const char* read_favourites(int status, const char* body, size_t length, Page<Favourite>& page) {
    return read_page(status, body, length, page, read_favourite);
}

// An entry's `position` is its place in the page, and its `album`, `duration_ms`, `artwork_id` and
// `track_id` are not shown, so they are not read.
static const char* read_queue_entry(const cJSON* item, QueueEntry& entry) {
    if (!cJSON_IsObject(item)) {
        return "an entry is not an object";
    }

    const auto title = get_string(item, "title");
    if (!title) {
        return "an entry has no title";
    }

    const auto artist = get_string(item, "artist");
    if (!artist) {
        return "an entry has no artist";
    }

    entry = QueueEntry{.title = read_text(title), .artist = read_text(artist)};

    return nullptr;
}

// A position past the queue's end is taken: the backend reads it apart from the entries, which a
// change can make fewer in between.
static const char* read_current_position(const cJSON* root, QueuePage& page) {
    if (cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(root, "current_position"))) {
        return nullptr;
    }

    uint32_t position;
    if (!read_count(root, "current_position", 1, position)) {
        return "current_position is neither a whole number from 1 nor null";
    }

    page.current_position = position;

    return nullptr;
}

const char* read_queue(int status, const char* body, size_t length, QueuePage& page) {
    return read_page(status, body, length, page, read_queue_entry, read_current_position);
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

// An event's `timestamp` is not shown, so it is not read, and neither is what a task that goes on
// reports.
const char* read_task_event(const cJSON* payload, std::optional<TaskEnd>& end) {
    if (!cJSON_IsObject(payload)) {
        return "the payload is not a JSON object";
    }

    const auto task_id = get_string(payload, "task_id");
    if (!task_id) {
        return "the payload has no task_id";
    }

    const auto type = get_string(payload, "event_type");
    if (!type) {
        return "the payload has no event_type";
    }

    if (!strcmp(type, "task_started") || !strcmp(type, "progress_update")) {
        end.reset();
    } else if (!strcmp(type, "task_completed")) {
        end = TaskEnd{.task_id = task_id};
    } else if (!strcmp(type, "task_failed")) {
        const auto data = cJSON_GetObjectItemCaseSensitive(payload, "data");
        const auto error = cJSON_IsObject(data) ? get_string(data, "error") : nullptr;
        if (!error) {
            return "a task_failed has no data.error";
        }

        end = TaskEnd{.task_id = task_id, .error = read_text(error)};
    } else {
        return "the event_type is not one of a task's";
    }

    return nullptr;
}
