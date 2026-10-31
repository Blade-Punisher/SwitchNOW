#include "localization.hpp"

#include <cassert>
#include <set>
#include <string>
#include <vector>

int main()
{
    const auto& languages = opennow::InterfaceLanguageOptions();
    assert(languages.size() == 8);

    std::set<std::string> codes;
    for (const auto& language : languages)
    {
        assert(!language.code.empty());
        assert(!language.label.empty());
        assert(codes.insert(language.code).second);

        opennow::SetInterfaceLanguage(language.code);
        assert(opennow::GetInterfaceLanguage() == language.code);
        assert(!opennow::Tr("Settings").empty());
        assert(!opennow::Tr("Store").empty());
        assert(!opennow::Tr("Library").empty());
        assert(!opennow::Tr("Language").empty());
    }

    opennow::SetInterfaceLanguage("unsupported");
    assert(opennow::GetInterfaceLanguage() == "en");
    assert(opennow::Tr("Untranslated technical message") ==
           "Untranslated technical message");

    opennow::SetInterfaceLanguage("ru");
    assert(opennow::Tr("Choose your GeForce NOW operator") ==
           "Выберите оператора GeForce NOW");
    assert(opennow::Tr("Back to library") == "Вернуться в библиотеку");
    assert(opennow::TrFormat("{0} operators ready", {"5"}).find("5") !=
           std::string::npos);

    const std::vector<std::string> translated_languages = {
        "zh-CN", "es", "ru", "it", "fr", "pl", "uk",
    };
    for (const auto& language : translated_languages)
    {
        opennow::SetInterfaceLanguage(language);
        assert(opennow::Tr("About this game") != "About this game");
        assert(
            opennow::Tr("Up / Down  Scroll  |  B  Back") !=
            "Up / Down  Scroll  |  B  Back");
        assert(opennow::Tr("Manage your GeForce NOW identity and persistent sign-in.") !=
               "Manage your GeForce NOW identity and persistent sign-in.");
        assert(opennow::Tr("NTE Auto-login: Ready") != "NTE Auto-login: Ready");
        for(const auto* label : {"Choose an auto-login profile","Game account","Epic Games account",
                "For sign-in without a manual code, disable 2FA in Epic account settings. Otherwise complete 2FA manually.",
                "Auto-login: Minus + R3, then X","Keyboard / auto-login"})assert(opennow::Tr(label)!=label);
        assert(opennow::Tr("Hold 0.3s; release allowed") != "Hold 0.3s; release allowed");
        assert(opennow::Tr("Commands ready. Release MINUS + R3 if desired; choose within 5s. B cancels.") !=
            "Commands ready. Release MINUS + R3 if desired; choose within 5s. B cancels.");
        assert(opennow::Tr("Hold 0.3s, then:") != "Hold 0.3s, then:");
        assert(opennow::Tr("Hold MINUS + R3 for 0.3s, then ZL Esc / L Tab / R Alt+Tab / ZR Win / Y keyboard") !=
            "Hold MINUS + R3 for 0.3s, then ZL Esc / L Tab / R Alt+Tab / ZR Win / Y keyboard");
        for (const auto* label : {"Auto-login: Ready", "Auto-login: Set credentials",
                "Auto-login: entering credentials", "Auto-login: sign-in submitted",
                "Auto-login requires a 16:9 stream", "Credentials are stored as plain text at:",
                "Open the email/password form, then press L + X. B cancels. Captcha and verification remain manual."})
            assert(opennow::Tr(label) != label);
        assert(opennow::Tr("library title") != "library title");
        assert(opennow::Tr("All") != "All");
        assert(opennow::Tr("Clear search") != "Clear search");
        assert(opennow::Tr("Search Library") != "Search Library");
        assert(opennow::Tr("Search Catalog") != "Search Catalog");
        assert(opennow::Tr("QUEUE") != "QUEUE");
        assert(opennow::Tr("SETUP") != "SETUP");
        assert(opennow::Tr("READY") != "READY");
        assert(opennow::Tr("Advertisement required") !=
               "Advertisement required");
        assert(opennow::Tr(
                   "Waiting for NVIDIA to provide the Free-tier advertisement details.") !=
               "Waiting for NVIDIA to provide the Free-tier advertisement details.");
        assert(opennow::Tr("Free-tier advertisement ready") !=
               "Free-tier advertisement ready");
        assert(opennow::Tr("Connecting to the streaming server") !=
               "Connecting to the streaming server");
        assert(opennow::Tr("Starting the video stream") !=
               "Starting the video stream");
        assert(opennow::TrFormat("Setting up rig... Position: {0}", {"12"}).find("12") !=
               std::string::npos);
        assert(opennow::Tr("Updates") != "Updates");
        assert(opennow::Tr("Check for updates") != "Check for updates");
        assert(opennow::Tr("SwitchNOW is up to date") != "SwitchNOW is up to date");
        assert(opennow::Tr("You already have the latest version ({0}).") !=
               "You already have the latest version ({0}).");
        assert(opennow::Tr("HOME shortcut created") != "HOME shortcut created");
        assert(opennow::TrFormat("Searching for: {0}", {"Fortnite"}).find("Fortnite") !=
               std::string::npos);
        assert(opennow::TrFormat("Position in queue:{0}", {"17"}).find("17") !=
               std::string::npos);
        assert(opennow::TrFormat("Loaded {0} supported games.", {"360"}).find("360") !=
               std::string::npos);
    }

    // Whole-catalogue coverage, rather than a few representative legacy labels.
    const auto keys=opennow::TranslationKeys();assert(keys.size()>900);
    std::set<std::string> unique_keys(keys.begin(),keys.end());assert(unique_keys.size()==keys.size());
    for(const auto& language:translated_languages) {
        opennow::SetInterfaceLanguage(language);
        for(const auto& key:keys){assert(opennow::HasTranslation(key,language));assert(!opennow::Tr(key).empty());}
        for(const auto* key:{"Create shortcut","Time remaining","Stream statistics","Previous Tab","Next Tab","Sign in inside SwitchNOW","Library Sync Failed"})assert(opennow::Tr(key)!=key);
        const auto formatted=opennow::TrFormat("Connected as {0}  |  {1} tier",{"test@example.invalid","FREE"});
        assert(formatted.find("test@example.invalid")!=std::string::npos&&formatted.find("FREE")!=std::string::npos);
    }
    opennow::SetInterfaceLanguage("ru");
    assert(opennow::Tr("Safe")=="Стабильный"&&opennow::Tr("Software")=="Программный");
    assert(opennow::TrMessage("1h 30m left").find("30")!=std::string::npos);
    assert(opennow::TrMessage("62.5 GB left")!=std::string("62.5 GB left"));
    assert(opennow::TrMessage("37.5 GB used of 100 GB").find("100")!=std::string::npos);
    assert(opennow::TrMessage("Session 0:09").find("09")!=std::string::npos);
    const std::string waiting="NVIDIA sent an approval email. Open it and approve this sign-in. Waiting: 15 seconds (timeout: 180 seconds). Check Spam if needed.";
    assert(opennow::TrMessage(waiting)!=waiting&&opennow::TrMessage(waiting).find("15")!=std::string::npos);
    assert(opennow::TrMessage("https://example.invalid/session/123")=="https://example.invalid/session/123");
    assert(opennow::TrFormat("Current: {0}",{"literal {1}"}).find("literal {1}")!=std::string::npos);
    // One-pass substitution must not interpret parameter-like user content.
    opennow::SetInterfaceLanguage("en");
    assert(opennow::TrFormat("{0} / {1}",{"name {1}","tail"})=="name {1} / tail");
    assert(!opennow::HasTranslation("unknown new UI label","ru"));
    return 0;
}
