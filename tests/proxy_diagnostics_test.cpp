#include "proxy_diagnostics.hpp"
#include "app_paths.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cassert>
#include <iostream>
namespace opennow { const std::string& AppHomePath(){static const std::string p="build/host-tests/proxy-log-fixture";return p;} }
int main() {
 using namespace opennow;std::filesystem::create_directories(AppHomePath());
 const auto path=AppHomePath()+"/proxy.log";std::filesystem::remove(path);
 StreamSettings s;s.proxy_enabled=true;s.proxy_url="socks5h://localhost:1080";s.proxy_username="secret-user";s.proxy_password="top-secret";
 proxy_diagnostics::Configuration(s);
 {proxy_diagnostics::Request r("POST","https://example.com/private?token=private-secret","http://secret-user:top-secret@localhost:8080");r.Finish(0,200,50);}
 {proxy_diagnostics::Request r("GET","https://example.com/","socks5h://secret-user:top-secret@localhost:1080");r.Finish(5,0,20);}
 {proxy_diagnostics::Request r("GET","https://example.com/","http://localhost:8080");}
 {proxy_diagnostics::Request r("GET","https://example.com/","");assert(!r.Active());}
 std::ifstream in(path);const std::string data((std::istreambuf_iterator<char>(in)),{});in.close();
 assert(data.find("top-secret")==std::string::npos && data.find("secret-user")==std::string::npos && data.find("private-secret")==std::string::npos);
 assert(data.find("endpoint=http://localhost:8080")!=std::string::npos);
 assert(data.find("curl_code=5")!=std::string::npos && data.find("event=aborted")!=std::string::npos);
 std::ofstream(path)<<std::string(270000,'x');proxy_diagnostics::Configuration(s);
 assert(std::filesystem::exists(AppHomePath()+"/proxy.previous.log"));assert(std::filesystem::file_size(path)<1000);
 std::cout<<"Proxy diagnostics redaction/status/abort/disabled/rotation tests passed.\n";
}
