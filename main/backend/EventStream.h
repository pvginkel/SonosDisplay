#pragma once

// The backend's server-sent event stream, held open by its own task. Frames are parsed here and
// handed to the UI task. Like HttpClient, it depends on nothing in ui/ or Application
// (docs/firmware-design.md, section 4).

#include <functional>
#include <memory>
#include <string>

#include "Queue.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"

class EventStream {
public:
    using Payload = std::shared_ptr<cJSON>;
    using EventCallback = std::function<void(const std::string& type, Payload payload)>;

private:
    Queue* _ui_queue;
    std::string _url;
    const char* _root_certificate{};
    std::function<void()> _connected;
    std::function<void()> _disconnected;
    EventCallback _event;
    esp_http_client_handle_t _client{};
    bool _ready{};
    bool _rejected{};
    int64_t _last_data_ms{};
    std::string _line;
    std::string _frame_event;
    std::string _frame_data;
    bool _frame_dropped{};

public:
    static constexpr int CONNECT_TIMEOUT_MS = 10000;

    // esp_http_client_read fills its whole buffer before it returns, or waits for its timeout. A
    // short read timeout once connected hands a small frame on at once.
    static constexpr int READ_TIMEOUT_MS = 200;

    // The server writes a heartbeat every 30 seconds.
    static constexpr int64_t IDLE_LIMIT_MS = 75000;
    static constexpr size_t READ_SIZE = 1024;
    static constexpr size_t MAX_FRAME_SIZE = 64 * 1024;
    static constexpr uint32_t REJECTED_DELAY_MS = 60000;

    explicit EventStream(Queue* ui_queue);
    EventStream(const EventStream&) = delete;
    EventStream& operator=(const EventStream&) = delete;

    // Each callback runs on the UI task. `connected` follows every `ready` frame, which is when
    // the app refetches what it shows; `disconnected` follows the loss of a connection that was
    // ready.
    void on_connected(std::function<void()> func) { _connected = std::move(func); }
    void on_disconnected(std::function<void()> func) { _disconnected = std::move(func); }
    void on_event(EventCallback func) { _event = std::move(func); }

    // `root_certificate` is the PEM of the one root an https:// server's certificate is verified
    // against; the stream keeps the pointer.
    esp_err_t begin(const std::string& base_url, const char* root_certificate, uint32_t stack_size,
                    UBaseType_t priority, BaseType_t core);

private:
    void run();
    void connect_and_read();
    void parse(const char* data, size_t length);
    void parse_line();
    void dispatch_frame();
    void dispatch_domain_event();
};
