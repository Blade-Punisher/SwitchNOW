#pragma once
#include "models.hpp"
namespace opennow::game_feed {
inline GameInfo ToRecord(const PublicGame& source) {
 GameInfo result;
 result.id=source.id;
 result.uuid=source.uuid;
 result.launch_app_id=source.launch_app_id;
 result.title=source.title;
 result.publisher=source.publisher;
 result.developer=source.developer;
 result.description=source.description;
 result.image_url=source.image_url;
 result.poster_url=source.poster_url;
 result.hero_url=source.hero_url;
 result.screenshots=source.screenshots;
 result.genres=source.genres;
 result.feature_labels=source.feature_labels;
 result.content_ratings=source.content_ratings;
 result.supported_controls=source.supported_controls;
 result.membership_tier_label=source.membership_tier_label;
 result.is_in_library=source.is_in_library;
 result.variants=source.variants;
 if (!source.store.empty()) result.available_stores.push_back(source.store);
 return result;
}
inline PublicGame ToPublic(const GameInfo& source) {
 PublicGame result;
 result.id=source.id;
 result.uuid=source.uuid;
 result.launch_app_id=source.launch_app_id;
 result.title=source.title;
 result.publisher=source.publisher;
 result.developer=source.developer;
 result.description=source.description;
 result.image_url=source.image_url;
 result.poster_url=source.poster_url;
 result.hero_url=source.hero_url;
 result.screenshots=source.screenshots;
 result.genres=source.genres;
 result.feature_labels=source.feature_labels;
 result.content_ratings=source.content_ratings;
 result.supported_controls=source.supported_controls;
 result.membership_tier_label=source.membership_tier_label;
 result.is_in_library=source.is_in_library;
 result.variants=source.variants;
 if (!source.available_stores.empty()) result.store=source.available_stores.front();
 return result;
}
}
