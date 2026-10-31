#pragma once
#include "game_feed_page_policy.hpp"
#include "localization.hpp"
#include <borealis.hpp>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
namespace opennow {
class GameFeedPager : public brls::Box {
    std::array<brls::Button*,11> numbers_{};
    std::vector<std::size_t> values_;
    brls::Label* hint_;
    game_feed::Page page_{};
    std::function<void(std::size_t)> choose_;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
    void PickNumber() {
        const auto alive=alive_; const auto count=page_.count;
        const auto choose=choose_;
        brls::Application::getImeManager()->openForNumber([alive,count,choose](long number) {
            if (!alive->load()) return;
            if (const auto page=game_feed::ParsePage(number,count)) choose(*page);
            else brls::Application::notify(TrFormat("Enter a page from 1 to {0}.", {std::to_string(count)}));
        }, Tr("Go to page"), TrFormat("Pages: 1-{0}", {std::to_string(count)}), 6, std::to_string(page_.index+1));
    }
public:
    explicit GameFeedPager(std::function<void(std::size_t)> choose)
        : brls::Box(brls::Axis::COLUMN), choose_(std::move(choose)) {
        setMarginTop(12); setMarginBottom(24); setWidthPercentage(100);
        // Reserve the scrollbar lane and focus-ring clearance. The row's
        // percentage width resolves against this inset content box.
        setPadding(0,24,0,0);
        auto* row=new brls::Box(brls::Axis::ROW);
        row->setWidthPercentage(100);
        for (std::size_t i=0;i<numbers_.size();++i) {
            auto* button=new brls::Button(); numbers_[i]=button;
            button->setWidth(72);button->setGrow(1);button->setShrink(0);button->setHeight(52);
            button->setPadding(10,8,10,8);button->setFontSize(20);
            if (auto* label=dynamic_cast<brls::Label*>(button->getView("brls/button/label"))) label->setSingleLine(true);
            button->setMarginRight(8);button->setCornerRadius(9);
            button->registerClickAction([this,i](brls::View*) {
                if (i>=values_.size()) return true;
                if (values_[i]==0) PickNumber(); else choose_(values_[i]-1);
                return true;
            });row->addView(button);
        }
        addView(row);
        hint_=new brls::Label();hint_->setFontSize(14);hint_->setMarginTop(8);addView(hint_);
    }
    ~GameFeedPager() override {alive_->store(false);}
    void Update(const game_feed::Page& page) {
        page_=page;values_=game_feed::PageNumbers(page);
        for(std::size_t i=0;i<numbers_.size();++i) {
            const bool visible=i<values_.size();
            numbers_[i]->setVisibility(visible?brls::Visibility::VISIBLE:brls::Visibility::GONE);
            if (!visible) continue;
            numbers_[i]->setMarginRight(i+1<values_.size()?8:0);
            numbers_[i]->setText(values_[i]==0?"...":std::to_string(values_[i]));
            numbers_[i]->setStyle(values_[i]==page.index+1?&brls::BUTTONSTYLE_PRIMARY:&brls::BUTTONSTYLE_BORDERED);
        }
        std::string hint = TrFormat("Page {0} of {1}. A: open selected page.",
            {std::to_string(page.index+1),std::to_string(page.count)});
        if(std::find(values_.begin(),values_.end(),0)!=values_.end()) hint += " " + Tr("...: enter a page number.");
        hint_->setText(hint);
    }
    std::vector<brls::View*> Buttons() const {
        std::vector<brls::View*> result;
        for(std::size_t i=0;i<values_.size();++i) result.push_back(numbers_[i]);
        return result;
    }
    brls::View* NextTarget() const {
        const auto target=page_.HasNext()?page_.index+2:page_.index+1;
        for(std::size_t i=0;i<values_.size();++i) if(values_[i]==target)return numbers_[i];
        return values_.empty()?nullptr:numbers_[0];
    }
    bool Owns(brls::View* view) const {
        for(auto* number:numbers_)if(number==view)return true;
        return false;
    }
};
inline std::string EmptyFeedMessage(bool loading, bool has_games, bool complete) {
    switch(game_feed::DescribeEmpty(loading,has_games,complete)) {
    case game_feed::EmptyState::Loading: return Tr("Loading games. The list will appear automatically...");
    case game_feed::EmptyState::NoMatches: return Tr("No games match the current search or filter.");
    case game_feed::EmptyState::Incomplete: return Tr("Game list is not ready. Press X to retry.");
    case game_feed::EmptyState::Empty: return Tr("No games in this list yet.");
    }
    return {};
}
}
