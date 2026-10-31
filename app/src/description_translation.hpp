#pragma once
#include <functional>
#include <string>
#include <vector>
#include <cstdint>
namespace opennow::description {
struct Reply { long status = 0; std::string body; };
using Transport = std::function<Reply(const std::string&, const std::string&, const std::string&)>;
struct Result { std::string text; std::string kind = "original"; std::string reason; };
struct Segment { std::string text, separator; };
std::string Locale(const std::string& language);
std::string PlainText(const std::string& text);
std::vector<Segment> Split(const std::string& text);
std::string MetadataBody(const std::string& id, const std::string& language);
// Public catalog text only. No tokens, account identity, proxy credentials or keys.
Result Resolve(const std::string& directory, const std::string& id,
    const std::string& language, const std::string& original,
    const Transport& transport, const std::function<bool()>& cancelled,
    std::int64_t now_seconds);
}
