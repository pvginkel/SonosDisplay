#pragma once

// An HTTP client for the backend's API: one task, one request at a time, results handed to the
// UI task. It depends on esp_http_client, FreeRTOS and esp-support only, and on nothing in ui/ or
// Application, so it can move to esp-libs as it is (docs/firmware-design.md, section 4).

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "Queue.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

struct HttpRequest {
    esp_http_client_method_t method{HTTP_METHOD_GET};
    std::string path;  // "/api/..." appended to the base URL
    std::string body;  // JSON for POST/PUT/DELETE, else empty
    size_t max_response{256 * 1024};
};

// A response body in PSRAM, with a NUL byte after its last byte that `length` does not count.
struct HttpBodyDeleter {
    void operator()(uint8_t* body) const;
};

using HttpBody = std::unique_ptr<uint8_t, HttpBodyDeleter>;

struct HttpResponse {
    esp_err_t err{ESP_OK};  // transport error, or ESP_OK
    int status{};
    HttpBody body;  // the receiver keeps it by moving it out
    size_t length{};
};

class HttpClient {
    struct Job {
        HttpRequest request;
        std::weak_ptr<void> scope;
        std::function<void(HttpResponse&&)> done;
    };

    Queue* _ui_queue;
    const char* _name;
    std::string _base_url;
    const char* _root_certificate{};
    QueueHandle_t _jobs{};
    esp_http_client_handle_t _client{};
    int64_t _last_used_ms{};
    // Requests sent whose answer has not yet run on the UI task, or been dropped.
    std::atomic<uint32_t> _unanswered{};

public:
    static constexpr UBaseType_t QUEUE_DEPTH = 16;
    static constexpr int TIMEOUT_MS = 5000;

    // A kept-alive connection idle for longer than this is closed before the next request. A
    // server that closed it first would otherwise fail that request, and only a GET is retried.
    static constexpr int64_t KEEP_ALIVE_IDLE_MS = 1500;

    HttpClient(Queue* ui_queue, const char* name);
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // `base_url` is scheme, host and optional port, with no trailing slash. `root_certificate` is
    // the PEM of the one root an https:// server's certificate is verified against; the client
    // keeps the pointer.
    esp_err_t begin(const std::string& base_url, const char* root_certificate, uint32_t stack_size,
                    UBaseType_t priority, BaseType_t core);

    // Queues a request. `done` runs on the UI task. `scope` is owned by the caller, in practice
    // the view that asked: once it has expired, the request is dropped and `done` is not called.
    void send(HttpRequest request, std::weak_ptr<void> scope, std::function<void(HttpResponse&&)> done);

    // No request is queued or under way, and every answer has run on the UI task.
    bool is_idle() const { return _unanswered.load() == 0; }

private:
    void run();
    HttpResponse perform(const HttpRequest& request);
    HttpResponse perform_once(const HttpRequest& request);
    esp_err_t read_body(const HttpRequest& request, int64_t content_length, HttpResponse& response);
    esp_err_t ensure_client();
    void reset_client();
    void deliver(Job* job, HttpResponse&& response);
};
