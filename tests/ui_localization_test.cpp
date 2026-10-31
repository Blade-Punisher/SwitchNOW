#include "ui_localization.hpp"
#include "localization.hpp"
#include <borealis.hpp>
#include <cassert>
#include <iostream>
int main() {
    brls::Box parent;brls::View child;child.parent=&parent;
    auto back=std::make_shared<brls::Action>();back->text="System Back";child.actions.push_back(back);
    auto save=std::make_shared<brls::Action>();save->text="Save";parent.actions.push_back(save);
    brls::Application::focus=&child;
    opennow::SetInterfaceLanguage("ru");opennow::LocalizeActionHints();
    assert(back->text==opennow::Tr("Back")&&save->text=="Сохранить");
    const auto reads=brls::View::reads;opennow::LocalizeActionHints();assert(brls::View::reads==reads); // idle frames do no tree scan
    opennow::SetInterfaceLanguage("uk");opennow::LocalizeActionHints();
    assert(back->text==opennow::Tr("Back")&&save->text==opennow::Tr("Save"));
    opennow::SetInterfaceLanguage("en");opennow::LocalizeActionHints();assert(back->text=="Back"&&save->text=="Save");
    // A view can replace an action hint; revisiting must retain the new meaning.
    save->text="Cancel";brls::Application::focus=&parent;opennow::LocalizeActionHints();assert(save->text=="Cancel");
    opennow::SetInterfaceLanguage("ru");opennow::LocalizeActionHints();assert(save->text==opennow::Tr("Cancel"));
    child.actions.clear();back.reset();brls::Application::focus=nullptr;opennow::LocalizeActionHints();
    opennow::SetInterfaceLanguage("en");
    std::cout<<"Production footer hints: app locale overrides system hints, language switching, ancestor actions, changed hints and expired action cleanup passed.\n";
}
