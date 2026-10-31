#include "server_zones.hpp"
#include "../server_zone_policy.hpp"
#include <jansson.h>
#include <memory>
namespace opennow::gfn {
namespace {
using Json = std::unique_ptr<json_t, decltype(&json_decref)>;
json_t* Data(json_t* root) {
    auto* data = json_object_get(root, "data");
    return json_is_true(json_object_get(root, "status")) && json_is_object(data) ? data : nullptr;
}
}
std::vector<StreamRegion> ParseServerZones(const std::string& queue_json,
    const std::string& mapping_json, std::int64_t now_ms, ZoneCatalogDiagnostics* diagnostics) {
    ZoneCatalogDiagnostics stats;
    if (diagnostics) *diagnostics = {};
    if (queue_json.size() > 512*1024 || mapping_json.size() > 512*1024) return {};
    Json queue(json_loads(queue_json.c_str(), 0, nullptr), &json_decref);
    Json mapping(json_loads(mapping_json.c_str(), 0, nullptr), &json_decref);
    auto* queues = Data(queue.get()); auto* meta = Data(mapping.get());
    if (!queues || !meta) return {};
    std::vector<StreamRegion> result;
    const char* key; json_t* value;
    json_object_foreach(queues, key, value) {
        if (result.size() >= 128) break;
        const std::string id = key;
        auto* details = json_object_get(meta, key);
        if (!server_zone::IsZoneId(id) || !json_is_object(value) || !json_is_object(details) ||
            json_is_true(json_object_get(details, "nuked"))) continue;
        if (auto* flag = json_object_get(details, "nuked"); flag && !json_is_boolean(flag)) continue;
        StreamRegion r;
        r.name = id; r.zone_id = id; r.url = server_zone::ZoneUrl(id); r.zone_catalog_at_ms = now_ms;
        auto* number = json_object_get(value, "QueuePosition");
        auto* updated = json_object_get(value, "Last Updated");
        if (json_is_integer(number) && json_integer_value(number) >= 0 && json_integer_value(number) <= 1000000 &&
            json_is_integer(updated)) {
            auto time = json_integer_value(updated);
            if (time > 0 && time < 100000000000LL) time *= 1000;
            if (time > 0 && time <= now_ms && (stats.newest_age_ms < 0 || now_ms - time < stats.newest_age_ms))
                stats.newest_age_ms = now_ms - time;
            if (server_location::IsProbeFresh(time, now_ms, 600000)) {
                ++stats.fresh;
                r.zone_queue_estimate = static_cast<int>(json_integer_value(number)); r.zone_queue_at_ms = time;
            } else if (time > now_ms) ++stats.future;
            else ++stats.stale;
        } else ++stats.invalid;
        result.push_back(std::move(r));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    if (diagnostics) *diagnostics = stats;
    return result;
}
} // namespace opennow::gfn
