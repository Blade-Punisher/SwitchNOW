#pragma once
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
namespace opennow {
// Resolves transport documents before the existing FFmpeg player is opened.
class AdvertisementSource {
public:
    using Download = std::function<std::string(const std::string&, const std::string&)>;
    using Report = std::function<void(const std::string&)>;
    AdvertisementSource(std::string prefix, Download download,
                        std::function<bool()> cancelled, Report report);
    ~AdvertisementSource();
    AdvertisementSource(const AdvertisementSource&) = delete;
    AdvertisementSource& operator=(const AdvertisementSource&) = delete;
    std::string Prepare(const std::string& url);
private:
    std::string Fetch(const std::string& url, int depth);
    std::string prefix_;
    Download download_;
    std::function<bool()> cancelled_;
    Report report_;
    std::vector<std::string> files_, active_urls_;
    std::unordered_map<std::string, std::string> ready_;
    unsigned long long total_bytes_ = 0;
};
} // namespace opennow
