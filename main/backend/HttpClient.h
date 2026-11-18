#pragma once

// An HTTP client for the backend's API: one task, one request at a time, results handed to the
// UI task. It depends on esp_http_client, FreeRTOS and esp-support only, and on nothing in ui/ or
// Application, so it can move to esp-libs as it is (docs/firmware-design.md, section 4).

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "Mutex.h"
#include "Queue.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct HttpRequest {
    esp_http_client_method_t method{HTTP_METHOD_GET};
    std::string path;  // "/api/..." or "/health/..." appended to the base URL
    std::string body;  // JSON for POST/PUT/DELETE, else empty
    size_t max_response{256 * 1024};
    // Whether the request goes as one with an equal request that also merges and still waits to
    // be sent. The later caller's scope and `done` then take the place of the earlier's, whose
    // `done` is not called. A request under way merges with nothing.
    bool merges{};
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
        // A request keep_open() queued, whose answer is dropped and which no caller waits on.
        bool keeps_open{};
    };

    Queue* _ui_queue;
    const char* _name;
    std::string _base_url;
    const char* _root_certificate{};
    Mutex _lock;
    // The jobs that wait to be sent, oldest first. Guarded by _lock.
    std::deque<Job*> _waiting;
    // Counts the jobs in _waiting.
    SemaphoreHandle_t _jobs_waiting{};
    esp_http_client_handle_t _client{};
    // Whether _client holds a kept-alive connection. Written by the task only.
    std::atomic<bool> _connected{};
    // When the last answer on the connection was read. Written by the task only, guarded by
    // _lock for keep_open().
    int64_t _last_used_ms{};
    // A job is under way on the task. Guarded by _lock.
    bool _under_way{};
    // When keep_open() last queued a request. Guarded by _lock.
    int64_t _last_keep_open_ms{};
    // Requests sent whose answer has not yet run on the UI task, or been dropped.
    std::atomic<uint32_t> _unanswered{};

public:
    static constexpr UBaseType_t QUEUE_DEPTH = 16;
    static constexpr int TIMEOUT_MS = 5000;

    // A kept-alive connection is closed by this client once it has been idle this long, by the task
    // as the time passes, so no request is ever sent on one the server may have closed: that
    // request would fail, and only a GET is retried, never a press. The backend's nginx closes a
    // connection idle for 65 seconds (its default keepalive_timeout, measured on 2026-11-18). The
    // 15 seconds between are the most the idle time seen here can fall short of the server's: the
    // time the last answer took to arrive and the next request takes to arrive, each bounded by
    // TIMEOUT_MS, and the task's wake-up. Past it the next request opens a new connection: a
    // handshake, never a request sent twice (docs/firmware-design.md, section 4).
    static constexpr int64_t KEEP_ALIVE_IDLE_MS = 50 * 1000;

    // keep_open() asks again on a connection idle this long, before the task closes it.
    static constexpr int64_t KEEP_OPEN_AFTER_MS = 40 * 1000;

    // keep_open() queues no request within this long of its last, so a backend that cannot be
    // reached is not asked in a loop.
    static constexpr int64_t KEEP_OPEN_RETRY_MS = 10 * 1000;

    HttpClient(Queue* ui_queue, const char* name);
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // `base_url` is scheme, host and optional port, with no trailing slash. `root_certificate` is
    // the PEM of the one root an https:// server's certificate is verified against; the client
    // keeps the pointer.
    esp_err_t begin(const std::string& base_url, const char* root_certificate, uint32_t stack_size,
                    UBaseType_t priority, BaseType_t core);

    // Queues a request, or merges it with one that waits (HttpRequest::merges). `done` runs on
    // the UI task. `scope` is owned by the caller, in practice the view that asked: once it has
    // expired, the request is dropped and `done` is not called.
    void send(HttpRequest request, std::weak_ptr<void> scope, std::function<void(HttpResponse&&)> done);

    // Opens the connection, or keeps it from closing, with a GET of `path` whose answer is
    // dropped: the app calls it on every pass of the UI task while the connection should be open
    // when a hand needs it. It queues the GET only when no connection is open or the open one has
    // been idle for KEEP_OPEN_AFTER_MS, and nothing while a request waits or is under way. A GET
    // that fails is not retried before KEEP_OPEN_RETRY_MS.
    void keep_open(const char* path);

    // No request is queued or under way, and every answer has run on the UI task.
    bool is_idle() const { return _unanswered.load() == 0; }

    // The client holds a kept-alive connection.
    bool is_connected() const { return _connected.load(); }

private:
    void run();
    Job* next_job();
    HttpResponse perform(const HttpRequest& request);
    HttpResponse perform_once(const HttpRequest& request);
    esp_err_t read_body(const HttpRequest& request, int64_t content_length, HttpResponse& response);
    esp_err_t ensure_client();
    bool server_closed();
    void close_connection();
    void reset_client();
    void used(int64_t now);
    void deliver(Job* job, HttpResponse&& response);
};
