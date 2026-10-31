#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <curl/curl.h>

class WebSocketClient {
public:
    using MessageCallback = std::function<void(const std::string&)>;

    WebSocketClient(const std::string& url);
    ~WebSocketClient();

    bool connect();
    void disconnect();
    void poll();
    bool send_message(const std::string& msg);

    void set_on_message(MessageCallback cb) { on_message_ = cb; }
    void set_custom_headers(const std::vector<std::string>& headers) { custom_headers_ = headers; }
    bool is_connected() const { return connected_; }
    std::string get_last_error() const { return last_error_; }
    // Call under the same signaling-I/O ownership as poll(). Contains no payloads.
    std::string get_receive_diagnostics() const;

private:
    std::string url_;
    std::vector<std::string> custom_headers_;
    CURL* curl_ = nullptr;
    MessageCallback on_message_;
    bool connected_ = false;
    std::string last_error_;
    std::vector<uint8_t> rx_buffer_;
    std::vector<uint8_t> message_buffer_;
    bool fragmented_message_ = false;
    struct ReceiveCounters {
        uint64_t text = 0, binary = 0, continuation = 0, ping = 0, pong = 0;
        uint64_t close = 0, messages = 0, fragmented = 0, protocol_errors = 0;
    } receive_;
    bool requested_subprotocol_ = false, selected_subprotocol_ = false, matched_subprotocol_ = false;
    int upgrade_status_ = 0;
    bool send_all(const uint8_t* data, size_t length);
    bool send_frame(uint8_t opcode, const uint8_t* payload, size_t length);
};
