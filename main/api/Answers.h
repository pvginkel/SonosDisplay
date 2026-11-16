#pragma once

// The backend's answers, read into the model's types. This is where what the backend sends
// enters the app, so it is checked here: an answer that is not what the API's document says is
// refused whole, and one it allows is never refused for a field the display does not show. A text
// it sends is read without the characters the fonts do not draw (can_draw()).
//
// It includes cJSON, the C++ standard library and ui/Model.h only, so sim/ builds it too and its
// scenes come from bodies as the backend sends them.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "cJSON.h"
#include "ui/Model.h"

// Each reader returns what was wrong with the answer, for the log, or nullptr when it was what
// the API's document says.

// Reads the answer to GET /api/speakers into `list`: Loaded with the speakers and the members of
// each one's group, or Failed with what the backend said went wrong, where it said so in the
// error envelope.
const char* read_speakers(int status, const char* body, size_t length, SpeakerList& list);

// Reads the answer to GET /api/speakers/{speaker_id}/playback into `playback`: Loaded with what
// plays and the commands the speaker allows, or Failed with what the backend said went wrong. A
// failure leaves the last playback answered in place.
const char* read_playback(int status, const char* body, size_t length, Playback& playback);

// Reads the answer to a transport command or to a change of a group, which is a 204 when the
// backend took it. `error` is what the backend said went wrong, where it said so.
const char* read_command(int status, const char* body, size_t length, std::string& error);

// Reads the answer to GET /api/speakers/{speaker_id} into `volume`: Loaded with the level and the
// mute of the speaker's group, or Failed, which leaves the last level answered in place. `error`
// is what the backend said went wrong, where it said so.
const char* read_speaker_volume(int status, const char* body, size_t length, Volume& volume, std::string& error);

// Reads the answer to a change of the group's volume, which is a 200 with the level and the mute
// after the change: `volume` is Loaded with them. Any other answer leaves `volume` as it is.
const char* read_volume_change(int status, const char* body, size_t length, Volume& volume, std::string& error);

// Reads the answer to GET /api/library/artists, asked for with `per_page` PAGE_SIZE, into `page`:
// Loaded with the page's artists, its number and how many artists there are, or Failed with what
// the backend said went wrong, where it said so.
const char* read_artists(int status, const char* body, size_t length, Page<Artist>& page);

// Reads the answer to GET /api/library/artists/{artist_id}/albums, asked for with `per_page`
// PAGE_SIZE and no sort, as read_artists() reads the artists: the albums newest first, by release
// date, as playing the artist queues them.
const char* read_albums(int status, const char* body, size_t length, Page<Album>& page);

// Reads the answer to GET /api/library/albums/{album_id}/tracks, asked for with `per_page`
// PAGE_SIZE, as read_artists() reads the artists: the tracks in disc and track order.
const char* read_tracks(int status, const char* body, size_t length, Page<AlbumTrack>& page);

// Reads the answer to GET /api/library/playlists, asked for with `per_page` PAGE_SIZE, as
// read_artists() reads the artists.
const char* read_playlists(int status, const char* body, size_t length, Page<Playlist>& page);

// Reads the answer to GET /api/speakers/{speaker_id}/queue, asked for with `per_page` PAGE_SIZE,
// as read_artists() reads the artists, and where in the queue the group is: the entries in the
// order the speaker plays them, and the place of the track that is loaded.
const char* read_queue(int status, const char* body, size_t length, QueuePage& page);

// Reads the answer to POST /api/speakers/{speaker_id}/queue, which is a 202 with the id of the
// task that queues the item when the backend took it, into `task_id`. `error` is what the backend
// said went wrong, where it said so.
const char* read_queue_answer(int status, const char* body, size_t length, std::string& task_id, std::string& error);

// Reads the answer to POST /api/favourites, which is a 201 with the new favourite, or a 200 with
// the favourite that marked the item already, into `favourite_id`: the favourite's id. `error` is
// what the backend said went wrong, where it said so.
const char* read_favourite_mark(int status, const char* body, size_t length, int64_t& favourite_id, std::string& error);

// Reads the answer to DELETE /api/favourites/{favourite_id}, which is a 204 when the backend
// removed the mark, or a 404 with the code RECORD_NOT_FOUND when it holds no favourite with the
// id, which another client removed: the item is unmarked either way. `error` is what the backend
// said went wrong, where it said so.
const char* read_favourite_unmark(int status, const char* body, size_t length, std::string& error);

// Reads the payload of an event that names a speaker into `speaker_id`.
const char* read_speaker_event(const cJSON* payload, std::string& speaker_id);

// Reads the payload of the event task_event into `end`: the task's end, with its failure in the
// backend's words where it failed, or nothing for a task that goes on.
const char* read_task_event(const cJSON* payload, std::optional<TaskEnd>& end);
