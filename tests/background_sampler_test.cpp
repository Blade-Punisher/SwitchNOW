#include "background_sampler.hpp"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <future>
using namespace std::chrono_literals;
int main(){
    std::mutex gate;
    std::condition_variable wake;
    bool released=false;
    std::promise<void> blocked;
    std::atomic<int> calls{0};
    {
        opennow::BackgroundSampler<int> sampler([&]{
            const auto n=++calls;
            if(n==2){
                blocked.set_value();
                std::unique_lock<std::mutex> lock(gate);
                wake.wait(lock,[&]{return released;});
            }
            if(n==3) throw 1; // Failure must preserve the last valid value.
            return n*10;
        },60s);
        const auto end=std::chrono::steady_clock::now()+5s;
        // Read() intentionally returns an invalid snapshot on try-lock contention.
        // Assert the snapshot which passed the wait, rather than racing a second Read.
        decltype(sampler.Read()) first;
        while(!(first=sampler.Read()).valid){assert(std::chrono::steady_clock::now()<end);std::this_thread::yield();}
        assert(first.value==10);
        sampler.Request();
        assert(blocked.get_future().wait_for(5s)==std::future_status::ready);
        // Deterministic proof: reading cannot wait for the in-flight OS query,
        // since that query only gets released after this Read has completed.
        auto reader=std::async(std::launch::async,[&]{return sampler.Read();});
        assert(reader.wait_for(5s)==std::future_status::ready);
        assert(reader.get().value==10);
        {std::lock_guard<std::mutex> lock(gate);released=true;}
        wake.notify_all();
        while(sampler.Read().value!=20){assert(std::chrono::steady_clock::now()<end);std::this_thread::yield();}
        sampler.Request();
        while(calls.load()<3){assert(std::chrono::steady_clock::now()<end);std::this_thread::yield();}
        assert(sampler.Read().value==20);
    }
    opennow::BackgroundSampler<int> unavailable([]()->int{throw 1;},60s);
    assert(!unavailable.Read().valid);
}
