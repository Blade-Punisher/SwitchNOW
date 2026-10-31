#include "../app/src/http_client.cpp" // Single TU avoids MinGW inline thread-local init linkage duplication.
#include "proxy_policy.hpp"
#include "runtime_journal.hpp"
#include "app_paths.hpp"
#include <curl/curl.h>
#include <iostream>
#include <cassert>
namespace opennow {
const std::string& AppHomePath(){static const std::string p="build/host-tests/proxy-log-fixture";return p;}
void LogRuntimeEvent(const std::string&,const std::string&,const std::string&){}
std::uint64_t BeginRuntimeOperation(const std::string&,const std::string&,const std::string&){return 1;}
void EndRuntimeOperation(std::uint64_t,const std::string&,const std::string&,const std::string&,const std::string&){}
std::string SanitizeRuntimeUrl(const std::string& value){return value;}
}
int main(int argc,char**argv){
 assert(argc==4 || argc==5);curl_global_init(CURL_GLOBAL_DEFAULT);
 opennow::StreamSettings s;s.proxy_enabled=true;s.proxy_url=std::string(argv[1])+"://127.0.0.1:"+argv[2];s.proxy_username="u@";s.proxy_password="p:#%";
 try {
 const auto response=opennow::HttpClient{}.Request("GET",argv[3],"proxy-test",{},"",3,5,1024,opennow::proxy::EnabledUrl(s));
 assert(response.status_code==200 && response.body=="proxy-ok");
 std::cout<<"Production HttpClient "<<argv[1]<<" request passed.\n";
 assert(argc==4);
 } catch(const std::exception& error) {
  if(argc!=5) throw;
  assert(std::string(error.what()).find("p:#%") == std::string::npos);
  std::cout<<"Production HttpClient CONNECT rejection handled.\n";
 }
 curl_global_cleanup();
}
