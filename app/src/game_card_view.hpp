#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>

namespace opennow
{
class ThumbnailImage;

struct GameCardDisplay
{
    std::string title;
    std::string subtitle;
    std::string badge;
    std::string image_url;
};

class GameCardView : public brls::Box
{
  public:
    using ClickHandler = std::function<void()>;

    GameCardView(GameCardDisplay display, ClickHandler click_handler);

    void onFocusGained() override;
    void onFocusLost() override;
    void frame(brls::FrameContext* ctx) override;

  private:
    void UpdateChrome(bool focused);
    void LoadImage();

    GameCardDisplay display_;
    ClickHandler click_handler_;
    ThumbnailImage* image_      = nullptr;
    bool thumbnail_active_     = false;
    brls::Label* title_label_   = nullptr;
    brls::Label* subtitle_label_ = nullptr;
};

} // namespace opennow
