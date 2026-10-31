#pragma once
#include <string>
#include <vector>
namespace opennow {
struct InterfaceLanguageOption { std::string code; std::string label; };
const std::vector<InterfaceLanguageOption>& InterfaceLanguageOptions();
std::string InterfaceLanguageLabel(const std::string& code);
bool IsSupportedInterfaceLanguage(const std::string& code);
void SetInterfaceLanguage(const std::string& code);
const std::string& GetInterfaceLanguage();
// English keys are stable. Protocol/store/language codes are localized only
// where displayed; arbitrary server responses retain their diagnostic content.
std::string Tr(const std::string& english);
std::string Tr(const char* english);
std::string TrFormat(const std::string& english,const std::vector<std::string>& values);
std::string TrMessage(const std::string& message);
bool HasTranslation(const std::string& key,const std::string& language);
std::vector<std::string> TranslationKeys();
std::string TranslationSource(const std::string& displayed,const std::string& language);
}
