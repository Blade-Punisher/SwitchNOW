#include "signaling_client.hpp"
#include <iostream>
#include "borealis.hpp"

SignalingClient::SignalingClient(const std::string& url) : url_(url) {
    ws_ = std::make_unique<WebSocketClient>(url_);
}

SignalingClient::~SignalingClient() {
    if (ws_) {
        ws_->disconnect();
    }
}

bool SignalingClient::connect() {
    if (!ws_) return false;

    ws_->set_on_message([this](const std::string& msg) {
        if (on_message_) {
            on_message_(msg);
        }
    });
    
    if (ws_->connect()) {
        return true;
    }
    return false;
}

void SignalingClient::poll() {
    if (ws_) {
        ws_->poll();
    }
}

bool SignalingClient::send_message(const std::string& msg) {
    brls::Logger::info("Signaling Tx: {}", msg);
    if (ws_) {
        return ws_->send_message(msg);
    }
    return false;
}
