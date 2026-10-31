#include "game_card_view.hpp"

#include "cover_image_cache.hpp"
#include "card_viewport_policy.hpp"

#include <utility>

namespace opennow
{
namespace
{

std::string BuildSubtitle(const GameCardDisplay& display)
{
    if (display.badge.empty())
        return display.subtitle;

    if (display.subtitle.empty())
        return display.badge;

    return display.subtitle + "  |  " + display.badge;
}

brls::Label* MakeLabel(const std::string& text, float size, NVGcolor color)
{
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(size);
    label->setTextColor(color);
    label->setSingleLine(true);
    return label;
}

} // namespace

GameCardView::GameCardView(GameCardDisplay display, ClickHandler click_handler)
    : brls::Box(brls::Axis::COLUMN)
    , display_(std::move(display))
    , click_handler_(std::move(click_handler))
{
    setWidth(224);
    setHeight(178);
    setPadding(6, 6, 7, 6);
    setMarginRight(10);
    setMarginBottom(10);
    setCornerRadius(10);
    setBorderThickness(2);
    setShadowType(brls::ShadowType::GENERIC);
    setFocusable(true);
    setHideHighlight(true);

    image_ = new ThumbnailImage();
    image_->setWidth(212);
    image_->setHeight(112);
    image_->setMarginBottom(6);
    image_->setScalingType(brls::ImageScalingType::FILL);
    addView(image_);

    title_label_ = MakeLabel(display_.title, 16.0f, nvgRGB(245, 246, 248));
    title_label_->setMarginBottom(3);
    addView(title_label_);

    subtitle_label_ = MakeLabel(BuildSubtitle(display_), 12.0f, nvgRGB(105, 220, 148));
    addView(subtitle_label_);

    UpdateChrome(false);

    registerClickAction([this](brls::View* view) {
        (void)view;
        if (click_handler_)
            click_handler_();

        return true;
    });
}

void GameCardView::onFocusGained()
{
    brls::Box::onFocusGained();
    UpdateChrome(true);
}

void GameCardView::onFocusLost()
{
    brls::Box::onFocusLost();
    UpdateChrome(false);
}

void GameCardView::UpdateChrome(bool focused)
{
    setBackgroundColor(focused ? nvgRGB(29, 33, 38) : nvgRGB(17, 19, 23));
    setBorderColor(focused ? nvgRGB(92, 238, 139) : nvgRGB(39, 42, 48));
}

void GameCardView::LoadImage()
{
    image_->Load(display_.image_url);
}

void GameCardView::frame(brls::FrameContext* ctx)
{
    const auto card = getFrame();
    bool render = true, retain = true;
    for (auto* parent = getParent(); parent; parent = parent->getParent()) {
        if (!dynamic_cast<brls::ScrollingFrame*>(parent)) continue;
        float top, right, bottom, left;
        parent->getCullingBounds(&top, &right, &bottom, &left);
        const ui::CardRect rect {card.getMinX(), card.getMinY(), card.getWidth(), card.getHeight()};
        const ui::CardRect viewport {left, top, right - left, bottom - top};
        render = ui::CardInViewport(rect, viewport, 20); // keep edge shadows intact
        retain = ui::CardInViewport(rect, viewport, 2 * 190); // two warm rows
        break;
    }
    if (!retain && thumbnail_active_) {
        image_->Release(); thumbnail_active_ = false;
    } else if (retain && !thumbnail_active_) {
        thumbnail_active_ = true; LoadImage();
    }
    // Box's default leaf-only culling otherwise still paints every off-screen
    // card background, border and shadow into the current command buffer.
    if (render) brls::Box::frame(ctx);
}

} // namespace opennow
