#include "ui_localization.hpp"
#include "localization.hpp"
#include <borealis.hpp>
#include <borealis/core/i18n.hpp>
#include <unordered_map>
namespace opennow {
void LocalizeActionHints() {
    struct Hint {std::weak_ptr<brls::Action> action;std::string source,displayed;};
    static std::unordered_map<brls::Action*,Hint> hints;
    static brls::View* previous_focus=nullptr;
    static std::string previous_language;
    auto* focus=brls::Application::getCurrentFocus();
    const auto& language=GetInterfaceLanguage();
    if(focus==previous_focus&&language==previous_language)return;
    previous_focus=focus;previous_language=language;
    std::erase_if(hints,[](const auto& item){return item.second.action.expired();});
    for(auto* view=focus;view;view=view->getParent())for(const auto& action:view->getActions()) {
        const auto found=hints.find(action.get());
        if(found==hints.end()||action->getHintText()!=found->second.displayed) {
            auto source=TranslationSource(action->getHintText(),language);
            // Borealis follows the console language. App preference takes
            // precedence for the built-in Back/OK hints as well.
            if(action->getHintText()==brls::getStr("hints/back"))source="Back";
            if(action->getHintText()==brls::getStr("hints/ok"))source="OK";
            hints.insert_or_assign(action.get(),Hint{action,std::move(source),{}});
        }
        auto& hint=hints.at(action.get());
        hint.displayed=Tr(hint.source);action->setHintText(hint.displayed);
    }
}
}

