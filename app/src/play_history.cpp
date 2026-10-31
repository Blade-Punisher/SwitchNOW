#include "play_history.hpp"

#include "atomic_file_replace.hpp"

#include <jansson.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

namespace opennow
{
namespace
{

constexpr const char* kHistoryPath = "sdmc:/switch/SwitchNOW/play_history.json";

void EnsureAppDirectory()
{
#ifdef _WIN32
    _mkdir("sdmc:/switch");
    _mkdir("sdmc:/switch/SwitchNOW");
#else
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SwitchNOW", 0777);
#endif
}

std::string JsonString(json_t* object, const char* key)
{
    json_t* value = object ? json_object_get(object, key) : nullptr;
    return json_is_string(value) ? json_string_value(value) : std::string {};
}

std::string NewerTimestamp(const std::string& first, const std::string& second)
{
    if (first.empty())
        return second;
    if (second.empty())
        return first;
    return std::max(first, second);
}

} // namespace

std::string CurrentUtcIsoTimestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm utc {};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char value[32] {};
    std::strftime(value, sizeof(value), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return value;
}

PlayHistory LoadPlayHistory()
{
    PlayHistory history;
    json_error_t error {};
    json_t* root = json_load_file(kHistoryPath, 0, &error);
    if (!root)
        return history;

    json_t* entries = json_object_get(root, "entries");
    if (json_is_object(entries))
    {
        const char* id = nullptr;
        json_t* item = nullptr;
        json_object_foreach(entries, id, item)
        {
            if (!json_is_object(item))
                continue;
            const std::string timestamp = JsonString(item, "last_played_at");
            const std::string title = JsonString(item, "title");
            if (!timestamp.empty())
            {
                history.by_id[id] = timestamp;
                if (!title.empty())
                    history.by_title[title] = NewerTimestamp(history.by_title[title], timestamp);
            }
        }
    }
    json_decref(root);
    return history;
}

bool RecordGamePlayed(const std::string& game_id, const std::string& title,
                      const std::string& requested_timestamp)
{
    if (game_id.empty() && title.empty())
        return false;

    EnsureAppDirectory();
    json_error_t error {};
    json_t* root = json_load_file(kHistoryPath, 0, &error);
    if (!json_is_object(root))
    {
        if (root)
            json_decref(root);
        root = json_object();
    }

    json_t* entries = json_object_get(root, "entries");
    if (!json_is_object(entries))
    {
        entries = json_object();
        json_object_set_new(root, "entries", entries);
    }

    const std::string key = game_id.empty() ? "title:" + title : game_id;
    const std::string timestamp = requested_timestamp.empty()
        ? CurrentUtcIsoTimestamp() : requested_timestamp;
    json_t* item = json_object();
    json_object_set_new(item, "title", json_string(title.c_str()));
    json_object_set_new(item, "last_played_at", json_string(timestamp.c_str()));
    json_object_set_new(entries, key.c_str(), item);
    json_object_set_new(root, "schema_version", json_integer(1));

    const std::string temporary = std::string(kHistoryPath) + ".tmp";
    const bool dumped = json_dump_file(root, temporary.c_str(), JSON_INDENT(2) | JSON_SORT_KEYS) == 0;
    json_decref(root);
    if (!dumped)
    {
        std::remove(temporary.c_str());
        return false;
    }
    return storage::ReplaceWithTemporaryFile(temporary, kHistoryPath);
}

void ApplyPlayHistory(std::vector<GameInfo>& games, const PlayHistory& history)
{
    for (GameInfo& game : games)
    {
        std::string local;
        for (const std::string* id : {&game.id, &game.uuid})
        {
            const auto found = history.by_id.find(*id);
            if (found != history.by_id.end())
                local = NewerTimestamp(local, found->second);
        }
        const auto title = history.by_title.find(game.title);
        if (title != history.by_title.end())
            local = NewerTimestamp(local, title->second);
        game.last_played = NewerTimestamp(game.last_played, local);
    }
}

} // namespace opennow
