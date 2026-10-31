#include "providers_tab.hpp"

#include "app_state.hpp"
#include "localization.hpp"
#include "provider_picker_policy.hpp"
#include "qr_login_dialog.hpp"

#include <exception>
#include <utility>

namespace opennow
{
namespace
{

brls::Label* MakeLabel(
    const std::string& text, float size, NVGcolor color, float bottom_margin = 0.0f)
{
    auto* label = new brls::Label();
    label->setText(Tr(text));
    label->setFontSize(size);
    label->setTextColor(color);
    label->setMarginBottom(bottom_margin);
    return label;
}

brls::Box* MakeInfoBlock(
    const std::string& eyebrow, const std::string& title, const std::string& detail,
    bool highlighted)
{
    auto* block = new brls::Box(brls::Axis::COLUMN);
    block->setPadding(14, 16, 14, 16);
    block->setMarginBottom(10);
    block->setCornerRadius(12);
    block->setBorderThickness(1);
    block->setBorderColor(highlighted ? nvgRGB(45, 111, 72) : nvgRGB(42, 46, 54));
    block->setBackgroundColor(highlighted ? nvgRGB(18, 44, 32) : nvgRGB(22, 24, 29));

    auto* small = MakeLabel(eyebrow, 12, highlighted ? nvgRGB(93, 231, 143)
                                                     : nvgRGB(125, 133, 145), 4);
    block->addView(small);
    block->addView(MakeLabel(title, 18, nvgRGB(241, 244, 247), 5));
    auto* description = MakeLabel(detail, 13, nvgRGB(145, 153, 164));
    description->setSingleLine(false);
    block->addView(description);
    return block;
}

class ProviderCard final : public brls::Box
{
  public:
    ProviderCard(
        const LoginProvider& provider, bool current,
        std::function<void(brls::View*)> on_selected)
        : brls::Box(brls::Axis::ROW)
        , on_selected_(std::move(on_selected))
        , nvidia_(provider_picker::IsNvidia(provider))
        , current_(current)
    {
        setHeight(96);
        setPadding(13, 16, 13, 16);
        setMarginBottom(10);
        setCornerRadius(13);
        setBorderThickness(2);
        setFocusable(true);
        setHideHighlight(true);
        setShadowType(brls::ShadowType::GENERIC);
        setAlignItems(brls::AlignItems::CENTER);

        accent_ = new brls::Box(brls::Axis::COLUMN);
        accent_->setWidth(5);
        accent_->setHeight(66);
        accent_->setCornerRadius(3);
        accent_->setMarginRight(15);
        addView(accent_);

        auto* copy = new brls::Box(brls::Axis::COLUMN);
        copy->setGrow(1.0f);

        auto* title_row = new brls::Box(brls::Axis::ROW);
        title_row->setAlignItems(brls::AlignItems::CENTER);
        title_row->setMarginBottom(4);
        auto* name = MakeLabel(provider_picker::DisplayName(provider), 21,
                               nvgRGB(244, 247, 249));
        name->setGrow(1.0f);
        title_row->addView(name);

        auto* badge = new brls::Box(brls::Axis::ROW);
        badge->setHeight(27);
        badge->setPadding(5, 11, 5, 11);
        badge->setCornerRadius(13);
        badge->setBackgroundColor(current ? nvgRGB(27, 76, 51)
                                          : (nvidia_ ? nvgRGB(31, 61, 43)
                                                     : nvgRGB(39, 42, 49)));
        badge->setAlignItems(brls::AlignItems::CENTER);
        badge->setJustifyContent(brls::JustifyContent::CENTER);
        auto* badge_label = MakeLabel(
            current ? "CURRENT" : provider_picker::TypeLabel(provider), 11,
            current || nvidia_ ? nvgRGB(103, 236, 148) : nvgRGB(183, 190, 200));
        badge->addView(badge_label);
        title_row->addView(badge);
        copy->addView(title_row);

        auto* detail = MakeLabel(provider_picker::Description(provider, current), 13,
                                 nvgRGB(143, 151, 162), 5);
        detail->setSingleLine(true);
        copy->addView(detail);

        auto* metadata = new brls::Box(brls::Axis::ROW);
        auto* method = MakeLabel(provider_picker::SignInLabel(provider), 12,
                                 nvidia_ ? nvgRGB(104, 222, 145)
                                         : nvgRGB(161, 168, 179));
        method->setGrow(1.0f);
        metadata->addView(method);
        metadata->addView(MakeLabel(
            provider_picker::ProviderCode(provider), 11, nvgRGB(104, 111, 122)));
        copy->addView(metadata);
        addView(copy);

        auto* choose = MakeLabel(opennow::Tr("A  Choose"), 14, nvgRGB(113, 225, 151));
        choose->setMarginLeft(18);
        addView(choose);

        registerClickAction([this](brls::View* view) {
            if (on_selected_)
                on_selected_(view);
            return true;
        });
        UpdateChrome(false);
    }

    void onFocusGained() override
    {
        brls::Box::onFocusGained();
        UpdateChrome(true);
    }

    void onFocusLost() override
    {
        brls::Box::onFocusLost();
        UpdateChrome(false);
    }

  private:
    void UpdateChrome(bool focused)
    {
        setBackgroundColor(focused ? nvgRGB(29, 34, 39) : nvgRGB(19, 21, 26));
        setBorderColor(focused ? nvgRGB(83, 230, 134) : nvgRGB(39, 43, 50));
        accent_->setBackgroundColor(
            focused ? nvgRGB(91, 235, 140)
                    : (current_ ? nvgRGB(58, 174, 105)
                                : (nvidia_ ? nvgRGB(67, 142, 92)
                                           : nvgRGB(75, 81, 91))));
    }

    std::function<void(brls::View*)> on_selected_;
    brls::Box* accent_ = nullptr;
    bool nvidia_ = false;
    bool current_ = false;
};

} // namespace

ProvidersTab::ProvidersTab(std::function<void()> on_success)
    : brls::Box(brls::Axis::COLUMN)
    , on_success_(std::move(on_success))
{
    setPadding(20, 30, 20, 30);
    setBackgroundColor(nvgRGB(11, 12, 15));

    auto* top = new brls::Box(brls::Axis::ROW);
    top->setHeight(80);
    top->setAlignItems(brls::AlignItems::CENTER);
    top->setMarginBottom(14);

    auto* accent = new brls::Box(brls::Axis::COLUMN);
    accent->setWidth(5);
    accent->setHeight(44);
    accent->setCornerRadius(3);
    accent->setBackgroundColor(nvgRGB(84, 232, 137));
    accent->setMarginRight(14);
    top->addView(accent);

    auto* heading = new brls::Box(brls::Axis::COLUMN);
    heading->setGrow(1.0f);
    heading->addView(MakeLabel(opennow::Tr("Choose your GeForce NOW operator"), 29,
                               nvgRGB(245, 247, 249), 4));
    heading->addView(MakeLabel(
        opennow::Tr("Select the service that owns your account and subscription."), 14,
        nvgRGB(143, 151, 162)));
    top->addView(heading);

    refresh_button_ = new brls::Button();
    refresh_button_->setWidth(184);
    refresh_button_->setHeight(46);
    refresh_button_->setCornerRadius(9);
    refresh_button_->setText(Tr("Refresh list"));
    refresh_button_->setMarginRight(10);
    refresh_button_->registerClickAction([this](brls::View*) {
        ReloadProviders();
        return true;
    });
    top->addView(refresh_button_);

    auto* close_button = new brls::Button();
    close_button->setStyle(&brls::BUTTONSTYLE_BORDERED);
    close_button->setWidth(174);
    close_button->setHeight(46);
    close_button->setCornerRadius(9);
    close_button->setText(Tr("Back to library"));
    close_button->registerClickAction([this](brls::View*) { return ClosePicker(); });
    top->addView(close_button);
    addView(top);

    auto* body = new brls::Box(brls::Axis::ROW);
    body->setGrow(1.0f);

    auto* guide = new brls::Box(brls::Axis::COLUMN);
    guide->setWidth(326);
    guide->setPadding(18, 18, 18, 18);
    guide->setMarginRight(18);
    guide->setCornerRadius(15);
    guide->setBackgroundColor(nvgRGB(16, 18, 22));
    guide->setBorderThickness(1);
    guide->setBorderColor(nvgRGB(37, 41, 48));
    guide->addView(MakeLabel("SIGN-IN GUIDE", 12, nvgRGB(90, 224, 137), 7));
    auto* guide_title = MakeLabel(
        opennow::Tr("Use the operator linked to your subscription"), 22,
        nvgRGB(241, 244, 247), 9);
    guide_title->setSingleLine(false);
    guide->addView(guide_title);
    auto* guide_detail = MakeLabel(
        opennow::Tr("Your games, membership and server access are provided by the selected operator."),
        14, nvgRGB(139, 147, 158), 17);
    guide_detail->setSingleLine(false);
    guide->addView(guide_detail);
    guide->addView(MakeInfoBlock(
        "OFFICIAL SERVICE", "NVIDIA",
        "Fast sign-in directly on the console, with phone or PC fallback.", true));
    guide->addView(MakeInfoBlock(
        "PARTNER SERVICE", "GeForce NOW Alliance",
        "Uses the partner's secure web authorization and regional service.", false));
    auto* back_hint = MakeLabel(
        opennow::Tr("B  Back at any time"), 14, nvgRGB(170, 177, 187));
    back_hint->setMarginTop(5);
    guide->addView(back_hint);
    body->addView(guide);

    auto* list_shell = new brls::Box(brls::Axis::COLUMN);
    list_shell->setGrow(1.0f);
    list_shell->setPadding(17, 18, 12, 18);
    list_shell->setCornerRadius(15);
    list_shell->setBackgroundColor(nvgRGB(15, 17, 21));
    list_shell->setBorderThickness(1);
    list_shell->setBorderColor(nvgRGB(37, 41, 48));

    auto* list_heading = new brls::Box(brls::Axis::ROW);
    list_heading->setHeight(44);
    list_heading->setAlignItems(brls::AlignItems::CENTER);
    auto* available = MakeLabel("AVAILABLE OPERATORS", 13,
                                nvgRGB(128, 136, 148));
    available->setGrow(1.0f);
    list_heading->addView(available);
    list_heading->addView(MakeLabel(opennow::Tr("A  Select   |   X  Refresh"), 13,
                                    nvgRGB(111, 119, 130)));
    list_shell->addView(list_heading);

    auto* status = new brls::Box(brls::Axis::ROW);
    status->setHeight(52);
    status->setPadding(8, 13, 8, 13);
    status->setMarginBottom(10);
    status->setCornerRadius(10);
    status->setBackgroundColor(nvgRGB(22, 25, 30));
    status->setAlignItems(brls::AlignItems::CENTER);
    auto* status_accent = new brls::Box(brls::Axis::COLUMN);
    status_accent->setWidth(4);
    status_accent->setHeight(30);
    status_accent->setCornerRadius(2);
    status_accent->setBackgroundColor(nvgRGB(83, 224, 132));
    status_accent->setMarginRight(11);
    status->addView(status_accent);
    auto* status_copy = new brls::Box(brls::Axis::COLUMN);
    status_copy->setGrow(1.0f);
    status_label_ = MakeLabel(opennow::Tr("Preparing operator list"), 15,
                              nvgRGB(225, 230, 235), 2);
    status_copy->addView(status_label_);
    status_detail_label_ = MakeLabel(
        opennow::Tr("The list will load in the background."), 12, nvgRGB(128, 136, 147));
    status_copy->addView(status_detail_label_);
    status->addView(status_copy);
    list_shell->addView(status);

    scrolling_frame_ = new brls::ScrollingFrame();
    scrolling_frame_->setGrow(1.0f);
    scrolling_frame_->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    list_container_ = new brls::Box(brls::Axis::COLUMN);
    list_container_->setPadding(0, 0, 26, 0);
    scrolling_frame_->setContentView(list_container_);
    list_shell->addView(scrolling_frame_);
    body->addView(list_shell);
    addView(body);

    registerAction(Tr("Back"), brls::BUTTON_B,
                   [this](brls::View*) { return ClosePicker(); }, false, false);
    registerAction(Tr("Refresh"), brls::BUTTON_X, [this](brls::View*) {
        ReloadProviders();
        return true;
    }, false, false);

    ShowListMessage("Loading available operators",
                    "You can leave this screen while the list is loading.");
}

ProvidersTab::~ProvidersTab()
{
    alive_->store(false, std::memory_order_release);
}

void ProvidersTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);

    const auto& state = AppState::Instance();
    if (providers_.empty() && state.HasProviders())
    {
        providers_ = state.providers();
        RebuildList();
        return;
    }

    if (providers_.empty() && !loading_)
        ReloadProviders();
}

void ProvidersTab::ReloadProviders()
{
    if (loading_ || closing_)
        return;

    loading_ = true;
    refresh_button_->setState(brls::ButtonState::DISABLED);
    SetStatus(opennow::Tr("Updating operator list"), opennow::Tr("Contacting GeForce NOW in the background."));
    if (providers_.empty())
        ShowListMessage("Loading available operators",
                        "Press B if you want to return without signing in.");

    const auto alive = alive_;
    brls::async([this, alive]() {
        std::vector<LoginProvider> loaded;
        std::string error;
        try
        {
            GfnClient client;
            loaded = client.FetchLoginProviders();
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
        }

        brls::sync([this, alive, loaded = std::move(loaded), error = std::move(error)]() mutable {
            if (!alive->load(std::memory_order_acquire))
                return;

            loading_ = false;
            refresh_button_->setState(brls::ButtonState::ENABLED);
            if (!error.empty())
            {
                SetStatus(opennow::Tr("Could not update operators"),
                          opennow::Tr("Check the connection and press X to try again."), true);
                if (providers_.empty())
                    ShowListMessage("Operator list unavailable",
                                    "Return to the library or retry without restarting SwitchNOW.");
                return;
            }

            providers_ = std::move(loaded);
            AppState::Instance().SetProviders(providers_);
            RebuildList();
            brls::Application::notify(Tr("Operator list updated"));
        });
    });
}

void ProvidersTab::RebuildList()
{
    brls::Application::giveFocus(refresh_button_);
    list_container_->clearViews();
    first_provider_card_ = nullptr;

    if (providers_.empty())
    {
        SetStatus(opennow::Tr("No operators found"), opennow::Tr("Press X to request the list again."), true);
        ShowListMessage("No operators available",
                        "The service returned an empty list. You can safely go back.");
        return;
    }

    SetStatus(TrFormat("{0} operators ready", {std::to_string(providers_.size())}),
              opennow::Tr("Choose the company used by your GeForce NOW account."));

    const LoginProvider* current = nullptr;
    if (AppState::Instance().HasSession())
        current = &AppState::Instance().session()->provider;

    for (size_t index = 0; index < providers_.size(); ++index)
    {
        const bool is_current = provider_picker::IsCurrent(providers_[index], current);
        auto* card = new ProviderCard(
            providers_[index], is_current,
            [this, index](brls::View* view) { OpenProviderDialog(view, index); });
        if (!first_provider_card_)
            first_provider_card_ = card;
        list_container_->addView(card);
    }

    if (first_provider_card_)
        brls::Application::giveFocus(first_provider_card_);
}

void ProvidersTab::ShowListMessage(
    const std::string& title, const std::string& detail)
{
    brls::Application::giveFocus(refresh_button_);
    list_container_->clearViews();
    first_provider_card_ = nullptr;

    auto* message = new brls::Box(brls::Axis::COLUMN);
    message->setPadding(28, 24, 28, 24);
    message->setCornerRadius(13);
    message->setBorderThickness(1);
    message->setBorderColor(nvgRGB(42, 46, 54));
    message->setBackgroundColor(nvgRGB(19, 21, 26));
    message->setAlignItems(brls::AlignItems::CENTER);
    auto* message_title = MakeLabel(title, 21, nvgRGB(235, 239, 243), 8);
    message_title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    message->addView(message_title);
    auto* message_detail = MakeLabel(detail, 14, nvgRGB(139, 147, 158));
    message_detail->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    message_detail->setSingleLine(false);
    message->addView(message_detail);
    list_container_->addView(message);
}

void ProvidersTab::SetStatus(
    const std::string& title, const std::string& detail, bool error)
{
    status_label_->setText(Tr(title));
    status_label_->setTextColor(error ? nvgRGB(255, 174, 178)
                                      : nvgRGB(225, 230, 235));
    status_detail_label_->setText(Tr(detail));
}

bool ProvidersTab::ClosePicker()
{
    if (closing_)
        return true;
    closing_ = true;
    alive_->store(false, std::memory_order_release);
    brls::Application::popActivity();
    return true;
}

bool ProvidersTab::OpenProviderDialog(brls::View* view, size_t index)
{
    (void)view;
    if (index >= providers_.size() || closing_)
        return false;

    const LoginProvider provider = providers_[index];
    auto* dialog = new QrLoginDialog(provider, client_, [this]() {
        // QrLoginDialog removes itself before this callback. Closing the picker
        // on the next UI turn restores the library focus stored by Borealis.
        brls::sync([this]() {
            if (on_success_)
                on_success_();
            ClosePicker();
        });
    });
    brls::Application::pushActivity(new brls::Activity(dialog));
    return true;
}

} // namespace opennow
