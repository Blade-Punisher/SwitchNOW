#include "game_feed_cache.hpp"
#include "atomic_file_replace.hpp"
#include "game_feed_policy.hpp"
#include "http_cancellation.hpp"
#include <jansson.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>

namespace opennow::gfn::feed {
namespace {
using Json = std::unique_ptr<json_t, decltype(&json_decref)>;
std::mutex cache_mutex;
constexpr size_t kMaximumBytes = 16 * 1024 * 1024;
const std::pair<const char*, std::string GameInfo::*> strings[] = {
    {"id",&GameInfo::id},{"uuid",&GameInfo::uuid},{"launch_app_id",&GameInfo::launch_app_id},
    {"title",&GameInfo::title},{"description",&GameInfo::description},
    {"publisher",&GameInfo::publisher},{"developer",&GameInfo::developer},
    {"image_url",&GameInfo::image_url},{"poster_url",&GameInfo::poster_url},{"hero_url",&GameInfo::hero_url},
    {"membership_tier_label",&GameInfo::membership_tier_label},{"last_played",&GameInfo::last_played}
};
const std::pair<const char*, std::vector<std::string> GameInfo::*> lists[] = {
    {"screenshots",&GameInfo::screenshots},{"genres",&GameInfo::genres},{"feature_labels",&GameInfo::feature_labels},
    {"content_ratings",&GameInfo::content_ratings},{"supported_controls",&GameInfo::supported_controls},
    {"available_stores",&GameInfo::available_stores}
};
const std::pair<const char*, std::string GameVariant::*> variant_strings[] = {
    {"id",&GameVariant::id},{"store",&GameVariant::store},{"library_status",&GameVariant::library_status},
    {"last_played_date",&GameVariant::last_played_date},{"gfn_status",&GameVariant::gfn_status}
};
std::string String(json_t* value) { const auto* s=json_string_value(value);return s?s:""; }
json_t* Encode(const GameInfo& game) {
    auto* object=json_object();
    for(const auto& [name,member]:strings) json_object_set_new(object,name,json_string((game.*member).c_str()));
    for(const auto& [name,member]:lists) {
        auto* array=json_array();for(const auto& item:game.*member)json_array_append_new(array,json_string(item.c_str()));
        json_object_set_new(object,name,array);
    }
    json_object_set_new(object,"is_in_library",json_boolean(game.is_in_library));
    json_object_set_new(object,"selected_variant_index",json_integer(game.selected_variant_index));
    auto* variants=json_array();
    for(const auto& variant:game.variants) {
        auto* item=json_object();
        for(const auto& [name,member]:variant_strings)json_object_set_new(item,name,json_string((variant.*member).c_str()));
        json_object_set_new(item,"library_selected",json_boolean(variant.library_selected));
        json_array_append_new(variants,item);
    }
    json_object_set_new(object,"variants",variants);return object;
}
std::optional<CachedFeed> Read(const std::string& path,const std::string& owner) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)return {};
    const auto size=input.tellg();if(size<=0 || size>static_cast<std::streamoff>(kMaximumBytes))return {};
    std::string bytes(static_cast<size_t>(size),'\0');input.seekg(0);input.read(bytes.data(),size);if(!input)return {};
    Json root(json_loadb(bytes.data(),bytes.size(),JSON_REJECT_DUPLICATES,nullptr),json_decref);
    if(!root || json_integer_value(json_object_get(root.get(),"schema"))!=1 ||
        String(json_object_get(root.get(),"owner"))!=owner || !json_is_true(json_object_get(root.get(),"complete")))return {};
    CachedFeed result;
    result.scanned_at_ms=json_integer_value(json_object_get(root.get(),"scanned_at_ms"));
    result.updated_at_ms=json_integer_value(json_object_get(root.get(),"updated_at_ms"));
    auto* games=json_object_get(root.get(),"games");
    if(result.scanned_at_ms<=0 || result.updated_at_ms<result.scanned_at_ms || !json_is_array(games) || json_array_size(games)>10000)return {};
    size_t i;json_t* object;
    json_array_foreach(games,i,object) {
        GameInfo game;
        for(const auto& [name,member]:strings)game.*member=String(json_object_get(object,name));
        if(game.id.empty() || game.title.empty())return {};
        for(const auto& [name,member]:lists) {
            auto* array=json_object_get(object,name);
            if(!json_is_array(array) || json_array_size(array)>128)return {};
            size_t j;json_t* item;
            json_array_foreach(array,j,item) {if(!json_is_string(item))return {};(game.*member).push_back(String(item));}
        }
        game.is_in_library=json_is_true(json_object_get(object,"is_in_library"));
        auto* variants=json_object_get(object,"variants");
        if(!json_is_array(variants) || json_array_size(variants)>64)return {};
        size_t j;json_t* item;
        json_array_foreach(variants,j,item) {
            GameVariant variant;
            for(const auto& [name,member]:variant_strings)variant.*member=String(json_object_get(item,name));
            variant.library_selected=json_is_true(json_object_get(item,"library_selected"));
            game.variants.push_back(std::move(variant));
        }
        const auto selected=json_integer_value(json_object_get(object,"selected_variant_index"));
        game.selected_variant_index=selected>=0 && static_cast<size_t>(selected)<game.variants.size()?static_cast<size_t>(selected):0;
        result.games.push_back(std::move(game));
    }
    return result;
}
}
std::string CachePath(const std::string& directory,const std::string& owner) {
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char c:owner) {hash^=c;hash*=1099511628211ull;}
    std::ostringstream name;name<<directory<<"/game_feed_cache_"<<std::hex<<std::setw(16)<<std::setfill('0')<<hash<<".json";return name.str();
}
std::optional<CachedFeed> LoadCache(const std::string& directory,const std::string& owner) {
    if(owner.empty())return {};
    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto path=CachePath(directory,owner);
    if(auto cache=Read(path,owner))return cache;
    return Read(path+".bak",owner);
}
bool SaveCache(const std::string& directory,const std::string& owner,const CachedFeed& cache, std::optional<std::uint64_t> revision) {
    if(owner.empty() || cache.scanned_at_ms<=0 || cache.updated_at_ms<cache.scanned_at_ms || cache.games.size()>10000)return false;
    for (const auto& game : cache.games) {
        if (game.id.empty() || game.title.empty() || game.variants.size()>64) return false;
        for (const auto& entry : lists) if ((game.*entry.second).size()>128) return false;
    }
    const auto cancelled=HttpCancellationScope::Callback();
    std::lock_guard<std::mutex> lock(cache_mutex);
    if((cancelled&&cancelled()) || (revision && *revision!=game_feed::Revision()))return false;
    Json root(json_object(),json_decref);
    json_object_set_new(root.get(),"schema",json_integer(1));
    json_object_set_new(root.get(),"owner",json_string(owner.c_str()));
    json_object_set_new(root.get(),"complete",json_true());
    json_object_set_new(root.get(),"scanned_at_ms",json_integer(cache.scanned_at_ms));
    json_object_set_new(root.get(),"updated_at_ms",json_integer(cache.updated_at_ms));
    auto* games=json_array();for(const auto& game:cache.games)json_array_append_new(games,Encode(game));
    json_object_set_new(root.get(),"games",games);
    char* raw=json_dumps(root.get(),JSON_COMPACT);if(!raw)return false;
    std::string bytes(raw);free(raw);if(bytes.size()>kMaximumBytes)return false;
    if(cancelled&&cancelled())return false;
    const auto path=CachePath(directory,owner), temporary=path+".tmp";
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
    output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));output.flush();const bool written=output.good();output.close();
    if(!written || (cancelled&&cancelled())) {std::remove(temporary.c_str());return false;}
    return storage::ReplaceWithTemporaryFile(temporary,path);
}
void InvalidateAccount(const std::string& directory,const std::string& identity) {
    if(identity.empty())return;
    std::lock_guard<std::mutex> lock(cache_mutex);
    ++game_feed::revision;
    for(const auto* feed:{"store","library"}) {
        const auto path=CachePath(directory,game_feed::ScopeKey(feed,identity));
        std::remove(path.c_str());std::remove((path+".bak").c_str());
    }
}

}
