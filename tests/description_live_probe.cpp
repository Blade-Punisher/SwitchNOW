// Manual live test: one short keyless request, never an authenticated GFN call.
#include "../app/src/http_client.cpp"
#include "description_translation.hpp"
#include "app_paths.hpp"
#include <filesystem>
#include <cassert>
#include <iostream>
namespace opennow {
const std::string& AppHomePath(){static const std::string p="build/host-tests/description-live";return p;}
void LogRuntimeEvent(const std::string&,const std::string&,const std::string&){}
std::uint64_t BeginRuntimeOperation(const std::string&,const std::string&,const std::string&){return 1;}
void EndRuntimeOperation(std::uint64_t,const std::string&,const std::string&,const std::string&,const std::string&){}
std::string SanitizeRuntimeUrl(const std::string& value){return value.substr(0,value.find('?'));}
}
int main(){
 using namespace opennow;curl_global_init(CURL_GLOBAL_DEFAULT);std::filesystem::create_directories(AppHomePath());
 const std::string original="Explore an open world and play with your friends.";int calls=0;
 auto transport=[&](const std::string& method,const std::string& url,const std::string& body){
  if(method=="POST")return description::Reply{200,R"({"data":{"english":{"items":[{"id":"live-probe","longDescription":"Explore an open world and play with your friends."}]},"localized":{"items":[{"id":"live-probe","longDescription":"Explore an open world and play with your friends."}]}}})"};
  ++calls;
  try {
   const auto response=HttpClient{}.Request(method,url,"SwitchNOW-description-test",{},body,3,15,65536);
   std::cout << "Live MyMemory HTTP status=" << response.status_code << " bytes=" << response.body.size() << "\n";
   return description::Reply{response.status_code,response.body};
  } catch(const std::exception& error) { std::cout << "Live transport error=" << error.what() << "\n"; throw; }
 };
 const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 const auto first=description::Resolve(AppHomePath(),"live-probe","ru",original,transport,[]{return false;},now);
 std::cout << "Live service result kind=" << first.kind << " reason=" << first.reason << "\n";
 assert(first.kind=="machine"&&!first.text.empty()&&first.text!=original);const int network=calls;
 const auto cached=description::Resolve(AppHomePath(),"live-probe","ru",original,transport,[]{return false;},now+30);
 assert(cached.text==first.text&&cached.reason=="cache"&&calls==network);
 std::cout<<"Production service + real HttpClient/MyMemory: translated text received; persistent cache reused; external requests="<<network<<".\n";
 curl_global_cleanup();
}
