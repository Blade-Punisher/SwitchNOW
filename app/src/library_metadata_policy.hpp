#pragma once
#include "models.hpp"
#include <unordered_map>
namespace opennow::game_feed {
inline bool NeedsLibraryMetadata(const GameInfo& game) {
    return game.description.empty() || game.screenshots.empty();
}
// Only reuse descriptive fields for IDs still returned by the server. Never
// resurrect removed games, ownership, variants, or old artwork URLs.
inline void ReuseLibraryMetadata(std::vector<GameInfo>& current,const std::vector<GameInfo>& cached) {
    std::unordered_map<std::string,const GameInfo*> by_id;
    for(const auto& game:cached)by_id.emplace(game.id,&game);
    for(auto& game:current) {
        const auto found=by_id.find(game.id);if(found==by_id.end())continue;
        const auto& old=*found->second;
        if(game.description.empty())game.description=old.description;
        if(game.publisher.empty())game.publisher=old.publisher;
        if(game.developer.empty())game.developer=old.developer;
        if(game.screenshots.empty())game.screenshots=old.screenshots;
        if(game.genres.empty())game.genres=old.genres;
        if(game.feature_labels.empty())game.feature_labels=old.feature_labels;
        if(game.content_ratings.empty())game.content_ratings=old.content_ratings;
        if(game.supported_controls.empty())game.supported_controls=old.supported_controls;
    }
}
}
