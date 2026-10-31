#pragma once
#include "models.hpp"
#include <algorithm>
namespace opennow::ownership {
inline bool Owned(const GameVariant& v){return v.library_status=="MANUAL" || v.library_status=="PLATFORM_SYNC" || v.library_status=="IN_LIBRARY";}
inline void Merge(std::vector<GameInfo>& games,const GameInfo& confirmed){
 auto found=std::find_if(games.begin(),games.end(),[&](const auto& g){return g.id==confirmed.id;});
 if(found==games.end()){games.push_back(confirmed);return;}
 found->is_in_library=true;
 for(const auto& variant:confirmed.variants)if(Owned(variant)){
  auto v=std::find_if(found->variants.begin(),found->variants.end(),[&](const auto& old){return old.id==variant.id;});
  if(v==found->variants.end())found->variants.push_back(variant);else if(!Owned(*v))v->library_status=variant.library_status;
 }
}
inline void Apply(std::vector<PublicGame>& games,const GameInfo& confirmed){
 for(auto& game:games)if(game.id==confirmed.id){
  game.is_in_library=true;
  for(auto& v:game.variants)for(const auto& known:confirmed.variants)if(v.id==known.id && Owned(known) && !Owned(v))v.library_status=known.library_status;
 }
}
class RemovalConfirmation {
 std::string owner_,variant_;bool consumed_=false;
public:
 RemovalConfirmation(std::string owner,std::string variant):owner_(std::move(owner)),variant_(std::move(variant)){}
 void Cancel(){consumed_=true;}
 bool Approve(const std::string& owner,const std::string& variant){
  if(consumed_)return false;
  consumed_=true;
  return owner==owner_ && variant==variant_;
 }
};
struct RemovedVariant {std::string app_id,variant_id;};
template<class Game>inline void Unmark(Game& game,const RemovedVariant& removed){
 if(game.id!=removed.app_id)return;
 bool matched=false;
 for(auto& variant:game.variants)if(variant.id==removed.variant_id){matched=true;variant.library_status="NOT_OWNED";variant.library_selected=false;}
 if(!matched)return; // incomplete metadata cannot prove that no other store is owned
 game.is_in_library=std::any_of(game.variants.begin(),game.variants.end(),Owned);
}
inline void Remove(std::vector<GameInfo>& games,const RemovedVariant& removed){
 for(auto& game:games)if(game.id==removed.app_id){
  Unmark(game,removed);
  if(game.selected_variant_index>=game.variants.size() || !Owned(game.variants[game.selected_variant_index]))
   for(size_t i=0;i<game.variants.size();++i)if(Owned(game.variants[i])){game.selected_variant_index=i;game.launch_app_id=game.variants[i].id;break;}
 }
 std::erase_if(games,[&](const auto& game){return game.id==removed.app_id && !game.is_in_library;});
}
inline void Remove(std::vector<PublicGame>& games,const RemovedVariant& removed){for(auto& game:games)Unmark(game,removed);}
inline bool RemovalObserved(const std::vector<GameInfo>& games,const RemovedVariant& removed,bool complete){
 const auto app=std::find_if(games.begin(),games.end(),[&](const auto& g){return g.id==removed.app_id;});
 if(app==games.end())return complete;
 const auto variant=std::find_if(app->variants.begin(),app->variants.end(),[&](const auto& v){return v.id==removed.variant_id;});
 return variant!=app->variants.end() && variant->library_status=="NOT_OWNED";
}

}
