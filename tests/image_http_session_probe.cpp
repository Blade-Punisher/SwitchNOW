#include "image_http_session.hpp"
#include "runtime_journal.hpp"
#include <curl/curl.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
namespace opennow { void LogRuntimeEvent(const std::string&,const std::string&,const std::string&){} }
int main(int argc,char** argv){
    assert(argc==2);curl_global_init(CURL_GLOBAL_DEFAULT);
    const std::string root=argv[1];
    {
        opennow::ImageHttpSession session;
        const auto first=session.Fetch(root+"/first",1024,[]{return false;});
        const auto second=session.Fetch(root+"/second",1024,[]{return false;});
        assert(first.status==200&&first.body=="image-body"&&first.new_connections==1);
        assert(second.status==200&&second.body=="image-body"&&second.new_connections==0);
        assert(session.Active());
        const auto too_big=session.Fetch(root+"/oversize",4,[]{return false;});
        assert(too_big.body.empty());assert(!session.Active());
        const auto after_limit=session.Fetch(root+"/after-limit",1024,[]{return false;});
        assert(after_limit.body=="image-body"&&after_limit.new_connections==1);
        std::atomic<bool> cancel{false};
        std::thread timer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(150));cancel=true;});
        const auto start=std::chrono::steady_clock::now();
        const auto cancelled=session.Fetch(root+"/slow",1024,[&]{return cancel.load();});
        timer.join();
        assert(cancelled.cancelled&&cancelled.body.empty()&&!session.Active());
        assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(3));
        const auto recovered=session.Fetch(root+"/recovered",1024,[]{return false;});
        assert(recovered.status==200&&recovered.body=="image-body"&&recovered.new_connections==1);
        session.Reset();assert(!session.Active());
    }
    curl_global_cleanup();
    std::cout<<"Production image HTTP session: keepalive reused (1 then 0 connections), bounded body, cancellation and recovery passed.\n";
}
