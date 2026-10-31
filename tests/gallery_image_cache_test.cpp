#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#undef STB_IMAGE_IMPLEMENTATION
#include "cover_image_cache.hpp"
#include "http_client.hpp"
#include <thread>
#include <atomic>
#include <cassert>
const auto gallery_ui_thread = std::this_thread::get_id();
std::atomic<int> gallery_decode_count{0};
std::atomic<bool> fail_gallery_decode{false};
unsigned char* TrackedGalleryDecode(const unsigned char* data,int size,int* w,int* h,int* channels,int wanted) {
    assert(std::this_thread::get_id()!=gallery_ui_thread);
    ++gallery_decode_count;
    if(fail_gallery_decode.exchange(false))throw std::bad_alloc();
    return stbi_load_from_memory(data,size,w,h,channels,wanted);
}
#define stbi_load_from_memory TrackedGalleryDecode
// One translation unit avoids MinGW duplicate TLS initialization symbols from
// the inline HttpCancellationScope variable while testing the production loader.
#include "../app/src/cover_image_cache.cpp"
#undef stbi_load_from_memory
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <map>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <set>
#include <thread>
using namespace std::chrono_literals;
namespace {
std::mutex network_mutex;
std::condition_variable network_cv;
std::map<std::string, int> hits;
std::set<int> textures;
std::string image;
bool release_covers = false, release_gallery = false;
int serial = 0, uploads = 0;
std::set<std::string> decoded_keys;
std::vector<unsigned char> expected_rgba;
int expected_width=0,expected_height=0;
std::atomic<int> cancelled_transfers{0},preempted_transfers{0};
const auto ui_thread = std::this_thread::get_id();
template<class Predicate> void Wait(Predicate ready) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!ready()) {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(1ms);
    }
}
bool Hit(const std::string& url) {
    std::lock_guard<std::mutex> lock(network_mutex); return hits[url] > 0;
}
}
int nvgCreateImageMem(NVGcontext*, int, unsigned char*, int) {
    assert(std::this_thread::get_id() == ui_thread);
    ++uploads; textures.insert(++serial); return serial;
}
int nvgCreateImageRGBA(NVGcontext*,int width,int height,int,const unsigned char* rgba) {
    assert(std::this_thread::get_id()==ui_thread);
    assert(width==expected_width && height==expected_height);
    assert(std::equal(expected_rgba.begin(),expected_rgba.end(),rgba));
    ++uploads;textures.insert(++serial);return serial;
}
void nvgDeleteImage(NVGcontext*, int id) {
    assert(std::this_thread::get_id() == ui_thread);
    assert(textures.erase(id) == 1);
}
namespace opennow {
void LogRuntimeEvent(const std::string& category,const std::string& event,const std::string& detail) {
    if(category=="gallery"&&event=="decode"){
        std::lock_guard<std::mutex> lock(network_mutex);
        decoded_keys.insert(detail.substr(4,16));
    }
}
struct ImageHttpSession::Impl {};
ImageHttpSession::ImageHttpSession() = default;
ImageHttpSession::~ImageHttpSession() = default;
void ImageHttpSession::Reset() {}
bool ImageHttpSession::Active() const {return false;}
ImageDownload ImageHttpSession::Fetch(const std::string& url,size_t limit,const std::function<bool()>& cancelled) {
    if(url=="cancelled-transfer"||url=="preemptible-prefetch") {
        std::unique_lock<std::mutex> lock(network_mutex);++hits[url];network_cv.notify_all();
        while(!cancelled())network_cv.wait_for(lock,1ms);
        if(url=="cancelled-transfer")++cancelled_transfers;else ++preempted_transfers;return {0,{},true,0};
    }
    const auto result=HttpClient{}.Get(url,"",{},5,15,limit);
    return {result.status_code,result.body,cancelled(),1};
}
HttpResponse HttpClient::Get(const std::string& url, const std::string&,
    const std::vector<std::string>&, long, long, size_t limit) const {
    assert(limit == 6 * 1024 * 1024);
    std::unique_lock<std::mutex> lock(network_mutex);
    ++hits[url]; network_cv.notify_all();
    if (url.find("cover") != std::string::npos)
        network_cv.wait(lock, [] { return release_covers; });
    if (url == "blocked") network_cv.wait(lock, [] { return release_gallery; });
    return {200, url == "broken" ? std::string("invalid") : image, {}, {}};
}
}
int main() {
    // Decode metadata from a real repository JPEG.
    std::ifstream input(std::filesystem::path(__FILE__).parent_path().parent_path() /
        "resources/icon/icon.jpg", std::ios::binary);
    image.assign(std::istreambuf_iterator<char>(input), {});
    assert(!image.empty());
    int channels=0;
    auto* reference=stbi_load_from_memory(reinterpret_cast<const unsigned char*>(image.data()),static_cast<int>(image.size()),&expected_width,&expected_height,&channels,4);
    assert(reference);expected_rgba.assign(reference,reference+static_cast<size_t>(expected_width)*expected_height*4);stbi_image_free(reference);
    const auto disk_path=std::filesystem::path("build/host-tests/gallery-read.jpg");
    {std::ofstream file(disk_path,std::ios::binary);file.write(image.data(),image.size());}
    std::string read_back;assert(opennow::ReadCachedImage(disk_path.string(),read_back)&&read_back==image);
    const std::string concurrent_path="build/host-tests/gallery-concurrent.img";
    const auto payload_a=image+std::string(800,'a'),payload_b=image+std::string(1500,'b');
    std::thread writer_a([&]{for(int n=0;n<10;++n)opennow::WriteCachedImage(concurrent_path,payload_a);});
    std::thread writer_b([&]{for(int n=0;n<10;++n)opennow::WriteCachedImage(concurrent_path,payload_b);});
    writer_a.join();writer_b.join();
    assert(opennow::ReadCachedImage(concurrent_path,read_back)&&(read_back==payload_a||read_back==payload_b));
    brls::Image cover_a, cover_b;
    opennow::SetCachedCoverImage(&cover_a, "cover-a");
    opennow::SetCachedCoverImage(&cover_b, "cover-b");
    Wait([] { return Hit("cover-a") && Hit("cover-b"); });
    {
        opennow::GalleryImage gallery;
        gallery.SetWindow({"reserved-visible","reserved-neighbor"});int completed=0;
        gallery.Load("reserved-visible",{},[&](bool ok){completed=ok?1:-1;});
        Wait([&]{brls::Pump();return completed==1;});
        gallery.Prefetch("reserved-neighbor");
        // The two catalogue workers are still occupied: the reserved worker
        // must warm a neighbor instead of sitting idle.
        Wait([]{std::lock_guard<std::mutex> lock(network_mutex);return decoded_keys.contains(opennow::HashUrl("reserved-neighbor"));});
        gallery.SetWindow({"reserved-selected","preemptible-prefetch"});
        gallery.Prefetch("preemptible-prefetch");Wait([]{return Hit("preemptible-prefetch");});
        completed=0;gallery.Load("reserved-selected",{},[&](bool ok){completed=ok?1:-1;});
        Wait([&]{brls::Pump();return completed==1;});
        assert(preempted_transfers.load()==1); // Visible image preempts a still-wanted optional neighbor.
    }
    brls::Pump();assert(textures.empty());uploads=0;
    {
        opennow::GalleryImage gallery;
        int completed = 0;
        auto select = [&](const std::string& url) {
            opennow::SetCachedGalleryImage(&gallery, url, {}, [&](bool ok) {
                completed = ok ? 1 : -1;
            });
        };
        // Two occupied catalogue workers cannot block the selected screenshot.
        select("blocked");
        Wait([] { return Hit("blocked"); });
        select("skipped");
        select("first");
        { std::lock_guard<std::mutex> lock(network_mutex); release_gallery = true; }
        network_cv.notify_all();
        Wait([] { return Hit("first"); });
        Wait([] { std::lock_guard<std::mutex> lock(brls::ui_mutex); return !brls::ui_jobs.empty(); });
        std::function<void()> old_ui;
        {
            std::lock_guard<std::mutex> lock(brls::ui_mutex);
            old_ui = std::move(brls::ui_jobs.front());
            brls::ui_jobs.erase(brls::ui_jobs.begin());
        }
        assert(!Hit("skipped"));
        select("second");
        // A's UI job was queued before B was selected. It must still be rejected.
        old_ui(); assert(gallery.getTexture() == 0 && completed == 0);
        Wait([&] { brls::Pump(); return completed == 1; });
        assert(uploads == 1);
        const int second_texture = gallery.getTexture();
        for (auto url : {"third", "fourth"}) {
            completed = 0; select(url);
            assert(gallery.getTexture() == 0);
            Wait([&] { brls::Pump(); return completed == 1; });
        }
        assert(textures.size() == 3);
        completed = 0; select("second");
        assert(completed == 1 && gallery.getTexture() == second_texture && uploads == 3);
        completed = 0; select("fifth");
        Wait([&] { brls::Pump(); return completed == 1; });
        assert(textures.size() == 3 && uploads == 4);
        completed = 0; select("broken");
        Wait([&] { brls::Pump(); return completed == -1; });
        assert(gallery.getTexture() == 0 && textures.size() == 3);
        completed = 0; select("closed");
        Wait([] { return Hit("closed"); });
    }
    // A completion queued for a destroyed view cannot upload or invoke callbacks.
    brls::Pump(); assert(textures.empty());
    {
        std::lock_guard<std::mutex> lock(network_mutex); release_covers = true;
    }
    network_cv.notify_all();
    {
        opennow::GalleryImage gallery;
        gallery.SetWindow({"warm-visible","warm-neighbor"});
        int completed=0;
        gallery.Load("warm-visible",{},[&](bool ok){completed=ok?1:-1;});
        Wait([&]{brls::Pump();return completed==1;});
        gallery.Prefetch("warm-neighbor");
        Wait([]{std::lock_guard<std::mutex> lock(network_mutex);return decoded_keys.contains(opennow::HashUrl("warm-neighbor"));});
        const auto before=gallery_decode_count.load();completed=0;
        gallery.Load("warm-neighbor",{},[&](bool ok){completed=ok?1:-1;});
        assert(completed==1); // CPU-warm neighbor is uploaded in this UI turn.
        Wait([&]{brls::Pump();return completed==1;});
        assert(gallery_decode_count.load()==before); // Neighbor is already decoded, not just downloaded.
        gallery.SetWindow({"cancelled-transfer"});completed=0;
        gallery.Load("cancelled-transfer",{},[&](bool ok){completed=ok?1:-1;});
        Wait([]{return Hit("cancelled-transfer");});
        gallery.SetWindow({"latest-visible"});
        gallery.Load("latest-visible",{},[&](bool ok){completed=ok?1:-1;});
        Wait([&]{brls::Pump();return completed==1;});
        assert(cancelled_transfers.load()==1);
        fail_gallery_decode=true;completed=0;gallery.SetWindow({"allocation-failure"});
        gallery.Load("allocation-failure",{},[&](bool ok){completed=ok?1:-1;});
        Wait([&]{brls::Pump();return completed==-1;});
        completed=0;gallery.Load("allocation-recovered",{},[&](bool ok){completed=ok?1:-1;});
        Wait([&]{brls::Pump();return completed==1;});
        gallery.Pause();
    }
    brls::Pump();assert(textures.empty());
    {
        std::atomic<bool> writer_entered{false},release_writer{false},first_ready{false},second_ready{false};
        opennow::ImageLoader isolated([&](const auto&,const auto&){writer_entered=true;while(!release_writer.load())std::this_thread::sleep_for(1ms);});
        isolated.RequestPixels("slow-sd-first",opennow::ImagePriority::Critical,[&](auto data){assert(data);first_ready=true;});
        Wait([&]{return first_ready.load()&&writer_entered.load();});
        isolated.RequestPixels("slow-sd-second",opennow::ImagePriority::Critical,[&](auto data){assert(data);second_ready=true;});
        Wait([&]{return second_ready.load();}); // SD writer is still blocked; display/decode workers advance.
        release_writer=true;
    }
    {
        opennow::ThumbnailImage thumbnail;
        thumbnail.Load("thumbnail-visible");
        Wait([&] { brls::Pump(); return thumbnail.getTexture()!=0; });
        assert(textures.size()==1);
        thumbnail.Release();assert(textures.empty());
        thumbnail.Load("thumbnail-stale");
        Wait([] { std::lock_guard<std::mutex> lock(brls::ui_mutex); return !brls::ui_jobs.empty(); });
        const int before=uploads;
        thumbnail.Release();brls::Pump();
        assert(uploads==before && textures.empty());
        thumbnail.Load("thumbnail-visible"); // encoded cache, a new UI-safe texture
        Wait([&] { brls::Pump(); return thumbnail.getTexture()!=0; });
    }
    assert(textures.empty());
    {
        auto thumbnail=std::make_unique<opennow::ThumbnailImage>();
        thumbnail->Load("thumbnail-closed");
        Wait([] { std::lock_guard<std::mutex> lock(brls::ui_mutex); return !brls::ui_jobs.empty(); });
        const int before=uploads;thumbnail.reset();brls::Pump();
        assert(uploads==before && textures.empty());
    }
    opennow::ShutdownImageCache();
    brls::Pump(); assert(textures.empty());
    puts("Production gallery and viewport thumbnail lifetime/release/stale-completion tests passed.");
    puts("Gallery off-UI decode/pixel equality/warm-neighbor/cancel/slow-SD independence tests passed.");
}
