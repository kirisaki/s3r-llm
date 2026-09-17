#include "api.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace api
{

namespace
{

constexpr char TAG[] = "api";
constexpr size_t MAX_MESSAGE = 256;
// How long a request waits for the model, which may be busy with someone else
constexpr TickType_t REPLY_TIMEOUT = pdMS_TO_TICKS(120 * 1000);
// Ends a reply in the message buffer; never part of a piece
constexpr char END_OF_REPLY = '\0';

constexpr char USAGE[] = "s3r-llm\n"
                         "\n"
                         "POST /chat  the body is your message, the reply is streamed back\n"
                         "POST /new   starts a new conversation\n"
                         "\n"
                         "curl -N -d 'Hello!' http://<this device>/chat\n";

// One chat request at a time. Taken by the server task, given back by the
// reply task, hence not a mutex.
SemaphoreHandle_t chat_lock;
QueueHandle_t replies;
char message[MAX_MESSAGE];
std::atomic<bool> message_waiting{false};
MessageBufferHandle_t pieces;
std::atomic<bool> new_requested{false};

// Reads the body, keeps the printable ASCII of it
bool receive_message(httpd_req_t *req)
{
    size_t len = 0;
    size_t remaining = req->content_len;
    while (remaining > 0) {
        char buf[128];
        const int n = httpd_req_recv(req, buf, std::min(remaining, sizeof(buf)));
        if (n <= 0) {
            return false;
        }
        for (int i = 0; i < n; i++) {
            if (buf[i] >= 0x20 && buf[i] <= 0x7E && len + 1 < sizeof(message)) {
                message[len++] = buf[i];
            }
        }
        remaining -= n;
    }
    message[len] = '\0';
    return len > 0;
}

// Streams the reply. Runs in a task of its own: the server has a single task,
// and POST /new has to get through while a reply is under way.
void stream_reply(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");

    // Keep reading to the end even if the client has gone away, so that the
    // next request does not get the rest of this reply
    bool client_ok = true;
    for (;;) {
        char piece[64];
        const size_t n = xMessageBufferReceive(pieces, piece, sizeof(piece), REPLY_TIMEOUT);
        if (n == 0) {
            ESP_LOGW(TAG, "no reply from the model");
            message_waiting = false;
            break;
        }
        if (n == 1 && piece[0] == END_OF_REPLY) {
            break;
        }
        if (client_ok) {
            client_ok = httpd_resp_send_chunk(req, piece, n) == ESP_OK;
        }
    }
    if (client_ok) {
        httpd_resp_send_chunk(req, "\n", 1);
        httpd_resp_send_chunk(req, nullptr, 0);
    }
}

void reply_task(void *)
{
    for (;;) {
        httpd_req_t *req;
        xQueueReceive(replies, &req, portMAX_DELAY);
        stream_reply(req);
        httpd_req_async_handler_complete(req);
        xSemaphoreGive(chat_lock);
    }
}

esp_err_t handle_chat(httpd_req_t *req)
{
    if (xSemaphoreTake(chat_lock, 0) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "busy with another request\n");
    }
    httpd_req_t *async_req = nullptr;
    if (!receive_message(req) || httpd_req_async_handler_begin(req, &async_req) != ESP_OK) {
        xSemaphoreGive(chat_lock);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "the body has to be the message, in ASCII");
    }

    xMessageBufferReset(pieces);
    message_waiting = true;
    xQueueSend(replies, &async_req, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t handle_new(httpd_req_t *req)
{
    new_requested = true;
    return httpd_resp_sendstr(req, "ok\n");
}

esp_err_t handle_usage(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, USAGE);
}

} // namespace

void start()
{
    chat_lock = xSemaphoreCreateBinary();
    xSemaphoreGive(chat_lock);
    pieces = xMessageBufferCreate(1024);
    replies = xQueueCreate(1, sizeof(httpd_req_t *));
    xTaskCreate(reply_task, "api_reply", 4096, nullptr, 5, nullptr);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    httpd_handle_t server = nullptr;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, handle_usage, nullptr},
        {"/chat", HTTP_POST, handle_chat, nullptr},
        {"/new", HTTP_POST, handle_new, nullptr},
    };
    for (const auto &route : routes) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
    }
}

bool poll_chat(char *buf, size_t size)
{
    if (!message_waiting.exchange(false)) {
        return false;
    }
    strlcpy(buf, message, size);
    return true;
}

void send(const char *piece)
{
    // Dropped if the handler has stopped reading; it has given up by then
    xMessageBufferSend(pieces, piece, strlen(piece), pdMS_TO_TICKS(1000));
}

void finish()
{
    xMessageBufferSend(pieces, &END_OF_REPLY, 1, pdMS_TO_TICKS(1000));
}

bool poll_new()
{
    return new_requested.exchange(false);
}

} // namespace api
