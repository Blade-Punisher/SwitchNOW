#include "signaling_client.hpp"
#include <cassert>
static bool write_result=true;
static unsigned sent=0;
WebSocketClient::WebSocketClient(const std::string& url):url_(url) {}
WebSocketClient::~WebSocketClient()=default;
bool WebSocketClient::connect() {connected_=true;return true;}
void WebSocketClient::disconnect() {connected_=false;}
void WebSocketClient::poll() {}
bool WebSocketClient::send_message(const std::string&) {++sent;return write_result;}
int main() {
    SignalingClient client("unused-test-endpoint");assert(client.connect());
    assert(client.send_message("registration"));
    write_result=false;assert(!client.send_message("ack"));
    assert(sent==2); // No new retry or hidden extra send is introduced.
}
