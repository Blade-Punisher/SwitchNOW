#pragma once
#include <borealis.hpp>
#include <string>
#include <unordered_map>
namespace opennow {
// UI-thread-only registry; no view survives a grid rebuild in this map.
class GameFeedFocus {
    std::unordered_map<brls::View*,std::string> cards_;
public:
    void Clear(){cards_.clear();}
    void Remember(brls::View* card,const std::string& id){cards_[card]=id;}
    std::string Capture() const {
        const auto found=cards_.find(brls::Application::getCurrentFocus());
        return found==cards_.end()?std::string{}:found->second;
    }
    bool Restore(const std::string& id) const {
        if(id.empty())return false;
        for(const auto& [view,key]:cards_) if(key==id){brls::Application::giveFocus(view);return true;}
        return false;
    }
};
}
