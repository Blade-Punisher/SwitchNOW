#pragma once
#include <string>
#include <functional>
#include <memory>
#include "WebSocketClient.hpp"

class SignalingClient {
public:
    SignalingClient(const std::string& url);
    ~SignalingClient();

    bool connect();
    bool send_message(const std::string& msg);
    void set_on_message(std::function<void(const std::string&)> cb) { on_message_ = cb; }
    void poll();
    bool is_connected() const { return ws_ && ws_->is_connected(); }
    
    void set_custom_headers(const std::vector<std::string>& headers) {
        if (ws_) ws_->set_custom_headers(headers);
    }
    
    std::string get_last_error() const { return ws_ ? ws_->get_last_error() : "No WS object"; }
    std::string get_receive_diagnostics() const { return ws_ ? ws_->get_receive_diagnostics() : "unavailable=1"; }

private:
    std::string url_;
    std::function<void(const std::string&)> on_message_;
    std::unique_ptr<WebSocketClient> ws_;
};
