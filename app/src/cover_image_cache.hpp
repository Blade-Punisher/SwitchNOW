#pragma once

#include "image_url_policy.hpp"

#include <borealis.hpp>

#include <functional>
#include <string>
#include <memory>
#include <vector>

namespace opennow
{

// A visible card owns only its current thumbnail. Release invalidates queued
// completions before freeing the GPU texture; encoded bytes remain in Loader.
class ThumbnailImage : public brls::Image {
public:
    ThumbnailImage();
    ~ThumbnailImage() override;
    void Load(const std::string& url);
    void Release();
private:
    struct State;
    std::shared_ptr<State> state_;
};

class GalleryImage : public brls::Image
{
public:
    GalleryImage();
    ~GalleryImage() override;
    void SetWindow(const std::vector<std::string>& image_urls);
    void Prefetch(const std::string& image_url);
    void Pause();
    void Load(const std::string& url, std::function<bool()> should_apply,
        std::function<void(bool)> complete);
private:
    struct State;
    std::shared_ptr<State> state_;
};

void SetCachedCoverImage(brls::Image* image, const std::string& image_url);
void SetCachedThumbnailImage(brls::Image* image, const std::string& image_url);
void SetCachedGalleryImage(
    GalleryImage* image,
    const std::string& image_url,
    std::function<bool()> should_apply = {},
    std::function<void(bool)> complete = {});
void PrefetchCachedGalleryImage(const std::string& image_url);
void SetCachedAvatarImage(brls::Image* image, const std::string& image_url);
std::string LoadCachedImageData(const std::string& image_url, bool refresh = false);
std::string CachedImageFilePath(const std::string& image_url);
void ShutdownImageCache();

} // namespace opennow
