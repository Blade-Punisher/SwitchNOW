
// The real WebSocketClient implementation runs against deterministic curl I/O.
// No parser replacement or external network/session is used by this test.
#include "WebSocketClient.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
namespace {
std::string incoming;
std::vector<std::string> writes;
size_t read_limit=4096;
}
extern "C" {
CURL* curl_easy_init(void) { return reinterpret_cast<CURL*>(1); }
void curl_easy_cleanup(CURL*) {}
CURLcode curl_easy_setopt(CURL*,CURLoption,...) { return CURLE_OK; }
CURLcode curl_easy_perform(CURL*) { return CURLE_OK; }
CURLcode curl_easy_send(CURL*,const void* data,size_t n,size_t* sent) {
 writes.emplace_back(static_cast<const char*>(data),n);*sent=n;return CURLE_OK;
}
CURLcode curl_easy_recv(CURL*,void* data,size_t capacity,size_t* received) {
 *received=std::min({capacity,incoming.size(),read_limit});
 if(!*received) return CURLE_AGAIN;
 std::memcpy(data,incoming.data(),*received);incoming.erase(0,*received);return CURLE_OK;
}
const char* curl_easy_strerror(CURLcode) { return "deterministic transport"; }
}
static std::string Frame(unsigned opcode,const std::string& payload,bool masked=false) {
 std::string result(1,static_cast<char>(opcode));
 const auto n=static_cast<uint64_t>(payload.size());
 const unsigned mask=masked ? 128 : 0;
 if(n<126) result+=static_cast<char>(mask|n);
 else if(n<=65535) {
  result+=static_cast<char>(mask|126);result+=static_cast<char>(n>>8);result+=static_cast<char>(n);
 } else {
  result+=static_cast<char>(mask|127);
  for(int i=7;i>=0;--i) result+=static_cast<char>(n>>(i*8));
 }
 if(masked) {
  result+="abcd";
  for(size_t i=0;i<payload.size();++i) result+=static_cast<char>(payload[i]^"abcd"[i%4]);
 } else result+=payload;
 return result;
}
struct Harness {
 WebSocketClient ws {"wss://fixture.example/nvst/sign_in"};
 std::vector<std::string> messages;
 explicit Harness(const std::string& coalesced={},const std::string& protocol="x-nv-sessionid.private-fixture") {
  writes.clear();read_limit=4096;
  incoming="HTTP/1.1 101 Switching Protocols\r\nSec-WebSocket-Protocol: "+protocol+"\r\n\r\n"+coalesced;
  ws.set_custom_headers({"Sec-WebSocket-Protocol: x-nv-sessionid.private-fixture"});
  ws.set_on_message([this](const auto& message) {messages.push_back(message);});
  assert(ws.connect());
 }
 void Pump(unsigned count=160) {for(unsigned i=0;i<count && ws.is_connected();++i) ws.poll();}
 void Reject(const std::string& frames) {
  incoming+=frames;Pump();assert(!ws.is_connected());
  assert(ws.get_receive_diagnostics().find("protocolErrors=1")!=std::string::npos);
 }
};
int main() {
 const std::string offer=R"({"peer_msg":{"msg":"{\"type\":\"offer\",\"sdp\":\"fixture\"}"}})";
 for(const unsigned opcode : {0x81,0x82}) {
  Harness h(Frame(opcode,offer));h.Pump();assert(h.messages==std::vector<std::string>{offer});
  const auto stats=h.ws.get_receive_diagnostics();
  assert(stats.find(opcode==0x81 ? "framesText=1" : "framesBinary=1")!=std::string::npos);
  assert(stats.find("protocolMatched=1")!=std::string::npos);
  assert(stats.find("private-fixture")==std::string::npos);
 }
 for(const unsigned opcode : {0x01,0x02}) {
  Harness h;incoming=Frame(opcode,offer.substr(0,9));h.Pump();assert(h.messages.empty());
  assert(h.ws.get_receive_diagnostics().find("fragmentPending=1")!=std::string::npos);
  incoming=Frame(0x89,"ping")+Frame(0x8a,"pong")+Frame(0x00,offer.substr(9,7));
  h.Pump();assert(h.messages.empty());
  assert(std::any_of(writes.begin(),writes.end(),[](const auto& w) {
   return w.size()>=2 && static_cast<unsigned char>(w[0])==0x8a && (static_cast<unsigned char>(w[1])&128);
  }));
  incoming=Frame(0x80,offer.substr(16));h.Pump();assert(h.messages==std::vector<std::string>{offer});
  const auto stats=h.ws.get_receive_diagnostics();
  assert(stats.find("fragmentedMessages=1")!=std::string::npos);
  assert(stats.find("framesContinuation=2")!=std::string::npos);
  assert(stats.find("ping=1 pong=1")!=std::string::npos);
 }
 { Harness h;read_limit=1;incoming=Frame(0x82,offer,true);h.Pump();assert(h.messages==std::vector<std::string>{offer}); }
 { Harness h;const std::string large(70000,'x');incoming=Frame(0x81,large);h.Pump();assert(h.messages==std::vector<std::string>{large}); }
 { // Header/payload split across independent network poll calls.
  Harness h;const auto frame=Frame(0x81,offer);
  incoming=frame.substr(0,1);h.Pump();assert(h.messages.empty());
  incoming=frame.substr(1,5);h.Pump();assert(h.messages.empty());
  incoming=frame.substr(6);h.Pump();assert(h.messages==std::vector<std::string>{offer});
 }
 { // Retain the existing sixteen-frames-per-poll cap and ordering.
  Harness h;for(int i=0;i<1000;++i) incoming+=Frame(0x81,std::to_string(i));
  h.ws.poll();assert(h.messages.size()==16);
  h.Pump();assert(h.messages.size()==1000);
  for(int i=0;i<1000;++i) assert(h.messages[i]==std::to_string(i));
 }
 { Harness h;h.Reject(Frame(0x80,offer));assert(h.messages.empty()); }
 { Harness h;incoming=Frame(0x01,"partial");h.Pump();h.Reject(Frame(0x81,offer));assert(h.messages.empty()); }
 { Harness h;h.Reject(Frame(0x09,"invalid fragmented ping")); }
 { Harness h;h.Reject(Frame(0x83,"reserved opcode")); }
 { Harness h;h.Reject(Frame(0xc1,"unnegotiated extension")); }
 { Harness h;h.Reject(Frame(0x89,std::string(126,'x'))); }
 { Harness h;h.Reject(Frame(0x88,"x")); }
 { Harness h;
  std::string header {static_cast<char>(0x81),static_cast<char>(127)};
  const uint64_t oversized=4*1024*1024+1;
  for(int i=7;i>=0;--i) header+=static_cast<char>(oversized>>(i*8));
  h.Reject(header);
 }
 { // A per-frame limit alone must not permit an unbounded fragmented message.
  Harness h;incoming=Frame(0x02,std::string(4*1024*1024-16,'x'));h.Pump();assert(h.ws.is_connected());
  h.Reject(Frame(0x80,std::string(32,'x')));assert(h.messages.empty());
 }
 { Harness h;incoming=Frame(0x01,"partial")+Frame(0x88,std::string("\x03\xe8",2));h.Pump();
  assert(!h.ws.is_connected() && h.messages.empty());
  assert(h.ws.get_receive_diagnostics().find("fragmentPending=0")!=std::string::npos);
 }
 { Harness h({},"different-protocol");assert(h.ws.get_receive_diagnostics().find("protocolMatched=0")!=std::string::npos); }
 std::cout<<"Production WebSocket binary/fragmentation/bounds tests passed.\n";
}
