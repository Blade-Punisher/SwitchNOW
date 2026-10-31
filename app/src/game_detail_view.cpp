#include "game_detail_view.hpp"
#include "game_detail_policy.hpp"

#include "app_state.hpp"
#include "game_feed_policy.hpp"
#include "owned_game_policy.hpp"
#include "http_cancellation.hpp"
#include "cover_image_cache.hpp"
#include "description_scroll_policy.hpp"
#include "gallery_prefetch_policy.hpp"
#include "description_translation.hpp"
#include "app_paths.hpp"
#include "network_resource_policy.hpp"
#include "home_shortcut.hpp"
#include "shortcut_worker.hpp"
#include "runtime_journal.hpp"
#include "nte_credentials.hpp"
#include "game_autologin.hpp"
#include "epic_autologin.hpp"
#include "ui_helpers.hpp"
#include "localization.hpp"

#ifdef __SWITCH__
#include <switch.h>
#include <borealis/platforms/switch/switch_video.hpp>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace opennow
{
namespace
{

class DescriptionScrollingFrame final : public brls::ScrollingFrame
{
  public:
    DescriptionScrollingFrame(
        brls::View* return_focus, brls::Label* focus_hint)
        : return_focus_(return_focus)
        , focus_hint_(focus_hint)
    {
        // Input is handled explicitly below. CENTERED prevents Borealis'
        // natural mode from changing focus while an Up/Down press is also
        // changing the description offset.
        setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
        setHideHighlightBackground(true);
        setHideHighlightBorder(true);
        setBorderThickness(0.0f);
        setBorderColor(nvgRGB(65, 229, 133));

        registerAction(
            "", brls::BUTTON_NAV_UP,
            [this](brls::View*) {
                return Scroll(DescriptionScrollDirection::Up);
            },
            true, true);
        registerAction(
            "", brls::BUTTON_NAV_DOWN,
            [this](brls::View*) {
                return Scroll(DescriptionScrollDirection::Down);
            },
            true, true);
        registerAction(
            "", brls::BUTTON_B,
            [this](brls::View*) {
                ReturnToButtons();
                return true;
            },
            true);
    }

    void onFocusGained() override
    {
        brls::ScrollingFrame::onFocusGained();
        setBorderThickness(3.0f);
        if (focus_hint_)
            focus_hint_->setVisibility(brls::Visibility::VISIBLE);
    }

    void onFocusLost() override
    {
        brls::ScrollingFrame::onFocusLost();
        setBorderThickness(0.0f);
        if (focus_hint_)
            focus_hint_->setVisibility(brls::Visibility::GONE);
    }

  private:
    bool Scroll(DescriptionScrollDirection direction)
    {
        const float maximum_offset =
            std::max(0.0f, getContentHeight() - getHeight());
        const DescriptionScrollResult result = AdvanceDescriptionScroll(
            getContentOffsetY(), maximum_offset, direction, 48.0f);
        setContentOffsetY(result.offset, false);
        if (result.return_to_buttons)
            ReturnToButtons();
        return true;
    }

    void ReturnToButtons()
    {
        if (return_focus_)
            brls::Application::giveFocus(return_focus_);
    }

    brls::View* return_focus_ = nullptr;
    brls::Label* focus_hint_ = nullptr;
};

brls::Label* MakeLabel(const std::string& text, float size, NVGcolor color, float bottom_margin = 10.0f)
{
    auto* label = new brls::Label();
    label->setText(Tr(text));
    label->setFontSize(size);
    label->setTextColor(color);
    label->setMarginBottom(bottom_margin);
    return label;
}

class ShortcutSetupView final : public brls::Box
{
  public:
    explicit ShortcutSetupView(shortcut::LaunchRequest request)
        : brls::Box(brls::Axis::COLUMN), request_(std::move(request))
    {
        setWidth(720);
        setPadding(28);
        setBackgroundColor(nvgRGB(10, 14, 19));
        addView(MakeLabel(opennow::Tr("Game shortcut"), 28, nvgRGB(80, 222, 142), 24));
        auto* row = new brls::Box(brls::Axis::ROW);
        auto* cover = new brls::Image();
        cover->setDimensions(160, 160);
        cover->setScalingType(brls::ImageScalingType::FILL);
        cover->setCornerRadius(16);
        cover->setMarginRight(24);
        SetCachedCoverImage(cover, request_.image_url);
        row->addView(cover);
        auto* details = new brls::Box(brls::Axis::COLUMN);
        details->setGrow(1);
        auto* title = MakeLabel(request_.title, 28, nvgRGB(242, 246, 250));
        title->setSingleLine(true);
        details->addView(title);
        details->addView(MakeLabel(Tr("Store") + ": " + request_.store,
            22, nvgRGB(180, 195, 205), 20));
        auto* hint = MakeLabel(opennow::Tr("The selected store is saved. Artwork is checked against GFN when you open the shortcut."),
            20, nvgRGB(180, 195, 205));
        hint->setSingleLine(false);
        details->addView(hint);
        row->addView(details);
        addView(row);
        status_ = MakeLabel(opennow::Tr("Create a shortcut for hbmenu, then optionally install it on HOME."),
            20, nvgRGB(180, 195, 205), 16);
        status_->setMarginTop(24);
        status_->setSingleLine(false);
        addView(status_);
        create_ = new brls::Button();
        create_->setText(Tr("Create shortcut"));
        create_->registerClickAction([this](brls::View*) { Create(); return true; });
        addView(create_);
        install_ = new brls::Button();
        install_->setMarginTop(12);
        install_->setText(Tr("Install on HOME"));
        install_->setVisibility(brls::Visibility::GONE);
        install_->registerClickAction([this](brls::View*) {
            std::string error;
            if (!shortcut::StartForwarderInstaller(result_.nro_path, result_.title, error))
                status_->setText(Tr(error));
            return true;
        });
        addView(install_);
        auto* close = new brls::Button();
        close->setText(Tr("Close"));
        close->setMarginTop(12);
        close->registerClickAction([this](brls::View*) {
            if (!busy_) {
                std::remove("sdmc:/switch/SwitchNOW/.shortcut/game_page.png");
                brls::Application::popActivity();
            }
            return true;
        });
        addView(close);
        setLastFocusedView(create_);
        registerAction(Tr("Back"), brls::BUTTON_B, [this](brls::View*) {
            if (!busy_) {
                std::remove("sdmc:/switch/SwitchNOW/.shortcut/game_page.png");
                brls::Application::popActivity();
            }
            return true;
        });
    }
    ~ShortcutSetupView() override { alive_->store(false); }
  private:
    void Create()
    {
        if (busy_) return;
        busy_ = true;
        status_->setText(Tr("Creating game shortcut..."));
        create_->setText(Tr("Creating game shortcut..."));
        const auto alive = alive_;
        if (!shortcut::RunTask([this, alive, request = request_] {
            shortcut::CreateResult result;
            try { result = shortcut::CreateGameShortcut(request); }
            catch (const std::exception& error) { result.error = error.what(); }
            catch (...) { result.error = "Shortcut creation failed"; }
            LogRuntimeEvent("shortcut", "create_complete",
                result.success ? "success" : result.error);
            brls::sync([this, alive, result = std::move(result)]() mutable {
                if (!alive->load()) return;
                busy_ = false;
                result_ = std::move(result);
                create_->setText(Tr("Create shortcut"));
                if (!result_.success)
                {
                    status_->setText(Tr("Shortcut creation failed") + ": " + Tr(result_.error));
                    return;
                }
                status_->setText(Tr(result_.used_game_cover
                    ? "Shortcut ready. You can return to hbmenu or install it on HOME."
                    : "Shortcut ready with the SwitchNOW icon. Game artwork will be retried on launch."));
                install_->setVisibility(brls::Visibility::VISIBLE);
                create_->setVisibility(brls::Visibility::GONE);
                brls::Application::giveFocus(install_);
            });
        }))
        {
            busy_ = false;
            create_->setText(Tr("Create shortcut"));
            status_->setText(Tr("Could not start shortcut worker. Please try again."));
        }
    }
    shortcut::LaunchRequest request_;
    shortcut::CreateResult result_;
    brls::Label* status_ = nullptr;
    brls::Button* create_ = nullptr;
    brls::Button* install_ = nullptr;
    bool busy_ = false;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
};

float MeasureWrappedLabelHeight(
    brls::Label* label, const std::string& text, float width)
{
    if (!label || text.empty() || width <= 0.0f)
        return 0.0f;

    NVGcontext* vg = brls::Application::getNVGContext();
    nvgSave(vg);
    nvgFontSize(vg, label->getFontSize());
    nvgFontFaceId(vg, label->getFont());
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, label->getLineHeight());
    float bounds[4] {};
    nvgTextBoxBounds(vg, 0.0f, 0.0f, width, text.c_str(), nullptr, bounds);
    nvgRestore(vg);
    return std::max(0.0f, bounds[3] - bounds[1]);
}

std::string PrimaryStore(const GameInfo& game)
{
    if (!game.available_stores.empty())
        return game_detail::DisplayStore(game.available_stores.front());

    if (!game.publisher.empty())
        return game.publisher;

    return "GeForce NOW";
}

std::string JoinStores(const GameInfo& game)
{
    if (game.available_stores.empty())
        return PrimaryStore(game);

    std::ostringstream stream;
    for (size_t i = 0; i < game.available_stores.size(); ++i)
    {
        if (i > 0)
            stream << ", ";
        stream << game.available_stores[i];
    }
    return stream.str();
}

std::string SafeText(const std::string& value, const std::string& fallback)
{
    return value.empty() ? fallback : value;
}

std::string VariantLabel(const GameVariant& variant)
{
    std::string label = SafeText(variant.store, Tr("Unknown store"));
    if (variant.library_selected)
        label += " (" + Tr("selected in library") + ")";
    else if (!variant.library_status.empty())
        label += " (" + variant.library_status + ")";
    return label;
}

std::string HumanizeMetadata(std::string value)
{
    bool capitalize = true;
    for (char& character : value)
    {
        if (character == '_' || character == '-')
        {
            character = ' ';
            capitalize = true;
            continue;
        }
        if (capitalize)
        {
            character = static_cast<char>(
                std::toupper(static_cast<unsigned char>(character)));
            capitalize = false;
        }
        else
        {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
    }
    return value;
}

std::string JoinMetadata(
    const std::vector<std::string>& values, size_t maximum = 4)
{
    std::ostringstream stream;
    const size_t count = std::min(maximum, values.size());
    for (size_t index = 0; index < count; ++index)
    {
        if (index > 0)
            stream << "  /  ";
        stream << Tr(HumanizeMetadata(values[index]));
    }
    return stream.str();
}

brls::Box* MakeTagRow(const std::vector<std::string>& values)
{
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setHeight(34);
    row->setMarginBottom(8);
    const size_t count = std::min<size_t>(4, values.size());
    for (size_t index = 0; index < count; ++index)
    {
        const std::string text = Tr(HumanizeMetadata(values[index]));
        auto* tag = new brls::Box(brls::Axis::ROW);
        tag->setHeight(28);
        tag->setWidth(std::min(142.0f, 24.0f + text.size() * 7.7f));
        tag->setMarginRight(index + 1 < count ? 7.0f : 0.0f);
        tag->setPadding(4, 9, 4, 9);
        tag->setCornerRadius(6);
        tag->setAlignItems(brls::AlignItems::CENTER);
        tag->setJustifyContent(brls::JustifyContent::CENTER);
        tag->setBackgroundColor(nvgRGB(31, 35, 41));
        tag->addView(MakeLabel(text, 13.0f, nvgRGB(195, 202, 211), 0.0f));
        row->addView(tag);
    }
    return row;
}

bool IsNumericLaunchId(const std::string& id)
{
    return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

std::string PromptCredential(
    const std::string& title, const std::string& initial, bool password)
{
#ifdef __SWITCH__
    SwkbdConfig keyboard {};
    if (R_FAILED(swkbdCreate(&keyboard, 0)))
        return {};
    if (password)
        swkbdConfigMakePresetPassword(&keyboard);
    else
        swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetType(&keyboard, SwkbdType_Latin);
    swkbdConfigSetStringLenMax(&keyboard, password ? 128 : 254);
    swkbdConfigSetHeaderText(&keyboard, title.c_str());
    const std::string save = Tr("Save");
    swkbdConfigSetOkButtonText(&keyboard, save.c_str());
    if (!initial.empty() && !password)
        swkbdConfigSetInitialText(&keyboard, initial.c_str());
    std::array<char, 512> output {};
    const Result result = swkbdShow(&keyboard, output.data(), output.size());
    swkbdClose(&keyboard);
    if (R_FAILED(result))
        return {};
    return output.data();
#else
    (void)title;
    (void)initial;
    (void)password;
    return {};
#endif
}

} // namespace

GameDetailView::GameDetailView(const GfnClient& client, GameDetailData data)
    : brls::Box(brls::Axis::COLUMN)
    , client_(client)
    , data_(std::move(data))
{
    const std::string saved_variant = client_.LoadLauncherPreference(ActiveUserId(), data_.game_id);
    for (size_t i = 0; i < data_.variants.size(); ++i)
    {
        if (!saved_variant.empty() && data_.variants[i].id == saved_variant)
        {
            selected_variant_index_ = i;
            launcher_preference_loaded_ = true;
            break;
        }
        if (!launcher_preference_loaded_ && data_.variants[i].id == data_.launch_app_id)
            selected_variant_index_ = i;
    }

    setPadding(16, 30, 18, 30);
    setBackgroundColor(nvgRGB(11, 12, 15));

    auto* title_row = new brls::Box(brls::Axis::ROW);
    title_row->setHeight(68);
    title_row->setAlignItems(brls::AlignItems::CENTER);
    title_row->setMarginBottom(6);
    auto* title = MakeLabel(data_.title, 39.0f, nvgRGB(248, 249, 251), 0.0f);
    title->setGrow(1.0f);
    title_row->addView(title);
    title_row->addView(MakeLabel(
        data_.subtitle, 15.0f, nvgRGB(137, 145, 156), 0.0f));
    addView(title_row);

    auto* content = new brls::Box(brls::Axis::ROW);
    content->setGrow(1.0f);
    content->setMarginTop(8);

    auto* info_column = new brls::Box(brls::Axis::COLUMN);
    info_column->setWidth(405);
    info_column->setMarginRight(24);
    info_column->setAlignItems(brls::AlignItems::STRETCH);

    auto* identity_text = new brls::Box(brls::Axis::COLUMN);
    identity_text->setMarginBottom(12);
    identity_text->addView(MakeLabel(
        Tr(data_.owned ? "In your library" : "Supported in GeForce NOW"),
        19.0f, nvgRGB(77, 218, 130), 8.0f));
    if (!data_.content_ratings.empty())
    {
        identity_text->addView(MakeLabel(
            JoinMetadata(data_.content_ratings, 2),
            14.0f, nvgRGB(219, 223, 229), 7.0f));
    }
    if (!data_.genres.empty())
        identity_text->addView(MakeTagRow(data_.genres));
    if (!data_.supported_controls.empty())
    {
        identity_text->addView(MakeLabel(
            Tr("Input") + ": " + JoinMetadata(data_.supported_controls, 3),
            13.5f, nvgRGB(151, 159, 170), 6.0f));
    }
    if (!data_.membership_tier_label.empty())
    {
        identity_text->addView(MakeLabel(
            Tr("Membership") + ": " + Tr(data_.membership_tier_label),
            13.5f, nvgRGB(246, 196, 80), 0.0f));
    }
    info_column->addView(identity_text);

    const bool has_autologin_actions = autologin::FindProfile(data_.title) != nullptr ||
        autologin::epic::Eligible(data_.title,data_.stores) || std::any_of(data_.variants.begin(),data_.variants.end(),[this](const auto& v){return autologin::epic::Eligible(data_.title,v.store);});
    auto* actions = new brls::Box(brls::Axis::COLUMN);
    actions->setMarginBottom(10);

    auto* play_button = play_button_ = new brls::Button();
    play_button->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    play_button->setHeight(50);
    play_button->setCornerRadius(9);
    play_button->setText(Tr(data_.owned ? "Play on GeForce NOW" : "Play from Store"));
    play_button->registerClickAction([this](brls::View* view) {
        (void)view;
        Play();
        return true;
    });
    actions->addView(play_button);

    auto* secondary_actions = new brls::Box(brls::Axis::ROW);
    secondary_actions->setHeight(44);
    secondary_actions->setMarginTop(8);

    store_button_ = new brls::Button();
    store_button_->setHeight(44);
    store_button_->setCornerRadius(8);
    store_button_->setGrow(1.0f);
    store_button_->registerClickAction([this](brls::View*) {
        ShowStoreSelector(false);
        return true;
    });
    UpdateStoreButton();
    secondary_actions->addView(store_button_);

    auto* shortcut_button = new brls::Button();
    shortcut_button->setStyle(&brls::BUTTONSTYLE_BORDERED);
    shortcut_button->setWidth(52);
    shortcut_button->setHeight(44);
    shortcut_button->setCornerRadius(8);
    shortcut_button->setMarginLeft(8);
    shortcut_button->setText("+");
    shortcut_button->registerClickAction([this](brls::View*) {
        CreateSwitchShortcut();
        return true;
    });
    secondary_actions->addView(shortcut_button);
    actions->addView(secondary_actions);
    auto* ownership_row = new brls::Box(brls::Axis::ROW);
    ownership_row->setHeight(44);ownership_row->setMarginTop(8);
    ownership_button_ = new brls::Button();ownership_button_->setGrow(1);
    ownership_button_->setHeight(44);ownership_button_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    ownership_button_->registerClickAction([this](brls::View*) {MarkAsOwned();return true;});
    ownership_row->addView(ownership_button_);
    remove_ownership_button_ = new brls::Button();
    remove_ownership_button_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    remove_ownership_button_->setWidth(52);remove_ownership_button_->setHeight(44);
    remove_ownership_button_->setMarginLeft(8);remove_ownership_button_->setCornerRadius(8);
    remove_ownership_button_->setPadding(4);remove_ownership_button_->setFontSize(24);
    remove_ownership_button_->setText("×");remove_ownership_button_->setTextColor(nvgRGB(232,135,135));
    remove_ownership_button_->registerClickAction([this](brls::View*) {ConfirmUnowned();return true;});
    ownership_row->addView(remove_ownership_button_);
    UpdateOwnershipButton();actions->addView(ownership_row);

    const std::string studio = !data_.developer.empty()
        ? data_.developer
        : data_.publisher;
    actions->addView(MakeLabel(
        Tr("By") + " " + SafeText(studio, Tr("Unknown studio")) +
            "  /  " + SafeText(data_.stores, Tr("Unknown store")),
        14.0f, nvgRGB(166, 174, 184), 7.0f));

    if (has_autologin_actions)
    {
        autologin_button_ = new brls::Button();
        autologin_button_->setHeight(44);
        autologin_button_->setMarginBottom(8);
        autologin_button_->registerClickAction([this](brls::View*) {
            OpenAutoLoginCredentialsMenu();
            return true;
        });
        UpdateAutoLoginButton();
        actions->addView(autologin_button_);
    }
    info_column->addView(actions);

    auto* description_header = new brls::Box(brls::Axis::ROW);
    description_header->setHeight(31);
    description_header->setAlignItems(brls::AlignItems::CENTER);
    auto* description_title = MakeLabel(
        opennow::Tr("About this game"), 16.0f, nvgRGB(246, 248, 250), 0.0f);
    description_title->setGrow(1.0f);
    description_title->setSingleLine(true);
    description_heading_ = description_title;
    description_header->addView(description_title);
    auto* description_focus_hint = MakeLabel(
        opennow::Tr("Up / Down  Scroll  |  B  Back"),
        12.0f,
        nvgRGB(105, 220, 148),
        0.0f);
    description_focus_hint->setVisibility(brls::Visibility::GONE);
    description_header->addView(description_focus_hint);
    info_column->addView(description_header);

    const std::string description_text = GetInterfaceLanguage() == "en"
        ? SafeText(description::PlainText(data_.description), Tr("No description is available yet for this title."))
        : Tr("Loading description...");
    auto* description = MakeLabel(
        description_text,
        15.5f,
        nvgRGB(232, 235, 240),
        0.0f);
    description_label_ = description;
    description->setSingleLine(false);
    description->setWidth(365);
    // Label defaults to max-height: 100%, which truncates wrapped text to the
    // viewport and makes ScrollingFrame think there is almost nothing to
    // scroll. Keep the full measured text height in the detached content.
    description->setMaxHeight(brls::View::AUTO);

    auto* description_content = new brls::Box(brls::Axis::COLUMN);
    description_content->setWidth(389);
    description_content->setPadding(10, 12, 10, 12);
    description_content->addView(description);

    brls::View* description_return_focus =
        autologin_button_ ? static_cast<brls::View*>(autologin_button_)
                    : static_cast<brls::View*>(store_button_);
    auto* description_frame = new DescriptionScrollingFrame(
        description_return_focus, description_focus_hint);
    const float measured_height =
        MeasureWrappedLabelHeight(description, description_text, 365.0f);
    description_frame_ = description_frame;
    description_max_height_ = has_autologin_actions ? 153.0f : 203.0f;
    const float maximum_height = description_max_height_;
    const float viewport_height = std::clamp(
        std::ceil(measured_height + 20.0f), 46.0f, maximum_height);
    description_frame->setHeight(viewport_height);
    description_frame->setShrink(1.0f);
    description_frame->setCornerRadius(9);
    description_frame->setBackgroundColor(nvgRGB(20, 23, 28));
    description_frame->setContentView(description_content);
    info_column->addView(description_frame);

    content->addView(info_column);

    auto* media_column = new brls::Box(brls::Axis::COLUMN);
    media_column->setGrow(1.0f);
    media_column->setAlignItems(brls::AlignItems::STRETCH);

    media_urls_ = data_.screenshots;
    if (media_urls_.empty() && !data_.hero_url.empty())
        media_urls_.push_back(data_.hero_url);
    if (media_urls_.empty() && !data_.image_url.empty())
        media_urls_.push_back(data_.image_url);

    screenshot_image_ = new GalleryImage();
    screenshot_image_->setHeight(420);
    screenshot_image_->setCornerRadius(13);
    screenshot_image_->setScalingType(brls::ImageScalingType::FILL);
    screenshot_image_->setMarginBottom(8);
    media_column->addView(screenshot_image_);
    screenshot_status_ = MakeLabel("", 13.0f, nvgRGB(150, 160, 174), 0.0f);
    screenshot_status_->setHeight(20);
    screenshot_status_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    media_column->addView(screenshot_status_);

    auto* dots = new brls::Box(brls::Axis::ROW);
    dots->setHeight(24);
    dots->setAlignItems(brls::AlignItems::CENTER);
    dots->setJustifyContent(brls::JustifyContent::CENTER);
    dots->setMarginBottom(6);
    for (size_t index = 0; index < media_urls_.size(); ++index)
    {
        auto* dot = MakeLabel("●", 14.0f, nvgRGB(94, 101, 110), 0.0f);
        dot->setWidth(20);
        dot->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        screenshot_dots_.push_back(dot);
        dots->addView(dot);
    }
    media_column->addView(dots);

    if (media_urls_.size() > 1)
    {
        media_column->addView(MakeLabel(
            "L / R  " + Tr("Browse screenshots"),
            13.0f, nvgRGB(132, 140, 151), 5.0f));
    }
    if (!data_.feature_labels.empty())
        media_column->addView(MakeTagRow(data_.feature_labels));
    media_column->addView(MakeLabel(
        Tr("Publisher") + ": " + SafeText(data_.publisher, Tr("Unknown")) +
            "  /  " + Tr("Last played") + ": " +
            SafeText(data_.last_played, Tr("Never")),
        13.5f, nvgRGB(137, 145, 156), 0.0f));

    UpdateScreenshot();
    content->addView(media_column);
    addView(content);

    if (media_urls_.size() > 1)
    {
        registerAction(Tr("Previous screenshot"), brls::BUTTON_LB, [this](brls::View*) {
            ShowPreviousScreenshot();
            return true;
        });
        registerAction(Tr("Next screenshot"), brls::BUTTON_RB, [this](brls::View*) {
            ShowNextScreenshot();
            return true;
        });
    }

    registerAction(Tr("Back"), brls::BUTTON_B, [](brls::View* view) {
        (void)view;
        brls::Application::popActivity();
        return true;
    });
    LoadDescription();
}

GameDetailView::~GameDetailView()
{
    alive_->store(false);
    description_cancelled_->store(true);
    screenshot_generation_->fetch_add(1);
}

void GameDetailView::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    if (gallery_paused_) { gallery_paused_ = false; UpdateScreenshot(); }
    if (description_cancelled_->load()) {
        // Old workers retain the cancelled token and can never update this view.
        description_cancelled_ = std::make_shared<std::atomic_bool>(false);
        LoadDescription();
    }
}

void GameDetailView::willDisappear(bool resetState)
{
    if (screenshot_image_) { screenshot_image_->Pause(); gallery_paused_ = true; }
    brls::Box::willDisappear(resetState);
}

void GameDetailView::LoadDescription()
{
    const auto language = GetInterfaceLanguage();
    if (language == "en") return;
    const auto alive = alive_;
    const auto cancelled = description_cancelled_;
    const auto id = data_.game_id.empty() ? data_.launch_app_id : data_.game_id;
    const auto original = data_.description;
    const auto directory = AppHomePath();
    brls::async([this, alive, cancelled, id, language, original, directory] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        const auto abort = [alive, cancelled, deadline] {
            if (network::launch_count.load() != 0) cancelled->store(true);
            return !alive->load() || cancelled->load() || std::chrono::steady_clock::now() >= deadline;
        };
        description::Result result{description::PlainText(original), "original", "unavailable"};
        try {
            const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            result = description::Resolve(directory, id, language, original,
                [abort](const std::string& method, const std::string& url, const std::string& body) {
                    std::vector<std::string> headers{"Accept: application/json"};
                    if (method == "POST") headers.push_back("Content-Type: application/json");
                    const auto response = HttpClient{}.Request(method, url, "SwitchNOW-description",
                        headers, body, 2, 6, 256*1024, {}, abort);
                    return description::Reply{response.status_code, response.body};
                }, abort, now);
        } catch (...) {}
        if (!alive->load() || cancelled->load() || network::launch_count.load() != 0) return;
        LogRuntimeEvent("description", "resolved", "language=" + language + " kind=" + result.kind + " reason=" + result.reason);
        brls::sync([this, alive, cancelled, language, result=std::move(result)] {
            if (!alive->load() || cancelled->load() || language != GetInterfaceLanguage()) return;
            const auto text = result.text.empty() ? Tr("No description is available yet for this title.") : result.text;
            description_label_->setText(text);
            const auto height = MeasureWrappedLabelHeight(description_label_, text, 365.0f);
            description_frame_->setHeight(std::clamp(std::ceil(height+20.0f), 46.0f, description_max_height_));
            description_frame_->setContentOffsetY(0, false);
            description_heading_->setText(Tr(result.kind == "machine" ? "Automatic translation" :
                result.reason == "quota" ? "Translation limit reached" : "About this game"));
        });
    }, true);
}

void GameDetailView::ShowShortcutResult(const shortcut::InstallerResult& result)
{
    brls::sync([this, alive = alive_, result] {
        if (!alive->load()) return;
        // The detail activity already exists. Closing the overlay only pops it;
        // no main UI construction or captured view access during its teardown.
        brls::Application::giveFocus(play_button_);
        const std::string message = result.success
            ? Tr("HOME shortcut created") + "\n\n" + data_.title
            : Tr("HOME shortcut installation failed") + "\n\n" + Tr(result.error);
        auto* dialog = new brls::Dialog(message);
        dialog->addButton(Tr("Close"), [] {});
        dialog->setCancelable(true);
        dialog->open();
    });
}

void GameDetailView::ShowPreviousScreenshot()
{
    if (media_urls_.empty())
        return;
    screenshot_index_ =
        (screenshot_index_ + media_urls_.size() - 1) % media_urls_.size();
    UpdateScreenshot();
}

void GameDetailView::ShowNextScreenshot()
{
    if (media_urls_.empty())
        return;
    screenshot_direction_ = 1;
    screenshot_index_ = (screenshot_index_ + 1) % media_urls_.size();
    UpdateScreenshot();
}

void GameDetailView::UpdateScreenshot()
{
    if (!screenshot_image_ || media_urls_.empty())
        return;
    screenshot_index_ = std::min(screenshot_index_, media_urls_.size() - 1);
    const auto indices = GalleryPrefetchWindow(media_urls_.size(), screenshot_index_, screenshot_direction_);
    std::vector<std::string> window;window.reserve(indices.count);
    for (size_t slot=0;slot<indices.count;++slot) window.push_back(media_urls_[indices.indices[slot]]);
    screenshot_image_->SetWindow(window);
    const std::uint64_t generation =
        screenshot_generation_->fetch_add(1) + 1;
    const auto generation_state = screenshot_generation_;
    screenshot_status_->setText(Tr("Loading screenshot..."));
    SetCachedGalleryImage(
        screenshot_image_,
        media_urls_[screenshot_index_],
        [generation_state, generation]() {
            return generation_state->load() == generation;
        }, [this, alive = alive_, generation_state, generation](bool loaded) {
            if (!alive->load() || generation_state->load() != generation) return;
            screenshot_status_->setText(loaded ? "" : Tr("Could not load screenshot"));
        });

    for (size_t slot = 1; slot < window.size(); ++slot)
        screenshot_image_->Prefetch(window[slot]);

    for (size_t index = 0; index < screenshot_dots_.size(); ++index)
    {
        screenshot_dots_[index]->setTextColor(
            index == screenshot_index_
                ? nvgRGB(105, 232, 151)
                : nvgRGB(91, 98, 108));
    }
}

void GameDetailView::Play()
{
    if(ownership_pending_)return;
    const auto& session = AppState::Instance().session();
    if (!session)
    {
        ShowError(opennow::Tr("Not Logged In"), opennow::Tr("Sign in from Library before starting a GeForce NOW session."));
        return;
    }

    if (data_.variants.size() > 1 && !launcher_preference_loaded_)
    {
        ShowStoreSelector(true);
        return;
    }

    LaunchSelectedVariant();
}

std::string GameDetailView::ActiveUserId() const
{
    const auto& session = AppState::Instance().session();
    return session ? session->user.user_id : "";
}

void GameDetailView::UpdateStoreButton()
{
    if (!store_button_)
        return;
    const std::string raw_store = selected_variant_index_ < data_.variants.size()
        ? SafeText(data_.variants[selected_variant_index_].store, "Unknown")
        : SafeText(data_.stores, "Unknown");
    const std::string store = game_detail::DisplayStore(raw_store);
    UpdateOwnershipButton();
    UpdateAutoLoginButton();
    store_button_->setText(Tr("Store") + ": " + store + (data_.variants.size() > 1 ? " (" + Tr("change") + ")" : ""));
}

void GameDetailView::UpdateOwnershipButton()
{
    if(!ownership_button_)return;
    const bool available=selected_variant_index_<data_.variants.size() && IsNumericLaunchId(data_.variants[selected_variant_index_].id);
    const bool owned=available && ownership::Owned(data_.variants[selected_variant_index_]);
    ownership_button_->setText(Tr(ownership_pending_?"Saving to GFN library...":owned?"In library":"Mark as owned"));
    ownership_button_->setState(ownership_pending_ || owned || !available?brls::ButtonState::DISABLED:brls::ButtonState::ENABLED);
    ownership_button_->setFontSize(owned?16:18);
    if(remove_ownership_button_){
        if(!owned && brls::Application::getCurrentFocus()==remove_ownership_button_)
            brls::Application::giveFocus(ownership_button_);
        remove_ownership_button_->setVisibility(owned?brls::Visibility::VISIBLE:brls::Visibility::GONE);
        remove_ownership_button_->setState(ownership_pending_?brls::ButtonState::DISABLED:brls::ButtonState::ENABLED);
    }
}

void GameDetailView::MarkAsOwned() { SetOwnership(true); }

void GameDetailView::ConfirmUnowned()
{
    if(ownership_pending_ || selected_variant_index_>=data_.variants.size())return;
    const auto variant=data_.variants[selected_variant_index_];
    const auto& session=AppState::Instance().session();
    if(!session || !ownership::Owned(variant))return;
    const auto owner=game_feed::OwnerKey(session->user.user_id,session->provider.code,session->provider.streaming_service_url);
    const auto alive=alive_;
    const auto confirmation=std::make_shared<ownership::RemovalConfirmation>(owner,variant.id);
    auto* dialog=new brls::Dialog(TrFormat("Mark {0} as not owned for {1}?\n\nOnly this store version will be changed.",
        {data_.title,game_detail::DisplayStore(variant.store)}));
    dialog->addButton(Tr("No"),[confirmation]{confirmation->Cancel();});
    dialog->addButton(Tr("Yes"),[this,alive,confirmation]{
        if(!alive->load())return;
        const auto& current=AppState::Instance().session();
        if(!current || selected_variant_index_>=data_.variants.size())return;
        if(!confirmation->Approve(game_feed::OwnerKey(current->user.user_id,current->provider.code,current->provider.streaming_service_url),data_.variants[selected_variant_index_].id))return;
        SetOwnership(false);
    });
    dialog->setCancelable(true);dialog->open();
}

void GameDetailView::SetOwnership(bool owned)
{
    if(ownership_pending_)return;
    auto& state=AppState::Instance();
    if(!state.HasSession()){ShowError(Tr("Mark as owned"),Tr("Sign in to GFN before adding games to your library."));return;}
    if(selected_variant_index_>=data_.variants.size())return;
    const auto variant=data_.variants[selected_variant_index_];
    if(ownership::Owned(variant)==owned)return;
    if(!IsNumericLaunchId(variant.id)){ShowError(Tr("Mark as owned"),Tr("Refresh the catalog to load this game's store versions."));return;}
    GameInfo confirmed;confirmed.id=data_.game_id;confirmed.uuid=data_.game_id;
    confirmed.title=data_.title;
    confirmed.publisher=data_.publisher;
    confirmed.developer=data_.developer;
    confirmed.description=data_.description;
    confirmed.image_url=data_.image_url;
    confirmed.poster_url=data_.poster_url;
    confirmed.hero_url=data_.hero_url;
    confirmed.screenshots=data_.screenshots;
    confirmed.genres=data_.genres;
    confirmed.feature_labels=data_.feature_labels;
    confirmed.content_ratings=data_.content_ratings;
    confirmed.supported_controls=data_.supported_controls;
    confirmed.membership_tier_label=data_.membership_tier_label;
    confirmed.last_played=data_.last_played;
    confirmed.launch_app_id=variant.id;
    confirmed.selected_variant_index=selected_variant_index_;

    confirmed.is_in_library=owned;confirmed.variants=data_.variants;
    for(auto& item:confirmed.variants){
        if(item.id==variant.id){item.library_status=owned?"MANUAL":"NOT_OWNED";if(!owned)item.library_selected=false;}
        if(std::find(confirmed.available_stores.begin(),confirmed.available_stores.end(),item.store)==confirmed.available_stores.end())confirmed.available_stores.push_back(item.store);
    }
    ownership_pending_=true;UpdateOwnershipButton();
    auto session=*state.session();const auto client=client_;const auto alive=alive_;
    const auto owner=game_feed::OwnerKey(session.user.user_id,session.provider.code,session.provider.streaming_service_url);
    auto same_account=[owner]{const auto& current=AppState::Instance();return current.HasSession() && owner==game_feed::OwnerKey(current.session()->user.user_id,current.session()->provider.code,current.session()->provider.streaming_service_url);};
    brls::async([this,client,alive,session=std::move(session),variant,confirmed=std::move(confirmed),same_account,owned]() mutable {
        try {
            // A request already sent must finish even if the card is closed:
            // its acknowledged result still updates the account's local view.
            if(owned)client.MarkGameOwned(session,confirmed.id,variant.id);
            else client.MarkGameUnowned(session,confirmed.id,variant.id);
            brls::sync([this,alive,session=std::move(session),confirmed=std::move(confirmed),variant,same_account,owned]() mutable {
                if(!same_account())return;
                auto& current=AppState::Instance();current.SetSession(std::move(session));
                if(owned)current.MarkGameOwned(std::move(confirmed),variant.id);else current.MarkGameUnowned(confirmed.id,variant.id);
                brls::Application::notify(Tr(owned?"Game added to your GFN library.":"Game marked as not owned for this store."));
                if(!alive->load())return;
                ownership_pending_=false;
                for(auto& item:data_.variants)if(item.id==variant.id){item.library_status=owned?"MANUAL":"NOT_OWNED";if(!owned)item.library_selected=false;}
                data_.owned=std::any_of(data_.variants.begin(),data_.variants.end(),ownership::Owned);
                UpdateOwnershipButton();play_button_->setText(Tr(data_.owned?"Play on GeForce NOW":"Play from Store"));
            });
        } catch(const std::exception& error) {
            const std::string message=error.what();
            brls::sync([this,alive,message,same_account]{
                if(!same_account())return;
                if(!alive->load()){brls::Application::notify(Tr("GFN did not confirm the library change. Refresh the library before retrying."));return;}
                ownership_pending_=false;UpdateOwnershipButton();
                const auto stack=brls::Application::getActivitiesStack();
                if(!stack.empty() && stack.back()==getParentActivity())ShowError(Tr("Could not update library"),message);
            });
        }
    },false);
}

void GameDetailView::UpdateAutoLoginButton()
{
    if (!autologin_button_)
        return;
    const auto* profile = autologin::FindProfile(data_.title);
    if(profile&&EpicAutoLoginAvailable()){autologin_button_->setText(Tr("Auto-login: game / Epic"));return;}
    if(!profile) {
        if(!EpicAutoLoginAvailable()){autologin_button_->setText(Tr("Epic auto-login: select Epic store"));return;}
        profile=&autologin::GetProfile(autologin::Game::Epic);
    }
    auto credentials = autologin::Load(*profile);
    const bool ready = autologin::Valid(*profile, credentials);
    credentials.Wipe();
    autologin_button_->setText((profile->game==autologin::Game::Epic?"Epic: ":"")+
        Tr(ready ? "Auto-login: Ready" : "Auto-login: Set credentials") +
        (ready ? " (X)" : ""));
}

bool GameDetailView::EpicAutoLoginAvailable() const {
    const auto store=selected_variant_index_<data_.variants.size()?data_.variants[selected_variant_index_].store:data_.stores;
    return autologin::epic::Eligible(data_.title,store);
}
void GameDetailView::ConfigureAutoLoginCredentials(const autologin::Profile* profile)
{
    if(!profile)profile=autologin::FindProfile(data_.title);
    if(profile&&profile->game==autologin::Game::Epic&&!EpicAutoLoginAvailable())return;
    if (!profile) return;
    auto credentials = autologin::Load(*profile);
    std::string email = PromptCredential(
        std::string(profile->folder) + ": " + Tr(profile->email_only ? "Email address" : "Email / username"), credentials.login, false);
    if (email.empty())
    {
        credentials.Wipe();
        brls::Application::notify(Tr("Credential setup cancelled"));
        return;
    }

    std::string password = PromptCredential(std::string(profile->folder) + ": " + Tr("Password"), {}, true);
    if (password.empty())
    {
        std::fill(email.begin(), email.end(), '\0');
        credentials.Wipe();
        brls::Application::notify(Tr("Credential setup cancelled"));
        return;
    }

    credentials.login = email;
    credentials.password = password;
    std::fill(password.begin(), password.end(), '\0');
    std::fill(email.begin(), email.end(), '\0');
    if (!autologin::Valid(*profile, credentials))
    {
        credentials.Wipe();
        ShowError(Tr("Auto-login"), Tr("Use printable English characters; check the login and password length."));
        return;
    }
    if (!autologin::Save(*profile, credentials))
    {
        credentials.Wipe();
        ShowError(Tr("Auto-login"), opennow::Tr("Could not write ") + autologin::CredentialsPath(*profile));
        return;
    }
    credentials.Wipe();
    UpdateAutoLoginButton();
    brls::Application::notify(Tr(profile->game==autologin::Game::Epic?
        "Epic saved. Open its email screen; hold Minus + R3, then press X.":"Auto-login saved. On the sign-in screen: Minus + R3, then X."));
}

void GameDetailView::OpenAutoLoginCredentialsMenu(const autologin::Profile* profile)
{
    const auto* game_profile=autologin::FindProfile(data_.title);
    if(!profile&&game_profile&&EpicAutoLoginAvailable()) {
        auto* choose=new brls::Dialog(Tr("Choose an auto-login profile"));
        choose->addButton(Tr("Game account"),[this,game_profile]{OpenAutoLoginCredentialsMenu(game_profile);});
        choose->addButton(Tr("Epic Games account"),[this]{OpenAutoLoginCredentialsMenu(&autologin::GetProfile(autologin::Game::Epic));});
        choose->addButton(Tr("Cancel"),[]{});choose->setCancelable(true);choose->open();return;
    }
    if(!profile)profile=game_profile?game_profile:EpicAutoLoginAvailable()?&autologin::GetProfile(autologin::Game::Epic):nullptr;
    if(!profile){ShowStoreSelector(false);return;}
    if(profile->game==autologin::Game::Epic&&!EpicAutoLoginAvailable())return;
    if (!profile) return;
    auto credentials = autologin::Load(*profile);
    const bool configured = autologin::Valid(*profile, credentials);
    credentials.Wipe();
    if (!configured && profile->game!=autologin::Game::Epic)
    {
        ConfigureAutoLoginCredentials(profile);
        return;
    }

    std::string message =
        std::string(profile->title) + " — " + Tr("Auto-login") + "\n\n" +
        Tr("Credentials are stored as plain text at:") + "\n" + autologin::CredentialsPath(*profile) + "\n\n" +
        Tr(profile->game == autologin::Game::Epic ?
            "For sign-in without a manual code, disable 2FA in Epic account settings. Otherwise complete 2FA manually." :
            profile->game == autologin::Game::Nte
            ? "NTE: open the first email sign-in screen; hold Minus + R3, then X. B cancels."
            : "Open the email/password form; hold Minus + R3, then X. B cancels.");
    if(profile->game==autologin::Game::Epic) {
        message=Tr("Epic: open the email screen, hold Minus + R3, then X. B cancels.")+"\n\n"+
            Tr("For sign-in without a manual code, disable 2FA in Epic account settings. Otherwise complete 2FA manually.")+"\n\n"+
            Tr("Credentials are stored as plain text at:")+"\n"+autologin::CredentialsPath(*profile);
    }
    auto* dialog = new brls::Dialog(message);
    dialog->addButton(Tr("Edit credentials"), [this,profile] { ConfigureAutoLoginCredentials(profile); });
    dialog->addButton(Tr("Clear credentials"), [this, profile] {
        if (!autologin::Clear(*profile)) { ShowError(Tr("Auto-login"),Tr("Could not clear credentials")); return; }
        UpdateAutoLoginButton();
        brls::Application::notify(Tr("Auto-login credentials removed"));
    });
    dialog->addButton(Tr("Cancel"), [] {});
    dialog->setCancelable(true);
    dialog->open();
}

void GameDetailView::ShowStoreSelector(bool launch_after_selection)
{
    if(ownership_pending_)return;
    if (data_.variants.empty())
    {
        LaunchSelectedVariant();
        return;
    }

    std::vector<std::string> labels;
    labels.reserve(data_.variants.size());
    for (const auto& variant : data_.variants)
        labels.push_back(VariantLabel(variant));

    auto* dropdown = new brls::Dropdown(
        Tr("Choose game store"), labels,
        [this](int selected) {
            if (selected < 0 || static_cast<size_t>(selected) >= data_.variants.size())
                return;
            selected_variant_index_ = static_cast<size_t>(selected);
            launcher_preference_loaded_ = true;
            const auto& variant = data_.variants[selected_variant_index_];
            data_.launch_app_id = variant.id;
            client_.SaveLauncherPreference(ActiveUserId(), data_.game_id, variant.id);
            UpdateStoreButton();
            brls::Application::notify(opennow::Tr("Store selected: ") + SafeText(variant.store, variant.id));
        }, static_cast<int>(selected_variant_index_),
        [this, launch_after_selection](int selected) {
            if (launch_after_selection && selected >= 0)
                LaunchSelectedVariant();
        });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void GameDetailView::LaunchSelectedVariant()
{
    std::string launch_app_id = data_.launch_app_id;
    std::string store = data_.stores;
    if (selected_variant_index_ < data_.variants.size())
    {
        launch_app_id = data_.variants[selected_variant_index_].id;
        store = data_.variants[selected_variant_index_].store;
    }

    if (launch_app_id.empty())
    {
        ShowError(opennow::Tr("Launch Error"), opennow::Tr("This game has no launch App ID."));
        return;
    }

    if (screenshot_image_) { screenshot_image_->Pause(); gallery_paused_ = true; }
    description_cancelled_->store(true);
    LaunchSessionDialog(client_, *AppState::Instance().session(), launch_app_id,
                        data_.title, store, data_.title, data_.game_id,
                        data_.image_url);
}

void GameDetailView::CreateSwitchShortcut()
{
    shortcut::LaunchRequest request;
    request.launch_app_id = data_.launch_app_id;
    request.store = data_.stores;
    if (selected_variant_index_ < data_.variants.size())
    {
        request.launch_app_id = data_.variants[selected_variant_index_].id;
        request.store = data_.variants[selected_variant_index_].store;
    }
    request.game_id = data_.game_id;
    request.title = data_.title;
    request.image_url = data_.image_url;
    if (!shortcut::IsValid(request))
    {
        ShowError(
            Tr("Shortcut unavailable"),
            Tr("This game does not expose a valid GeForce NOW launch ID."));
        return;
    }

    GameDetailData context = data_;
    context.launch_app_id = request.launch_app_id;
    context.stores = request.store;
    const auto alive = alive_;
    auto capture = [this, alive, context = std::move(context), request = std::move(request)]() mutable {
        if (!alive->load()) return;
        SaveShortcutReturnContext(context);
        // Defer activity changes until the next input cycle, outside frame drawing.
        brls::sync([this, alive, request = std::move(request)]() mutable {
            if (!alive->load()) return;
            brls::Application::giveFocus(play_button_);
            auto* dialog = new brls::Dialog(new ShortcutSetupView(std::move(request)));
            dialog->setCancelable(false);
            dialog->open();
        });
    };
#ifdef __SWITCH__
    auto* video = static_cast<brls::SwitchVideoContext*>(
        brls::Application::getPlatform()->getVideoContext());
    video->captureNextFrame(std::move(capture));
#else
    capture();
#endif
}

GameDetailData MakeLibraryGameDetail(const GameInfo& game)
{
    GameDetailData data;
    data.title         = game.title;
    data.game_id       = game.uuid.empty() ? game.id : game.uuid;
    data.subtitle      = PrimaryStore(game) + " " + Tr("library title");
    data.image_url     = game.image_url;
    data.poster_url    = game.poster_url;
    data.hero_url      = game.hero_url;
    data.screenshots   = game.screenshots;
    data.launch_app_id = game.launch_app_id;
    data.publisher     = game.publisher;
    data.developer     = game.developer;
    data.description   = game.description;
    data.stores        = JoinStores(game);
    data.membership_tier_label = game.membership_tier_label;
    data.last_played   = game.last_played;
    data.genres        = game.genres;
    data.feature_labels = game.feature_labels;
    data.content_ratings = game.content_ratings;
    data.supported_controls = game.supported_controls;
    data.owned         = true;
    data.variants      = game.variants;
    data.variants.erase(
        std::remove_if(data.variants.begin(), data.variants.end(), [](const GameVariant& variant) {
            return !IsNumericLaunchId(variant.id);
        }),
        data.variants.end());
    return data;
}

GameDetailData MakeCatalogGameDetail(const PublicGame& game)
{
    GameDetailData data;
    data.title         = game.title;
    data.game_id       = game.uuid.empty() ? game.id : game.uuid;
    data.subtitle      = game.store + " " +
        Tr(game.is_in_library ? "library title" : "catalog title");
    data.image_url     = game.image_url;
    data.poster_url    = game.poster_url;
    data.hero_url      = game.hero_url;
    data.screenshots   = game.screenshots;
    data.launch_app_id = game.launch_app_id.empty() ? game.id : game.launch_app_id;
    data.publisher     = game.publisher;
    data.developer     = game.developer;
    data.description   = game.description;
    data.stores        = game.store;
    data.membership_tier_label = game.membership_tier_label;
    data.genres        = game.genres;
    data.feature_labels = game.feature_labels;
    data.content_ratings = game.content_ratings;
    data.supported_controls = game.supported_controls;
    data.owned         = game.is_in_library;
    data.variants      = game.variants;
    if (data.variants.empty())
    {
        GameVariant variant;
        variant.id = game.id;
        variant.store = game.store;
        data.variants.push_back(std::move(variant));
    }
    return data;
}

} // namespace opennow
