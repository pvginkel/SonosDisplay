#pragma once

#include <memory>
#include <string>

#include "ApplicationBase.h"
#include "api/Artwork.h"
#include "backend/EventStream.h"
#include "backend/HttpClient.h"
#include "board/Board.h"
#include "ui/Model.h"
#include "ui/Shell.h"
#include "ui/UiHost.h"

class LoadingView;

// The firmware's app on esp-mdm. One UI task runs esp-mdm's queue and LVGL; it is the only task
// that touches LVGL, the model, I2C or NVS. Every other task hands it a closure through
// get_queue().
class Application : public ApplicationBase, public UiHost {
    Board* _board;
    Model _model;
    std::unique_ptr<Shell> _shell;
    LoadingView* _loading_view{};
    uint32_t _screen_off_seconds{ScreenPower::DEFAULT_TIMEOUT_SECONDS};
    // The backend's state, fetched again on its events.
    std::unique_ptr<HttpClient> _api_client;
    // Presses: transport commands, changes of the volume and of groups, items queued and
    // favourites marked or unmarked, in the order they were made, behind no fetch.
    std::unique_ptr<HttpClient> _press_client;
    std::unique_ptr<HttpClient> _art_client;
    std::unique_ptr<EventStream> _event_stream;
    ArtworkLoader _artwork{[this](ArtworkRequest&& request) { fetch_artwork(std::move(request)); }};
    // The scope of the app's own requests, which lives as long as the app.
    std::shared_ptr<void> _scope{std::make_shared<int>(0)};
    int64_t _last_memory_log_ms{};
    bool _settled{};

public:
    explicit Application(Board* board);

    const Model& get_model() const override { return _model; }
    void choose_speaker(const std::string& id) override;
    void refresh_speakers() override;
    void refresh_playback() override;
    void refresh_volume() override;
    void change_volume(int step) override;
    void send_command(Command command) override;
    void add_to_group(const std::string& speaker_id) override;
    void remove_from_group(const std::string& speaker_id) override;
    void queue_item(const QueueRequest& request) override;
    void change_favourite(const FavouriteRequest& request) override;
    void request_artists(uint32_t number, std::weak_ptr<void> scope, std::function<void(Page<Artist>)> done) override;
    void request_albums(const std::string& artist_id, uint32_t number, std::weak_ptr<void> scope,
                        std::function<void(Page<Album>)> done) override;
    void request_tracks(const std::string& album_id, uint32_t number, std::weak_ptr<void> scope,
                        std::function<void(Page<AlbumTrack>)> done) override;
    void request_playlists(uint32_t number, std::weak_ptr<void> scope,
                           std::function<void(Page<Playlist>)> done) override;
    void request_queue(uint32_t number, bool locate, std::weak_ptr<void> scope,
                       std::function<void(QueuePage)> done) override;
    void clear_queue() override;
    void request_artwork(const std::string& artwork_id, int32_t size, std::weak_ptr<void> scope,
                         std::function<void(Artwork)> done) override;

protected:
    void do_begin() override;
    void do_configuration_loaded(cJSON* data) override;
    void do_ready() override;
    void do_process() override;

private:
    void run();
    void begin_backend();
    void load_speaker_choice();
    void speakers_answered(HttpResponse&& response);
    void playback_answered(HttpResponse&& response);
    void volume_answered(HttpResponse&& response);
    void send_volume_change();
    void volume_change_answered(uint32_t change, HttpResponse&& response);
    void command_answered(Command command, HttpResponse&& response);
    void send_group_change(const std::string& speaker_id, bool adds);
    void group_change_answered(uint32_t change, bool adds, HttpResponse&& response);
    void queue_answered(uint32_t request, HttpResponse&& response);
    void favourite_answered(uint32_t change, bool marks, HttpResponse&& response);
    void clear_answered(HttpResponse&& response);
    void task_event_received(const cJSON* payload);
    void fetch_artwork(ArtworkRequest&& request);
    void artwork_answered(const ArtworkRequest& request, HttpResponse&& response);
    void event_received(const std::string& type, const cJSON* payload);
    bool has_settled() const;
    void model_changed() { _model.revision++; }
    void log_memory();
};
