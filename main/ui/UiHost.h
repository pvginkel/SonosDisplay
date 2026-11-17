#pragma once

// What a view may ask of the firmware. Application implements it on the device; sim/ implements
// it on fixtures.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "Model.h"
#include "lvgl.h"

// A piece of artwork as LVGL draws it. The host keeps the image in memory for as long as a view
// holds this, so a view holds it for exactly as long as a widget draws from it.
using Artwork = std::shared_ptr<const lv_image_dsc_t>;

class UiHost {
public:
    UiHost() = default;
    UiHost(const UiHost&) = delete;
    UiHost& operator=(const UiHost&) = delete;
    virtual ~UiHost() = default;

    virtual const Model& get_model() const = 0;

    // Makes the speaker with this id the one the display controls, and stores the choice on the
    // display. Nothing changes when it is the chosen one already.
    virtual void choose_speaker(const std::string& id) = 0;

    // Asks the backend for the speakers again.
    virtual void refresh_speakers() = 0;

    // Asks the backend again for what the chosen speaker plays.
    virtual void refresh_playback() = 0;

    // Asks the backend again for the volume of the chosen speaker's group.
    virtual void refresh_volume() = 0;

    // Turns the volume of the chosen speaker's group down, by a negative step, or up. A step up
    // while the volume shows muted unmutes the group too; down leaves the mute. A view sends only
    // a step the volume allows.
    virtual void change_volume(int step) = 0;

    // Sends a transport command to the chosen speaker. A view sends only a command the playback
    // allows.
    virtual void send_command(Command command) = 0;

    // Has the speaker with this id join the chosen speaker's group, where it plays what the group
    // plays. A view asks only while a speaker is chosen, for another speaker than the chosen one,
    // and only for a speaker the grouping allows a change of. The change is pending in the model
    // from here on.
    virtual void add_to_group(const std::string& speaker_id) = 0;

    // Has the speaker with this id leave the group it plays in, and play alone. A view asks under
    // the same conditions, for a speaker of the chosen speaker's group.
    virtual void remove_from_group(const std::string& speaker_id) = 0;

    // Has the chosen speaker's group play, shuffle or add to its queue an item of the library. A
    // view asks only while a speaker is chosen, never to shuffle a track, and never while the same
    // request is on its way (Queueing::is_on_its_way()). The request is on its way in the model
    // from here on.
    virtual void queue_item(const QueueRequest& request) = 0;

    // Marks an artist, an album or a playlist of the library as a favourite, or unmarks the
    // favourite that marks it. A view asks only while no change of the same item is on its way
    // (Favouring::is_on_its_way()). The change is on its way in the model from here on.
    virtual void change_favourite(const FavouriteRequest& request) = 0;

    // Asks the backend for page `number` of the library's artists, from 1, PAGE_SIZE to a page.
    // `done` runs with the page, or with why it did not come, and not at all once `scope`, which
    // the asker owns, has expired.
    virtual void request_artists(uint32_t number, std::weak_ptr<void> scope,
                                 std::function<void(Page<Artist>)> done) = 0;

    // Asks, as request_artists() does, for a page of the albums of the artist with this id, newest
    // first, as playing the artist queues them.
    virtual void request_albums(const std::string& artist_id, uint32_t number, std::weak_ptr<void> scope,
                                std::function<void(Page<Album>)> done) = 0;

    // Asks, as request_artists() does, for a page of the tracks of the album with this id, in disc
    // and track order.
    virtual void request_tracks(const std::string& album_id, uint32_t number, std::weak_ptr<void> scope,
                                std::function<void(Page<AlbumTrack>)> done) = 0;

    // Asks, as request_artists() does, for a page of the library's playlists.
    virtual void request_playlists(uint32_t number, std::weak_ptr<void> scope,
                                   std::function<void(Page<Playlist>)> done) = 0;

    // Asks, as request_artists() does, for a page of the favourites, by title.
    virtual void request_favourites(uint32_t number, std::weak_ptr<void> scope,
                                    std::function<void(Page<Favourite>)> done) = 0;

    // Asks, as request_artists() does, for page `number` of the queue of the chosen speaker's group,
    // or, `locate`, for the page that holds the track that is loaded, and page `number` while none
    // is. A view asks only while a speaker is chosen.
    virtual void request_queue(uint32_t number, bool locate, std::weak_ptr<void> scope,
                               std::function<void(QueuePage)> done) = 0;

    // Empties the queue of the chosen speaker's group; the group stops if it plays from it. A view
    // asks only while a speaker is chosen. What changed comes as the events that move
    // Model::queue_changes.
    virtual void clear_queue() = 0;

    // Asks for the picture with this id, `size` by `size` pixels. `done` runs with it once the
    // host has it, which is before this returns for one it has already, and may be much later:
    // nothing is fetched while the screen is dark. It runs with nullptr when the backend did not
    // give the picture or it does not fit beside the pictures views hold, and not at all once
    // `scope`, which the asker owns, has expired.
    virtual void request_artwork(const std::string& artwork_id, int32_t size, std::weak_ptr<void> scope,
                                 std::function<void(Artwork)> done) = 0;
};
