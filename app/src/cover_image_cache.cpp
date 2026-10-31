#include "cover_image_cache.hpp"

#include "gfn_client.hpp"
#include "http_client.hpp"
#include "gallery_request_state.hpp"
#include "image_http_session.hpp"
#include "network_resource_policy.hpp"
#include "runtime_journal.hpp"
#include "atomic_file_replace.hpp"
#include <atomic>
#include <chrono>
#include <stb_image.h>

#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#endif

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace opennow
{
namespace
{

constexpr const char* kFallbackCoverRes = "img/opennow_switch_icon.jpg";
constexpr size_t kMemoryCacheLimit = 16 * 1024 * 1024;
constexpr size_t kMaximumCachedImageSize = 6 * 1024 * 1024;
constexpr int kImageWorkerCount = 3;
constexpr int kThumbnailWidth = 420;
constexpr int kGalleryWidth = 1120;

enum class ImagePriority : int
{
    Prefetch = 0,
    Normal = 1,
    GalleryPrefetch = 2,
    Critical = 3,
};

using ImageData = std::shared_ptr<const std::string>;
using Completion = std::function<void(ImageData)>;

void EnsureImageCacheDirectory()
{
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SwitchNOW", 0777);
    mkdir("sdmc:/switch/SwitchNOW/cache", 0777);
    mkdir("sdmc:/switch/SwitchNOW/cache/images", 0777);
#endif
}

std::string HashUrl(const std::string& url)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : url)
    {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }

    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << hash;
    return stream.str();
}

std::string CachePathForUrl(const std::string& url)
{
    return "sdmc:/switch/SwitchNOW/cache/images/" + HashUrl(url) + ".img";
}

bool HasImageSignature(const std::string& data)
{
    if (data.size() >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xff &&
        static_cast<unsigned char>(data[1]) == 0xd8 &&
        static_cast<unsigned char>(data[2]) == 0xff)
    {
        return true;
    }

    static constexpr unsigned char kPngSignature[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    };
    if (data.size() >= sizeof(kPngSignature) &&
        std::equal(
            std::begin(kPngSignature), std::end(kPngSignature),
            reinterpret_cast<const unsigned char*>(data.data())))
    {
        return true;
    }

    if (data.size() >= 6 &&
        (data.compare(0, 6, "GIF87a") == 0 ||
         data.compare(0, 6, "GIF89a") == 0))
    {
        return true;
    }

    return data.size() >= 12 &&
           data.compare(0, 4, "RIFF") == 0 &&
           data.compare(8, 4, "WEBP") == 0;
}

bool ReadCachedImage(const std::string& path, std::string& data)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open())
        return false;

    const std::streampos size = stream.tellg();
    if (size <= 0 ||
        static_cast<std::uint64_t>(size) > kMaximumCachedImageSize)
    {
        stream.close();
        std::remove(path.c_str());
        return false;
    }

    stream.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(size));
    stream.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (static_cast<size_t>(stream.gcount()) != data.size()) { data.clear(); return false; }
    if (!data.empty() && HasImageSignature(data))
        return true;

    stream.close();
    std::remove(path.c_str());
    data.clear();
    return false;
}

std::atomic_uint64_t cache_write_sequence{0};
std::mutex cache_commit_mutex;

void WriteCachedImage(const std::string& path, const std::string& data)
{
    if (data.empty() || !HasImageSignature(data))
        return;

    EnsureImageCacheDirectory();
    const std::string temporary_path = path + ".tmp." + std::to_string(++cache_write_sequence);
    {
        std::ofstream stream(
            temporary_path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
            return;

        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        stream.flush();
        if (!stream.good())
        {
            stream.close();
            std::remove(temporary_path.c_str());
            return;
        }
    }

    // Serialize only the short commit. Readers and unrelated SD writes do not
    // wait for a whole image to be written, and concurrent writers never share a temp file.
    std::lock_guard<std::mutex> lock(cache_commit_mutex);
    if (!storage::ReplaceWithTemporaryFile(temporary_path, path))
        std::remove(temporary_path.c_str());
}

struct DecodedImage {
    int width = 0, height = 0;
    size_t bytes = 0;
    long long decode_us = 0;
    std::shared_ptr<unsigned char> pixels;
};
using DecodedData = std::shared_ptr<const DecodedImage>;
using PixelCompletion = std::function<void(DecodedData)>;

void ConfigureImageWorker(int index, int priority)
{
#ifdef __SWITCH__
    // Use only cores already permitted to this thread. Keep image work below
    // the UI/input priorities; the final allowed core is not preferred when
    // there are three or more choices (same policy as the Borealis worker pool).
    svcSetThreadPriority(CUR_THREAD_HANDLE, priority);
    s32 preferred=-1;u64 mask=0;
    if(R_SUCCEEDED(svcGetThreadCoreMask(&preferred,&mask,CUR_THREAD_HANDLE))){
        std::array<s32,4> cores{};size_t count=0;
        for(s32 core=0;core<4;++core)if(mask&(1ULL<<core))cores[count++]=core;
        if(count){size_t ordinal=static_cast<size_t>(index)%count;if(count>2&&ordinal==count-1)ordinal=count-2;
            if(cores[ordinal]!=preferred)svcSetThreadCoreMask(CUR_THREAD_HANDLE,cores[ordinal],static_cast<u32>(mask));}
    }
#else
    (void)index;(void)priority;
#endif
}

class ImageLoader
{
  public:
    explicit ImageLoader(std::function<void(const std::string&,const std::string&)> writer=WriteCachedImage) : cache_writer_(std::move(writer)) {
        for(int index=0;index<kImageWorkerCount;++index)
            workers_.emplace_back([this,index]{WorkerLoop(index);});
        writer_=std::thread([this]{WriterLoop();});
    }
    ~ImageLoader() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_=true;critical_queue_.clear();gallery_queue_.clear();normal_queue_.clear();prefetch_queue_.clear();writes_.clear();telemetry_.clear();
        }
        condition_.notify_all();write_condition_.notify_all();
        for(auto& thread:workers_)if(thread.joinable())thread.join();
        if(writer_.joinable())writer_.join();
    }
    void Request(const std::string& url,ImagePriority priority,Completion completion={},std::function<bool()> wanted={}) {
        Enqueue(url,priority,std::move(completion),{},std::move(wanted),false);
    }
    void RequestPixels(const std::string& url,ImagePriority priority,PixelCompletion completion={},std::function<bool()> wanted={}) {
        Enqueue(url,priority,{},std::move(completion),std::move(wanted),true);
    }
    void Trace(std::string category,std::string event,std::string detail) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopping_||telemetry_.size()>=64)return;
        telemetry_.push_back({std::move(category),std::move(event),std::move(detail)});write_condition_.notify_one();
    }
    void DropDecoded() {
        {std::lock_guard<std::mutex> lock(mutex_);decoded_cache_.clear();decoded_bytes_=0;++reset_generation_;}
        condition_.notify_all();
    }
  private:
    static constexpr size_t kDecodedLimit=12*1024*1024, kWriteLimit=8*1024*1024;
    struct MemoryEntry { ImageData data;std::uint64_t used=0; };
    struct PixelEntry { DecodedData data;std::uint64_t used=0; };
    struct PendingRequest {
        ImagePriority priority=ImagePriority::Normal;
        bool started=false,cache_requested=false,want_pixels=false,critical_unscoped=false;
        ImageData supplied;
        std::vector<std::function<bool()>> interested,critical_interested;
        std::vector<Completion> completions;
        std::vector<PixelCompletion> pixel_completions;
    };
    struct QueueEntry {std::string url;ImagePriority priority;};
    struct WriteJob {std::string path;ImageData data;};
    struct Telemetry {std::string category,event,detail;};
    bool Wanted(const PendingRequest& state) const {
        return state.cache_requested || std::any_of(state.interested.begin(),state.interested.end(),[](const auto& f){return f();});
    }
    bool CriticalWanted(const PendingRequest& state) const {
        return state.critical_unscoped || std::any_of(state.critical_interested.begin(),state.critical_interested.end(),[](const auto& f){return f();});
    }
    void Enqueue(const std::string& url,ImagePriority priority,Completion complete,PixelCompletion pixels,std::function<bool()> wanted,bool decode) {
        ImageData encoded_hit;DecodedData pixel_hit;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(stopping_)return;
            auto encoded=memory_cache_.find(url);auto decoded=decoded_cache_.find(url);
            if(decode&&decoded!=decoded_cache_.end()){decoded->second.used=++access_counter_;pixel_hit=decoded->second.data;}
            else if(!decode&&encoded!=memory_cache_.end()){encoded->second.used=++access_counter_;encoded_hit=encoded->second.data;}
            else {
                auto found=pending_.find(url);const bool fresh=found==pending_.end();
                auto state=fresh?std::make_shared<PendingRequest>():found->second;
                if(fresh){state->priority=priority;pending_.emplace(url,state);}
                if(encoded!=memory_cache_.end())state->supplied=encoded->second.data;
                if(priority==ImagePriority::Critical){if(wanted)state->critical_interested.push_back(wanted);else state->critical_unscoped=true;}
                if(wanted)state->interested.push_back(std::move(wanted));else state->cache_requested=true;
                if(complete)state->completions.push_back(std::move(complete));
                if(pixels)state->pixel_completions.push_back(std::move(pixels));
                state->want_pixels=state->want_pixels||decode;
                if(fresh||(!state->started&&static_cast<int>(priority)>static_cast<int>(state->priority))){
                    state->priority=priority;Queue(url,priority);condition_.notify_all();
                }
                return;
            }
        }
        if(complete)complete(std::move(encoded_hit));
        if(pixels)pixels(std::move(pixel_hit));
    }
    void Queue(const std::string& url,ImagePriority priority) {
        QueueEntry entry{url,priority};
        if(priority==ImagePriority::Critical)critical_queue_.push_back(std::move(entry));
        else if(priority==ImagePriority::GalleryPrefetch)gallery_queue_.push_back(std::move(entry));
        else if(priority==ImagePriority::Normal)normal_queue_.push_back(std::move(entry));
        else prefetch_queue_.push_back(std::move(entry));
    }
    bool PopNext(QueueEntry& entry,bool critical_only) {
        auto pop=[&](auto& queue){if(queue.empty())return false;entry=std::move(queue.front());queue.pop_front();return true;};
        return pop(critical_queue_)||pop(gallery_queue_)||(!critical_only&&(pop(normal_queue_)||pop(prefetch_queue_)));
    }
    ImageData Load(const std::string& url,ImageHttpSession& http,const std::function<bool()>& cancelled,bool& write,std::vector<std::string>& metrics) {
        try {
            if(cancelled())return {};
            std::string bytes;
            if(ReadCachedImage(CachePathForUrl(url),bytes))return std::make_shared<const std::string>(std::move(bytes));
            auto fetch=[&]{
                auto response=http.Fetch(url,kMaximumCachedImageSize,cancelled);
                metrics.push_back("key="+HashUrl(url)+" curl_code="+std::to_string(response.curl_code)+" status="+std::to_string(response.status)+
                    " bytes="+std::to_string(response.body.size())+" elapsed_ms="+std::to_string(response.elapsed_ms)+
                    " new_connections="+std::to_string(response.new_connections)+" dns_ms="+std::to_string(response.dns_ms)+
                    " connect_ms="+std::to_string(response.connect_ms)+" cancelled="+std::to_string(response.cancelled));
                return response;
            };
            auto response=fetch();
            // A new selection may revive this URL while curl is unwinding an
            // obsolete request. Give the new interested selection one retry.
            if(response.cancelled&&!cancelled())response=fetch();
            if(cancelled()||response.status!=200||response.body.empty()||!HasImageSignature(response.body))return {};
            write=true;return std::make_shared<const std::string>(std::move(response.body));
        }catch(...){return {};}
    }
    DecodedData Decode(const ImageData& data) {
        try {
        if(!data||data->empty())return {};
        const auto started=std::chrono::steady_clock::now();int width=0,height=0,channels=0;
        const auto* bytes=reinterpret_cast<const unsigned char*>(data->data());
        if(!stbi_info_from_memory(bytes,static_cast<int>(data->size()),&width,&height,&channels)||width<=0||height<=0||width>4096||height>4096||static_cast<std::uint64_t>(width)*height*4>16*1024*1024)return {};
        auto* raw=stbi_load_from_memory(bytes,static_cast<int>(data->size()),&width,&height,&channels,4);
        if(!raw)return {};
        auto owned=std::shared_ptr<unsigned char>(raw,stbi_image_free);
        auto result=std::make_shared<DecodedImage>();result->width=width;result->height=height;result->bytes=static_cast<size_t>(width)*height*4;
        result->pixels=std::move(owned);
        const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        result->decode_us=elapsed;
        return result;
        } catch (...) { return {}; }
    }
    void InsertMemoryCache(const std::string& url,const ImageData& data) {
        if(!data||data->empty()||data->size()>kMaximumCachedImageSize)return;
        const auto existing=memory_cache_.find(url);if(existing!=memory_cache_.end())memory_cache_bytes_-=existing->second.data->size();
        memory_cache_[url]={data,++access_counter_};memory_cache_bytes_+=data->size();
        while(memory_cache_bytes_>kMemoryCacheLimit&&memory_cache_.size()>1){
            auto oldest=std::min_element(memory_cache_.begin(),memory_cache_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
            memory_cache_bytes_-=oldest->second.data->size();memory_cache_.erase(oldest);
        }
    }
    void InsertDecoded(const std::string& url,const DecodedData& data) {
        if(!data||data->bytes>kDecodedLimit)return;
        const auto existing=decoded_cache_.find(url);if(existing!=decoded_cache_.end())decoded_bytes_-=existing->second.data->bytes;
        decoded_cache_[url]={data,++access_counter_};decoded_bytes_+=data->bytes;
        while(decoded_bytes_>kDecodedLimit||decoded_cache_.size()>4){
            auto oldest=std::min_element(decoded_cache_.begin(),decoded_cache_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
            decoded_bytes_-=oldest->second.data->bytes;decoded_cache_.erase(oldest);
        }
    }
    void QueueWrite(const std::string& url,const ImageData& data) {
        if(!data||data->size()>kWriteLimit)return;
        std::lock_guard<std::mutex> lock(mutex_);if(stopping_)return;
        while(!writes_.empty()&&(write_bytes_+data->size()>kWriteLimit||writes_.size()>=8)){
            write_bytes_-=writes_.front().data->size();writes_.pop_front();
        }
        writes_.push_back({CachePathForUrl(url),data});write_bytes_+=data->size();write_condition_.notify_one();
    }
    void WriterLoop() {
        ConfigureImageWorker(3, 0x3C);
        for(;;){
            WriteJob job;Telemetry log;bool telemetry=false;
            {std::unique_lock<std::mutex> lock(mutex_);write_condition_.wait(lock,[&]{return stopping_||!writes_.empty()||!telemetry_.empty();});if(stopping_)return;
                if(!telemetry_.empty()){log=std::move(telemetry_.front());telemetry_.pop_front();telemetry=true;}
                else {job=std::move(writes_.front());writes_.pop_front();write_bytes_-=job.data->size();}}
            try { if(telemetry)LogRuntimeEvent(log.category,log.event,log.detail);
            else cache_writer_(job.path,*job.data); } catch (...) {}
        }
    }
    void WorkerLoop(int index) {
        const bool critical_only=index==0;
        ConfigureImageWorker(index, critical_only?0x39:0x3B);
        ImageHttpSession http;auto last_use=std::chrono::steady_clock::now();std::uint64_t reset=0;
        for(;;){
            QueueEntry entry;std::shared_ptr<PendingRequest> state;ImageData data;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                const auto ready_predicate=[&]{return stopping_||reset!=reset_generation_||!critical_queue_.empty()||!gallery_queue_.empty()||(!critical_only&&(!normal_queue_.empty()||!prefetch_queue_.empty()));};
                bool ready=true;
                if(http.Active())ready=condition_.wait_for(lock,std::chrono::milliseconds(250),ready_predicate);
                else condition_.wait(lock,ready_predicate);
                if(stopping_)return;
                const bool reset_http=reset!=reset_generation_;reset=reset_generation_;
                if(!ready){lock.unlock();if(reset_http||network::launch_count.load()!=0||std::chrono::steady_clock::now()-last_use>std::chrono::seconds(2))http.Reset();continue;}
                if(reset_http){lock.unlock();http.Reset();lock.lock();if(stopping_)return;}
                if(!PopNext(entry,critical_only))continue;
                const auto pending=pending_.find(entry.url);
                if(pending==pending_.end()||pending->second->started||pending->second->priority!=entry.priority)continue;
                state=pending->second;
                if(!Wanted(*state)){pending_.erase(pending);continue;}
                state->started=true;data=state->supplied;
            }
            const auto cancelled=[this,state,critical_only]{std::lock_guard<std::mutex> lock(mutex_);return stopping_||(state->want_pixels&&network::launch_count.load()!=0)||!Wanted(*state)||
                (critical_only&&!CriticalWanted(*state)&&!critical_queue_.empty());};
            bool write=false;std::vector<std::string> metrics;
            if(!data)data=Load(entry.url,http,cancelled,write,metrics);
            last_use=std::chrono::steady_clock::now();
            DecodedData decoded;bool attempted=false;std::vector<Completion> completions;std::vector<PixelCompletion> pixel_completions;
            for(;;){
                std::unique_lock<std::mutex> lock(mutex_);if(stopping_)return;
                if(data&&state->want_pixels&&!attempted&&Wanted(*state)){
                    lock.unlock();decoded=Decode(data);attempted=true;continue;
                }
                if(data){try{InsertMemoryCache(entry.url,data);}catch(...){}}
                if(decoded&&Wanted(*state)){try{InsertDecoded(entry.url,decoded);}catch(...){}}
                completions=std::move(state->completions);pixel_completions=std::move(state->pixel_completions);pending_.erase(entry.url);break;
            }
            // Display/decode completion is dispatched before optional SD work.
            for(auto& complete:completions)if(complete){try{complete(data);}catch(...){}}
            for(auto& complete:pixel_completions)if(complete){try{complete(decoded);}catch(...){}}
            for(auto& detail:metrics)Trace("image","download",std::move(detail));
            if(attempted&&decoded)Trace("gallery","decode","key="+HashUrl(entry.url)+" decode_us="+std::to_string(decoded->decode_us)+" rgba_bytes="+std::to_string(decoded->bytes));
            if(write){try{QueueWrite(entry.url,data);}catch(...){}}
        }
    }
    std::function<void(const std::string&,const std::string&)> cache_writer_;
    std::mutex mutex_;std::condition_variable condition_,write_condition_;
    std::deque<QueueEntry> critical_queue_,gallery_queue_,normal_queue_,prefetch_queue_;
    std::deque<WriteJob> writes_;
    std::deque<Telemetry> telemetry_;
    std::unordered_map<std::string,std::shared_ptr<PendingRequest>> pending_;
    std::unordered_map<std::string,MemoryEntry> memory_cache_;
    std::unordered_map<std::string,PixelEntry> decoded_cache_;
    std::vector<std::thread> workers_;std::thread writer_;
    size_t memory_cache_bytes_=0,decoded_bytes_=0,write_bytes_=0;
    std::uint64_t access_counter_=0,reset_generation_=0;bool stopping_=false;
};

std::mutex g_loader_mutex;
ImageLoader* g_loader = nullptr;

ImageLoader& Loader()
{
    std::lock_guard<std::mutex> lock(g_loader_mutex);
    if (!g_loader)
        g_loader = new ImageLoader();
    return *g_loader;
}

void TraceGallery(std::string event,std::string detail)
{
    std::lock_guard<std::mutex> lock(g_loader_mutex);
    if(g_loader)g_loader->Trace("gallery",std::move(event),std::move(detail));
}

void SetCachedRemoteImage(
    brls::Image* image,
    const std::string& image_url,
    ImagePriority priority,
    std::function<bool()> should_apply = {})
{
    if (!image || image_url.empty())
        return;

    // Keep freeTexture=true. Replacing a resource-backed texture with an
    // async memory texture otherwise leaks GPU textures in Borealis.
    image->setImageAsync(
        [image_url, priority, should_apply = std::move(should_apply)](
            auto ready) mutable {
            Loader().Request(
                image_url,
                priority,
                [ready, should_apply = std::move(should_apply)](
                    ImageData data) mutable {
                    if (!data || (should_apply && !should_apply()))
                    {
                        ready({}, 0);
                        return;
                    }
                    ready(*data, data->size());
                });
        });
}

} // namespace

void SetCachedCoverImage(brls::Image* image, const std::string& image_url)
{
    if (!image)
        return;
    if (image_url.empty())
    {
        image->setImageFromRes(kFallbackCoverRes);
        return;
    }
    SetCachedRemoteImage(image, image_url, ImagePriority::Normal);
}

void SetCachedThumbnailImage(brls::Image* image, const std::string& image_url)
{
    if (!image)
        return;
    if (image_url.empty())
    {
        image->setImageFromRes(kFallbackCoverRes);
        return;
    }
    SetCachedRemoteImage(
        image,
        ResizeNvidiaImageUrl(image_url, kThumbnailWidth),
        ImagePriority::Normal);
}

struct GalleryImage::State
{
    const std::thread::id ui_thread = std::this_thread::get_id();
    GalleryRequestState requests;
    struct Entry { int texture; size_t bytes; std::uint64_t used; };
    std::unordered_map<std::string, Entry> textures;
    std::uint64_t counter = 0;
    size_t bytes = 0;
    std::mutex window_mutex;
    std::vector<std::string> window;
    bool active = true;
    bool Wants(const std::string& url) {
        std::lock_guard<std::mutex> lock(window_mutex);
        return active && std::find(window.begin(),window.end(),url)!=window.end();
    }
};

struct ThumbnailImage::State { GalleryRequestState requests; };
ThumbnailImage::ThumbnailImage() : state_(std::make_shared<State>()) {}
ThumbnailImage::~ThumbnailImage() { state_->requests.Close(); clear(); }
void ThumbnailImage::Release() {
    state_->requests.Begin();
    clear(); invalidate();
}
void ThumbnailImage::Load(const std::string& url) {
    const auto state = state_;
    const auto token = state->requests.Begin();
    if (url.empty()) { setImageFromRes(kFallbackCoverRes); return; }
    Loader().Request(ResizeNvidiaImageUrl(url, kThumbnailWidth), ImagePriority::Normal,
        [this, state, token](ImageData data) {
            if (!state->requests.IsCurrent(token)) return;
            brls::sync([this, state, token, data] {
                if (!state->requests.IsCurrent(token)) return;
                int w = 0, h = 0, channels = 0;
                if (!data || data->empty() || data->size() > 4 * 1024 * 1024 ||
                    !stbi_info_from_memory(reinterpret_cast<const unsigned char*>(data->data()),
                        static_cast<int>(data->size()), &w, &h, &channels) ||
                    w <= 0 || h <= 0 || w > 2048 || h > 2048) return;
                setImageFromMem(reinterpret_cast<const unsigned char*>(data->data()), static_cast<int>(data->size()));
            });
        });
}

GalleryImage::GalleryImage() : state_(std::make_shared<State>())
{
    setFreeTexture(false);
}
GalleryImage::~GalleryImage()
{
    state_->requests.Close();
    {std::lock_guard<std::mutex> lock(state_->window_mutex);state_->active=false;state_->window.clear();}
    clear();
    for(const auto& item:state_->textures)nvgDeleteImage(brls::Application::getNVGContext(),item.second.texture);
}
void GalleryImage::SetWindow(const std::vector<std::string>& urls)
{
    std::vector<std::string> resized;
    for(const auto& url:urls){auto value=ResizeNvidiaImageUrl(url,kGalleryWidth);if(!value.empty()&&std::find(resized.begin(),resized.end(),value)==resized.end())resized.push_back(std::move(value));if(resized.size()==4)break;}
    std::lock_guard<std::mutex> lock(state_->window_mutex);state_->window=std::move(resized);state_->active=true;
}
void GalleryImage::Pause()
{
    state_->requests.Begin();
    {std::lock_guard<std::mutex> lock(state_->window_mutex);state_->active=false;state_->window.clear();}
    std::lock_guard<std::mutex> lock(g_loader_mutex);
    if(g_loader)g_loader->DropDecoded(); // No loader is created merely to pause an empty gallery.
}
void GalleryImage::Prefetch(const std::string& image_url)
{
    const auto url=ResizeNvidiaImageUrl(image_url,kGalleryWidth);const auto state=state_;
    if(url.empty())return;
    Loader().RequestPixels(url,ImagePriority::GalleryPrefetch,{},[state,url]{return state->Wants(url);});
}
void GalleryImage::Load(const std::string& url,std::function<bool()> should_apply,std::function<void(bool)> complete)
{
    const auto state=state_;const auto token=state->requests.Begin();const auto start=std::chrono::steady_clock::now();
    {std::lock_guard<std::mutex> lock(state->window_mutex);if(std::find(state->window.begin(),state->window.end(),url)==state->window.end())state->window={url};state->active=true;}
    clear();invalidate();
    const auto cached=state->textures.find(url);
    if(cached!=state->textures.end()){
        cached->second.used=++state->counter;innerSetImage(cached->second.texture);
        TraceGallery("shown","key="+HashUrl(url)+" source=texture_cache upload_us=0");
        if(complete)complete(true);
        return;
    }
    if(url.empty()){if(complete)complete(false);return;}
    Loader().RequestPixels(url,ImagePriority::Critical,
        [this,state,token,url,start,should_apply=std::move(should_apply),complete=std::move(complete)](DecodedData data){
            if(!state->requests.IsCurrent(token))return;
            auto apply = [this,state,token,url,start,data,should_apply,complete]{
                if(!state->requests.IsCurrent(token)||(should_apply&&!should_apply()))return;
                const auto upload_start=std::chrono::steady_clock::now();
                // Only GPU texture creation remains on the UI thread. JPEG/PNG
                // decoding and pixel allocation have completed on an image worker.
                const int texture=data?nvgCreateImageRGBA(brls::Application::getNVGContext(),data->width,data->height,0,data->pixels.get()):0;
                if(texture){
                    while(!state->textures.empty()&&(state->textures.size()>=3||state->bytes+data->bytes>16*1024*1024)){
                        auto oldest=std::min_element(state->textures.begin(),state->textures.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
                        state->bytes-=oldest->second.bytes;nvgDeleteImage(brls::Application::getNVGContext(),oldest->second.texture);state->textures.erase(oldest);
                    }
                    state->textures.emplace(url,State::Entry{texture,data->bytes,++state->counter});state->bytes+=data->bytes;innerSetImage(texture);
                    const auto now=std::chrono::steady_clock::now();
                    TraceGallery("shown","key="+HashUrl(url)+" elapsed_us="+std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(now-start).count())+
                        " upload_us="+std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(now-upload_start).count()));
                }
                if(complete)complete(texture!=0);
            };
            // A decoded cache hit is delivered synchronously to Load() on the
            // UI thread. Upload it now, rather than adding an extra frame of latency.
            if (std::this_thread::get_id()==state->ui_thread) apply();
            else brls::sync(std::move(apply));
        },[state,url,token]{return state->requests.IsCurrent(token)&&state->Wants(url);});
}

void SetCachedGalleryImage(
    GalleryImage* image,
    const std::string& image_url,
    std::function<bool()> should_apply,
    std::function<void(bool)> complete)
{
    if (image) image->Load(ResizeNvidiaImageUrl(image_url, kGalleryWidth),
        std::move(should_apply), std::move(complete));
}

void PrefetchCachedGalleryImage(const std::string& image_url)
{
    if (image_url.empty())
        return;
    Loader().RequestPixels(
        ResizeNvidiaImageUrl(image_url, kGalleryWidth),
        ImagePriority::GalleryPrefetch);
}

void SetCachedAvatarImage(brls::Image* image, const std::string& image_url)
{
    SetCachedRemoteImage(image, image_url, ImagePriority::Normal);
}

std::string LoadCachedImageData(const std::string& image_url, bool refresh)
{
    if (image_url.empty())
        return {};

    std::string bytes;
    if (!refresh && ReadCachedImage(CachePathForUrl(image_url), bytes))
        return bytes;

    try
    {
        HttpClient http_client;
        std::vector<std::string> headers {"Accept: image/jpeg,image/png,image/*,*/*;q=0.8"};
        if (refresh) headers.push_back("Cache-Control: no-cache");
        const HttpResponse response = http_client.Get(
            image_url,
            GfnClient::kUserAgent,
            headers,
            refresh ? 2 : 5,
            refresh ? 4 : 15, kMaximumCachedImageSize);
        if (response.status_code != 200 || response.body.empty() ||
            response.body.size() > kMaximumCachedImageSize ||
            !HasImageSignature(response.body))
        {
            return {};
        }
        WriteCachedImage(CachePathForUrl(image_url), response.body);
        return response.body;
    }
    catch (...)
    {
        return {};
    }
}

std::string CachedImageFilePath(const std::string& image_url)
{
    return CachePathForUrl(image_url);
}

void ShutdownImageCache()
{
    ImageLoader* loader = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_loader_mutex);
        loader = std::exchange(g_loader, nullptr);
    }
    delete loader;
}

} // namespace opennow
