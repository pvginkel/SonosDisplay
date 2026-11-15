#pragma once

#include <memory>
#include <string>

#include "ApplicationBase.h"
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
    std::unique_ptr<HttpClient> _api_client;
    std::unique_ptr<HttpClient> _art_client;
    std::unique_ptr<EventStream> _event_stream;
    // The scope of the app's own requests, which lives as long as the app.
    std::shared_ptr<void> _scope{std::make_shared<int>(0)};
    int64_t _last_memory_log_ms{};

public:
    explicit Application(Board* board);

    const Model& get_model() const override { return _model; }
    void choose_speaker(const std::string& id) override;
    void refresh_speakers() override;
    void refresh_playback() override;
    void send_command(Command command) override;

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
    void command_answered(Command command, HttpResponse&& response);
    void event_received(const std::string& type, const cJSON* payload);
    void model_changed() { _model.revision++; }
    void log_memory();
};
