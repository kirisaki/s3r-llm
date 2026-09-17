#include "peer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "mdns.h"

namespace peer
{

namespace
{

constexpr char TAG[] = "peer";
constexpr int POLL_MS = 200;
// The other device may have to read a long context again before it answers
constexpr int64_t SILENCE_TIMEOUT_US = 60 * 1000 * 1000;

// The reply comes in HTTP/1.1 chunks, one per piece: status line and headers,
// then "<size in hex>\r\n<data>\r\n" over and over, a size of 0 at the end.
class ResponseParser
{
  public:
    // Feeds one byte; returns a piece whenever one is complete, else nullptr.
    const char *feed(char c)
    {
        switch (state) {
        case State::Headers:
            if (line_len + 1 < sizeof(line) && c != '\r' && c != '\n') {
                line[line_len++] = c;
            }
            if (c == '\n') {
                line[line_len] = '\0';
                if (status == 0) {
                    // "HTTP/1.1 200 OK"
                    const char *code = strchr(line, ' ');
                    status = code ? atoi(code + 1) : -1;
                } else if (line_len == 0) {
                    state = State::ChunkSize;
                }
                line_len = 0;
            }
            return nullptr;
        case State::ChunkSize:
            if (c == '\n') {
                line[line_len] = '\0';
                remaining = strtol(line, nullptr, 16);
                line_len = 0;
                state = remaining > 0 ? State::ChunkData : State::Done;
            } else if (c != '\r' && line_len + 1 < sizeof(line)) {
                line[line_len++] = c;
            }
            return nullptr;
        case State::ChunkData:
            if (line_len + 1 < sizeof(line)) {
                line[line_len++] = c;
            }
            if (--remaining > 0) {
                return nullptr;
            }
            line[line_len] = '\0';
            line_len = 0;
            state = State::ChunkEnd;
            return line;
        case State::ChunkEnd:
            if (c == '\n') {
                state = State::ChunkSize;
            }
            return nullptr;
        case State::Done:
            return nullptr;
        }
        return nullptr;
    }

    bool done() const
    {
        return state == State::Done;
    }

    int status = 0;

  private:
    enum class State
    {
        Headers,
        ChunkSize,
        ChunkData,
        ChunkEnd,
        Done,
    };
    State state = State::Headers;
    char line[128];
    size_t line_len = 0;
    long remaining = 0;
};

int connect_to(const char *ip)
{
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        return -1;
    }
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }
    // Short timeouts: the waiting is done in chat(), where it can be stopped
    timeval timeout = {};
    timeout.tv_usec = POLL_MS * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    timeval send_timeout = {};
    send_timeout.tv_sec = 5;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
    if (connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        close(sock);
        return -1;
    }
    return sock;
}

bool send_all(int sock, const char *data, size_t len)
{
    while (len > 0) {
        const int n = send(sock, data, len, 0);
        if (n <= 0) {
            return false;
        }
        data += n;
        len -= n;
    }
    return true;
}

} // namespace

bool resolve(const char *name, char *ip, size_t size)
{
    in_addr addr;
    if (inet_pton(AF_INET, name, &addr) == 1) {
        strlcpy(ip, name, size);
        return true;
    }
    esp_ip4_addr_t found;
    if (mdns_query_a(name, 3000, &found) != ESP_OK) {
        return false;
    }
    esp_ip4addr_ntoa(&found, ip, size);
    return true;
}

Result chat(const char *ip, const char *message, const std::function<bool(const char *piece)> &on_piece)
{
    const int sock = connect_to(ip);
    if (sock < 0) {
        ESP_LOGW(TAG, "cannot connect to %s", ip);
        return Result::Failed;
    }

    char head[160];
    const int head_len = snprintf(head, sizeof(head),
                                  "POST /chat HTTP/1.1\r\n"
                                  "Host: %s\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "Content-Length: %u\r\n"
                                  "Connection: close\r\n"
                                  "\r\n",
                                  ip, static_cast<unsigned>(strlen(message)));
    Result result = Result::Failed;
    if (send_all(sock, head, head_len) && send_all(sock, message, strlen(message))) {
        ResponseParser parser;
        int64_t last_data = esp_timer_get_time();
        result = Result::Ok;
        while (!parser.done()) {
            char buf[128];
            const int n = recv(sock, buf, sizeof(buf), 0);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                result = Result::Failed;
                break;
            }
            if (n < 0) {
                if (esp_timer_get_time() - last_data > SILENCE_TIMEOUT_US) {
                    result = Result::Failed;
                    break;
                }
                if (!on_piece("")) {
                    result = Result::Stopped;
                    break;
                }
                continue;
            }
            last_data = esp_timer_get_time();
            bool go_on = true;
            for (int i = 0; i < n && go_on; i++) {
                const char *piece = parser.feed(buf[i]);
                // Only a reply is handed on, not the text of an error
                if (piece && parser.status == 200) {
                    go_on = on_piece(piece);
                }
            }
            if (!go_on) {
                result = Result::Stopped;
                break;
            }
            if (parser.status != 0 && parser.status != 200) {
                result = parser.status == 503 ? Result::Busy : Result::Failed;
                break;
            }
        }
    }
    close(sock);
    return result;
}

} // namespace peer
