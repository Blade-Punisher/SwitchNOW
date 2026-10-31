#include "library_catalog.hpp"
#include <jansson.h>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <cstdlib>
#include <cstdint>

namespace opennow::gfn::library {
namespace {
using Json = std::unique_ptr<json_t, decltype(&json_decref)>;
std::string String(json_t* parent, const char* name) {
    const char* value = json_string_value(json_object_get(parent, name));
    return value ? value : "";
}
std::string Dump(json_t* value) {
    char* text = json_dumps(value, JSON_COMPACT);
    if (!text) throw std::runtime_error("Library JSON serialization failed");
    std::string result(text); free(text); return result;
}
}
bool OwnedStatus(const std::string& status) {
    return status == "MANUAL" || status == "PLATFORM_SYNC" || status == "IN_LIBRARY";
}
std::string RequestBody(const std::string& vpc, const std::string& cursor) {
    // Same supported apps API as Store, with deterministic ordering. Do not
    // invent a library filter field: inspect authenticated per-variant status.
    static const char* query = R"GQL(query SwitchNOWCompleteLibrary($vpcId: String!, $locale: String!, $cursor: String!, $fetchCount: Int!, $filters: AppFilterFields!) {
      apps(vpcId: $vpcId, language: $locale, orderBy: "sortName:ASC", first: $fetchCount, after: $cursor, filters: $filters) {
        pageInfo { hasNextPage endCursor totalCount }
        items { id title
          images { KEY_ART GAME_BOX_ART TV_BANNER HERO_IMAGE }
          variants { id appStore gfn { status library { status selected } } }
          gfn { playabilityState minimumMembershipTierLabel }
        }
      }
    })GQL";
    Json body(json_object(), json_decref), variables(json_object(), json_decref);
    json_object_set_new(body.get(), "query", json_string(query));
    json_object_set_new(variables.get(), "vpcId", json_string(vpc.c_str()));
    json_object_set_new(variables.get(), "locale", json_string("en_US"));
    json_object_set_new(variables.get(), "cursor", json_string(cursor.c_str()));
    json_object_set_new(variables.get(), "fetchCount", json_integer(200));
    json_object_set_new(variables.get(), "filters", json_object());
    json_object_set(body.get(), "variables", variables.get());
    return Dump(body.get());
}
ScanResult Scan(const std::function<std::string(const std::string&)>& fetch_page,
    const std::function<void(const ScanResult&)>& progress) {
    ScanResult result;
    std::string cursor;
    std::unordered_set<std::string> cursors, ids, seen_apps;
    for (size_t page = 0; page < 96; ++page) {
        const auto response = fetch_page(cursor);
        Json root(json_loads(response.c_str(), 0, nullptr), json_decref);
        if (!root) throw std::runtime_error("Invalid library catalog JSON");
        json_t* errors = json_object_get(root.get(), "errors");
        if (json_is_array(errors) && json_array_size(errors))
            throw std::runtime_error("Library catalog GraphQL rejected request");
        json_t* apps = json_object_get(json_object_get(root.get(), "data"), "apps");
        json_t* items = json_object_get(apps, "items");
        json_t* info = json_object_get(apps, "pageInfo");
        if (!json_is_array(items) || !json_is_object(info) ||
            !json_is_boolean(json_object_get(info, "hasNextPage")))
            throw std::runtime_error("Incomplete library catalog response");
        size_t i; json_t* app;
        json_array_foreach(items, i, app) {
            ++result.scanned;
            const auto id = String(app, "id");
            if (!id.empty()) seen_apps.insert(id);
            bool owned = false;
            json_t* variants = json_object_get(app, "variants");
            if (json_is_array(variants)) {
                size_t v; json_t* variant;
                json_array_foreach(variants, v, variant) {
                    json_t* library = json_object_get(json_object_get(variant, "gfn"), "library");
                    if (OwnedStatus(String(library, "status"))) owned = true;
                }
            }
            if (owned && !id.empty() && !String(app, "title").empty() && ids.insert(id).second)
                result.owned_apps.push_back(Dump(app));
        }
        ++result.pages;
        if (progress) progress(result);
        if (json_is_false(json_object_get(info, "hasNextPage"))) {
            const auto total = json_object_get(info, "totalCount");
            if (json_is_integer(total) && json_integer_value(total) > 0 &&
                static_cast<uint64_t>(json_integer_value(total)) > seen_apps.size())
                throw std::runtime_error("Library catalog ended before reported totalCount");
            return result;
        }
        auto next = String(info, "endCursor");
        if (next.empty() || next == cursor || !cursors.insert(next).second)
            throw std::runtime_error("Library catalog pagination did not advance");
        cursor = std::move(next);
    }
    throw std::runtime_error("Library catalog pagination limit reached");
}
}
