#include "app_state.hpp"
#include "game_feed_policy.hpp"
#include "owned_game_policy.hpp"

namespace opennow
{

AppState& AppState::Instance()
{
    static AppState state;
    return state;
}

bool AppState::HasProviders() const
{
    return !providers_.empty();
}

bool AppState::HasPublicGames() const
{
    return !public_games_.empty();
}

bool AppState::HasLibraryGames() const
{
    return !library_games_.empty();
}

bool AppState::IsSessionLoaded() const
{
    return session_loaded_;
}

bool AppState::HasSession() const
{
    return session_.has_value();
}

const std::vector<LoginProvider>& AppState::providers() const
{
    return providers_;
}

const std::vector<PublicGame>& AppState::public_games() const
{
    return public_games_;
}

const std::vector<GameInfo>& AppState::library_games() const
{
    return library_games_;
}

const std::optional<AuthSession>& AppState::session() const
{
    return session_;
}

void AppState::SetProviders(std::vector<LoginProvider> providers)
{
    providers_ = std::move(providers);
}

void AppState::SetPublicGames(std::vector<PublicGame> games)
{
    public_games_ = std::move(games);
    ApplyConfirmedOwnership(public_games_);
}

void AppState::SetLibraryGames(std::vector<GameInfo> games,bool complete)
{
    // Retire temporary overlays as soon as a server snapshot confirms every
    // manually added store version. Subsequent server removals then win.
    std::erase_if(confirmed_owned_,[&](const GameInfo& known){
        auto found=std::find_if(games.begin(),games.end(),[&](const auto& game){return game.id==known.id;});
        if(found==games.end())return false;
        for(const auto& variant:known.variants)if(ownership::Owned(variant)) {
            const auto match=std::find_if(found->variants.begin(),found->variants.end(),[&](const auto& item){return item.id==variant.id && ownership::Owned(item);});
            if(match==found->variants.end())return false;
        }
        return true;
    });
    std::erase_if(confirmed_unowned_,[&](const auto& removed){return ownership::RemovalObserved(games,removed,complete);});
    library_games_ = std::move(games);
    for(const auto& known:confirmed_owned_)ownership::Merge(library_games_,known);
    for(const auto& removed:confirmed_unowned_)ownership::Remove(library_games_,removed);
}

void AppState::ApplyConfirmedOwnership(std::vector<PublicGame>& games) const
{
    for(const auto& known:confirmed_owned_)ownership::Apply(games,known);
    for(const auto& removed:confirmed_unowned_)ownership::Remove(games,removed);
}

void AppState::MarkGameOwned(GameInfo confirmed,const std::string& variant_id)
{
    if(!session_ || confirmed.id.empty() || !confirmed.is_in_library)return;
    std::erase_if(confirmed_unowned_,[&](const auto& removed){return removed.app_id==confirmed.id && (variant_id.empty() || removed.variant_id==variant_id) && std::any_of(confirmed.variants.begin(),confirmed.variants.end(),[&](const auto& variant){return variant.id==removed.variant_id && ownership::Owned(variant);});});
    GameInfo intent=confirmed;
    if(!variant_id.empty())for(auto& variant:intent.variants)if(variant.id!=variant_id)variant.library_status="NOT_OWNED";
    ownership::Merge(confirmed_owned_,intent);
    ownership::Merge(library_games_,confirmed);
    ownership::Apply(public_games_,intent);
    for(const auto& removed:confirmed_unowned_){ownership::Remove(library_games_,removed);ownership::Remove(public_games_,removed);}
}

void AppState::MarkGameUnowned(const std::string& app_id,const std::string& variant_id)
{
    if(!session_ || app_id.empty() || variant_id.empty())return;
    const ownership::RemovedVariant removed{app_id,variant_id};
    ownership::Remove(confirmed_owned_,removed);
    std::erase_if(confirmed_unowned_,[&](const auto& entry){return entry.app_id==app_id && entry.variant_id==variant_id;});
    confirmed_unowned_.push_back(removed);
    ownership::Remove(library_games_,removed);ownership::Remove(public_games_,removed);
}

void AppState::MarkGamePlayed(const std::string& game_id, const std::string& title,
                              const std::string& timestamp)
{
    for (GameInfo& game : library_games_)
    {
        if ((!game_id.empty() && (game.id == game_id || game.uuid == game_id)) ||
            (!title.empty() && game.title == title))
            game.last_played = timestamp;
    }
}

void AppState::SetSession(AuthSession session)
{
    if (!session_ || game_feed::OwnerKey(session_->user.user_id, session_->provider.code, session_->provider.streaming_service_url) !=
        game_feed::OwnerKey(session.user.user_id, session.provider.code, session.provider.streaming_service_url)) {
        library_games_.clear(); public_games_.clear(); confirmed_owned_.clear(); confirmed_unowned_.clear();
    }
    session_        = std::move(session);
    session_loaded_ = true;
}

void AppState::ClearSession()
{
    session_.reset();
    library_games_.clear(); public_games_.clear(); confirmed_owned_.clear(); confirmed_unowned_.clear();
    session_loaded_ = true;
}

void AppState::MarkSessionLoaded()
{
    session_loaded_ = true;
}

} // namespace opennow
