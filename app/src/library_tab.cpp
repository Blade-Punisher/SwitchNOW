#include "library_tab.hpp"

#include "app_state.hpp"
#include "game_detail_view.hpp"
#include "game_card_view.hpp"
#include "game_grid_navigation.hpp"
#include "game_feed_pager.hpp"
#include "library_sort.hpp"
#include "membership_label.hpp"
#include "providers_tab.hpp"
#include "ui_action_guard.hpp"
#include "ui_helpers.hpp"
#include "qr_login_dialog.hpp"
#include "localization.hpp"
#include "membership_tier_style.hpp"
#include "game_feed_policy.hpp"
#include "network_resource_policy.hpp"
#include "runtime_journal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace opennow
{
namespace
{

constexpr size_t kCardsPerRow       = 5;
constexpr std::array<const char*, 6> kStoreFilters = {"All", "Steam", "Epic", "Ubisoft", "Xbox", "Battle.net"};
constexpr std::array<const char*, 4> kLibrarySortModes = {
    "Last Played", "Last Added", "A-Z", "Store"};

brls::Label* MakeParagraph(const std::string& text, float bottom_margin = 16.0f, float font_size = 18.0f)
{
    auto* label = new brls::Label();
    label->setText(Tr(text));
    label->setFontSize(font_size);
    label->setMarginBottom(bottom_margin);
    return label;
}

brls::Button* MakeToolbarButton(const std::string& text)
{
    auto* button = new brls::Button();
    const std::string localized = Tr(text);
    button->setText(localized);
    button->setFontSize(15);
    button->setStyle(&brls::BUTTONSTYLE_BORDERED);
    button->setHeight(42);
    button->setWidth(static_cast<float>(std::max<size_t>(170, localized.size() * 9 + 38)));
    button->setMarginRight(10);
    return button;
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ContainsText(const std::string& haystack, const std::string& needle)
{
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

std::string CompactSearchQuery(const std::string& text, size_t max_codepoints = 19)
{
    size_t offset = 0;
    size_t count = 0;
    while (offset < text.size() && count < max_codepoints)
    {
        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        size_t length = 1;
        if ((lead & 0xE0) == 0xC0)
            length = 2;
        else if ((lead & 0xF0) == 0xE0)
            length = 3;
        else if ((lead & 0xF8) == 0xF0)
            length = 4;
        if (offset + length > text.size())
            length = 1;
        offset += length;
        ++count;
    }
    return offset < text.size() ? text.substr(0, offset) + "..." : text;
}

std::string PrimaryStore(const GameInfo& game)
{
    if (!game.available_stores.empty())
        return game.available_stores.front();

    if (!game.publisher.empty())
        return game.publisher;

    return "GeForce NOW";
}

bool MatchesStoreFilter(const GameInfo& game, size_t filter_index)
{
    if (filter_index == 0 || filter_index >= kStoreFilters.size())
        return true;

    const std::string filter = kStoreFilters[filter_index];
    if (ContainsText(PrimaryStore(game), filter) || ContainsText(game.publisher, filter))
        return true;

    for (const std::string& store : game.available_stores)
    {
        if (ContainsText(store, filter))
            return true;
    }

    if (filter == "Battle.net")
        return ContainsText(PrimaryStore(game), "battle") || ContainsText(game.publisher, "blizzard");

    return false;
}

} // namespace

LibraryTab::LibraryTab()
    : brls::Box(brls::Axis::COLUMN)
{
    setPadding(18, 32, 18, 32);
    setBackgroundColor(nvgRGB(11, 12, 15));

    auto* heading = new brls::Box(brls::Axis::ROW);
    heading->setAlignItems(brls::AlignItems::CENTER);
    heading->setMarginBottom(8);
    auto* accent = new brls::Rectangle();
    accent->setWidth(4);
    accent->setHeight(30);
    accent->setMarginRight(12);
    accent->setColor(nvgRGB(92, 238, 139));
    heading->addView(accent);
    auto* title = MakeParagraph(opennow::Tr("My Library"), 0.0f);
    title->setFontSize(28);
    title->setTextColor(nvgRGB(248, 249, 251));
    heading->addView(title);
    addView(heading);

    account_label_ = MakeParagraph(opennow::Tr("Checking saved GeForce NOW session..."), 8.0f, 14.0f);
    account_label_->setTextColor(nvgRGB(144, 151, 163));
    addView(account_label_);

    auto* actions = new brls::Box(brls::Axis::ROW);
    actions->setMarginBottom(8);

    sign_in_button_ = new brls::Button();
    sign_in_button_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    sign_in_button_->setText(Tr("Sign in to GeForce NOW"));
    sign_in_button_->setMarginRight(12);
    sign_in_button_->setHeight(42);
    sign_in_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        return RunUiAction("library.account.login", [this]() { BeginLogin(); });
    });
    actions->addView(sign_in_button_);

    refresh_button_ = new brls::Button();
    refresh_button_->setText(Tr("Refresh library"));
    refresh_button_->setMarginRight(12);
    refresh_button_->setHeight(42);
    refresh_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        return RunUiAction("library.account.refresh", [this]() { ReloadLibrary(false, true); });
    });
    actions->addView(refresh_button_);

    logout_button_ = new brls::Button();
    logout_button_->setText(Tr("Sign out"));
    logout_button_->setHeight(42);
    logout_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        return RunUiAction("library.account.logout", [this]() { Logout(); });
    });
    actions->addView(logout_button_);
    account_buttons_ = {sign_in_button_, refresh_button_, logout_button_};

    addView(actions);

    resume_card_ = new brls::Box(brls::Axis::ROW);
    resume_card_->setPadding(10, 16, 10, 16);
    resume_card_->setCornerRadius(12);
    resume_card_->setBackgroundColor(nvgRGB(17, 34, 27));
    resume_card_->setAlignItems(brls::AlignItems::CENTER);
    resume_card_->setMarginBottom(8);
    auto* resume = MakeToolbarButton(opennow::Tr("Continue active session"));
    resume_button_ = resume;
    resume->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    resume->registerClickAction([this](brls::View*) {
        return RunUiAction("library.session.resume", [this] {
            const auto& app = AppState::Instance();
            if (app.HasSession()) ResumeSavedSessionDialog(client_, *app.session());
            else ShowDialog(opennow::Tr("Continue active session"), opennow::Tr("Sign in to GeForce NOW"));
        });
    });
    resume_card_->addView(resume);
    resume_detail_ = MakeParagraph("", 0, 13);
    resume_detail_->setTextColor(nvgRGB(182, 207, 193));
    resume_detail_->setGrow(1);
    resume_card_->addView(resume_detail_);
    resume_card_->setVisibility(brls::Visibility::GONE);
    addView(resume_card_);

    auto* toolbar = new brls::Box(brls::Axis::ROW);
    toolbar->setAlignItems(brls::AlignItems::CENTER);
    toolbar->setMarginBottom(8);
    search_button_ = MakeToolbarButton("Y  " + Tr("Search"));
    filter_button_ = MakeToolbarButton("ZL  " + Tr("All stores"));
    sort_button_   = MakeToolbarButton("ZR  " + Tr("Last Played"));
    more_button_   = MakeToolbarButton("X  " + Tr("More / Refresh"));
    search_button_->setStyle(&brls::BUTTONSTYLE_HIGHLIGHT);
    toolbar_buttons_ = {search_button_, filter_button_, sort_button_, more_button_};
    for (brls::View* button : toolbar_buttons_)
        toolbar->addView(button);
    addView(toolbar);

    search_button_->registerClickAction([this](brls::View*) {
        return RunUiAction("library.search.button", [this]() { BeginSearch(); });
    });
    filter_button_->registerClickAction([this](brls::View*) {
        return RunUiAction("library.filter.button", [this]() { CycleStoreFilter(); });
    });
    sort_button_->registerClickAction([this](brls::View*) {
        return RunUiAction("library.sort.button", [this]() { CycleSortMode(); });
    });
    more_button_->registerClickAction([this](brls::View*) {
        return RunUiAction("library.more.button", [this]() { LoadMoreOrRefresh(); });
    });

    search_context_label_ = MakeParagraph("", 5.0f, 16.0f);
    search_context_label_->setTextColor(nvgRGB(88, 230, 146));
    search_context_label_->setVisibility(brls::Visibility::GONE);
    addView(search_context_label_);

    status_label_ = MakeParagraph(opennow::Tr("Sign in to load your GeForce NOW library."), 8.0f, 14.0f);
    status_label_->setTextColor(nvgRGB(132, 139, 151));
    addView(status_label_);

    scrolling_frame_ = new brls::ScrollingFrame();
    scrolling_frame_->setGrow(1.0f);
    scrolling_frame_->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    grid_container_ = new brls::Box(brls::Axis::COLUMN);
    grid_container_->setPadding(0, 0, 30, 0);
    scrolling_frame_->setContentView(grid_container_);

    addView(scrolling_frame_);

    registerAction(opennow::Tr("Search"), brls::BUTTON_Y, [this](brls::View* view) {
        (void)view;
        return RunUiAction("library.search.hotkey", [this]() { BeginSearch(); });
    }, false, false);

    registerAction(opennow::Tr("Login / More / Refresh"), brls::BUTTON_X, [this](brls::View* view) {
        (void)view;
        return RunUiAction("library.more.hotkey", [this]() { LoadMoreOrRefresh(); });
    }, false, false);

    registerAction(opennow::Tr("Store Filter"), brls::BUTTON_LT, [this](brls::View* view) {
        (void)view;
        return RunUiAction("library.filter.hotkey", [this]() { CycleStoreFilter(); });
    }, false, false);

    registerAction(opennow::Tr("Sort"), brls::BUTTON_RT, [this](brls::View* view) {
        (void)view;
        return RunUiAction("library.sort.hotkey", [this]() { CycleSortMode(); });
    }, false, false);

    registerAction(Tr("Clear search"), brls::BUTTON_B, [this](brls::View*) {
        if (search_query_.empty())
            return false;
        return RunUiAction("library.search.clear", [this]() { ClearSearch(); });
    }, false, false);
    ScheduleRefreshMaintenance();
}

LibraryTab::~LibraryTab()
{
    alive_->store(false);
    if (paging_container_ && !paging_button_attached_) delete paging_container_;
    LogUiAction("library", "destroy");
}

void LibraryTab::BeginSearch()
{
    const auto alive = alive_;
    brls::Application::giveFocus(search_button_);
    brls::Application::getImeManager()->openForText(
        [this, alive](std::string text) {
            if (!alive->load())
                return;
            RunUiAction("library.search.result", [this, text = std::move(text)]() mutable {
                MoveFocusBeforeDestroy(grid_container_, search_button_);
                search_query_ = std::move(text);
                page_index_ = 0;
                UpdateSearchUi();
                RebuildGrid();
            });
        },
        Tr("Search Library"), Tr("Enter a title to filter your games"), 64, search_query_);
}

bool LibraryTab::ClearSearch()
{
    if (search_query_.empty())
        return false;

    MoveFocusBeforeDestroy(grid_container_, search_button_);
    search_query_.clear();
    page_index_ = 0;
    UpdateSearchUi();
    RebuildGrid();
    brls::Application::giveFocus(search_button_);
    return true;
}

void LibraryTab::UpdateSearchUi()
{
    if (!search_button_ || !search_context_label_)
        return;

    if (search_query_.empty())
    {
        search_button_->setText("Y  " + Tr("Search"));
        search_button_->setStyle(&brls::BUTTONSTYLE_HIGHLIGHT);
        search_context_label_->setText("");
        search_context_label_->setVisibility(brls::Visibility::GONE);
        return;
    }

    const std::string compact_query = CompactSearchQuery(search_query_);
    search_button_->setText("Y  " + Tr("Search") + ": " + compact_query);
    search_button_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    search_context_label_->setText(
        TrFormat("Searching for: {0}", {search_query_}) + "  |  " + Tr("B  Clear search"));
    search_context_label_->setVisibility(brls::Visibility::VISIBLE);
}

void LibraryTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);

    UpdateSearchUi();
    EnsureSessionLoaded();
    if(feed_revision_!=game_feed::Revision()){
        feed_revision_=game_feed::Revision();requests_->Invalidate();loading_=false;last_library_sync_={};refresh_schedule_.Reset();
        games_=AppState::Instance().library_games();
    }

    UpdateSessionUi();

    auto& state = AppState::Instance();
    const std::string owner = state.HasSession() ? game_feed::OwnerKey(state.session()->user.user_id,
        state.session()->provider.code, state.session()->provider.streaming_service_url) : std::string{};
    if (owner != library_owner_) {
        library_owner_ = owner; requests_->Invalidate(); loading_ = false; last_library_sync_ = {}; refresh_schedule_.Reset(); games_.clear(); feed_complete_ = false; page_index_ = 0; RebuildGrid();
    }
    if (state.HasLibraryGames())
    {
        games_ = state.library_games();
        RebuildGrid();

    }

    // Also covers the race where a deferred callback arrives after willAppear.
    refresh_ui_dirty_=true;
    MaintainRefresh();
}

void LibraryTab::ScheduleRefreshMaintenance()
{
    const auto alive=alive_;
    brls::delay(500,[this,alive] {
        if(!alive->load())return;
        MaintainRefresh();
        ScheduleRefreshMaintenance();
    });
}

void LibraryTab::MaintainRefresh()
{
    const auto now=std::chrono::steady_clock::now();
    if(feed_revision_!=game_feed::Revision()) {
        feed_revision_=game_feed::Revision();requests_->Invalidate();loading_=false;
        refresh_schedule_.Reset();refresh_ui_dirty_=true;
        games_=AppState::Instance().library_games();
    }
    if(loading_&&refresh_schedule_.Expired(now)) {
        requests_->Invalidate();loading_=false;refresh_schedule_.Failed(now);refresh_ui_dirty_=true;
        LogRuntimeEvent("library","refresh_timeout","cached_games="+std::to_string(games_.size()));
    }
    if(!IsFrontmostLibrary()||network::launch_count.load()!=0)return;
    auto& state=AppState::Instance();
    const auto owner=state.HasSession()?game_feed::OwnerKey(state.session()->user.user_id,
        state.session()->provider.code,state.session()->provider.streaming_service_url):std::string{};
    if(owner!=library_owner_) {
        requests_->Invalidate();loading_=false;refresh_schedule_.Reset();library_owner_=owner;
        games_=state.library_games();feed_complete_=false;page_index_=0;refresh_ui_dirty_=true;
    }
    if(refresh_ui_dirty_) {refresh_ui_dirty_=false;UpdateSessionUi();RebuildGrid();}
    if(!loading_&&state.HasSession()&&!state.session()->reauthentication_required&&refresh_schedule_.Due(now))ReloadLibrary(true);
}

void LibraryTab::EnsureSessionLoaded()
{
    auto& state = AppState::Instance();
    if (state.IsSessionLoaded())
        return;

    AuthSession session;
    if (client_.LoadSavedSession(session))
        state.SetSession(std::move(session));
    else
        state.MarkSessionLoaded();
}

void LibraryTab::UpdateSessionUi()
{
    UpdateResumeCard();
    const auto& state = AppState::Instance();
    if (!state.HasSession())
    {
        account_label_->setTextColor(membership::TextColor({}));
        account_label_->setText(Tr("No GeForce NOW account connected on this Switch yet."));
        status_label_->setText(Tr("Sign in to load your GeForce NOW library."));
        sign_in_button_->setText(Tr("Sign in to GeForce NOW"));
        refresh_button_->setState(brls::ButtonState::DISABLED);
        logout_button_->setState(brls::ButtonState::DISABLED);
        return;
    }

    const AuthSession& session = *state.session();
    const std::string membership = session.user.membership_tier_verified
        ? Tr(session.user.membership_tier)
        : Tr("Unknown");

    if (session.reauthentication_required)
    {
        account_label_->setTextColor(nvgRGB(255, 170, 174));
        account_label_->setText(TrFormat(
            "Reconnect required for {0}", {session.user.display_name}));
        status_label_->setText(Tr("NVIDIA revoked or expired the saved refresh token."));
        sign_in_button_->setText(Tr("Reconnect this account"));
        refresh_button_->setState(brls::ButtonState::DISABLED);
    }
    else
    {
        account_label_->setTextColor(membership::TextColor(
            session.user.membership_tier_verified
                ? session.user.membership_tier
                : std::string {}));
        account_label_->setText(TrFormat(
            "Connected as {0}  |  {1} tier",
            {session.user.display_name, membership}));
        sign_in_button_->setText(Tr("Add another account"));
        refresh_button_->setState(brls::ButtonState::ENABLED);
    }
    logout_button_->setState(brls::ButtonState::ENABLED);
}

void LibraryTab::UpdateResumeCard()
{
    if (!resume_card_) return;
    const auto& app = AppState::Instance();
    const auto saved = app.HasSession() && !app.session()->reauthentication_required
        ? client_.LoadResumableCloudSession(*app.session()) : std::nullopt;
    resume_card_->setVisibility(saved ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (!saved) return;
    const std::time_t when = static_cast<std::time_t>(saved->started_at_ms / 1000);
    std::tm value {};
#ifdef _WIN32
    gmtime_s(&value, &when);
#else
    gmtime_r(&when, &value);
#endif
    std::ostringstream time;
    time << std::put_time(&value, "%d.%m %H:%M UTC");
    resume_detail_->setText((saved->title.empty() ? Tr("GeForce NOW session") : saved->title) +
        "\n" + (saved->server.empty() ? Tr("Saved server") : saved->server) +
        "  |  " + time.str() + "\n" + Tr("Availability is checked before reconnecting"));
}

std::vector<std::vector<brls::View*>> LibraryTab::NavigationHeaderRows() const
{
    std::vector<std::vector<brls::View*>> rows {account_buttons_};
    if (resume_card_ && resume_card_->getVisibility() == brls::Visibility::VISIBLE)
        rows.push_back({resume_button_});
    rows.push_back(toolbar_buttons_);
    return rows;
}

void LibraryTab::BeginLogin()
{
    try
    {
        auto& state = AppState::Instance();
        const auto alive = alive_;

        auto on_success = [this, alive]() {
            if (!alive->load())
                return;
            UpdateSessionUi();
            ReloadLibrary();
        };

        if (state.HasSession() && state.session()->reauthentication_required)
        {
            auto* dialog = new QrLoginDialog(
                state.session()->provider, client_, std::move(on_success));
            brls::Application::pushActivity(new brls::Activity(dialog));
            return;
        }

        auto* providers = new ProvidersTab(std::move(on_success));
        brls::Application::pushActivity(new brls::Activity(providers));
    }
    catch (const std::exception& ex)
    {
        ShowError(opennow::Tr("GeForce NOW Login Failed"), ex.what());
    }
}

void LibraryTab::Logout()
{
    requests_->Invalidate();loading_=false;refresh_schedule_.Reset();

    games_.clear();
    client_.ClearSavedSession();

    auto& state = AppState::Instance();
    state.SetLibraryGames({});

    AuthSession next;
    if (client_.LoadSavedSession(next))
    {
        state.SetSession(std::move(next));
        UpdateSessionUi();
        RebuildGrid();
        brls::Application::notify(opennow::Tr("Active account removed; switched to another saved account"));
        return;
    }

    state.ClearSession();

    UpdateSessionUi();
    RebuildGrid();
    brls::Application::notify(opennow::Tr("GeForce NOW account disconnected"));
}

bool LibraryTab::IsFrontmostLibrary()
{
    const auto stack = brls::Application::getActivitiesStack();
    return !stack.empty() && stack.back() == getParentActivity();
}

void LibraryTab::ReloadLibrary(bool background, bool force_full)
{
    auto& state = AppState::Instance();
    if (loading_ || !state.HasSession()) { UpdateSessionUi(); return; }
    loading_ = true;
    feed_complete_ = false;
    if (games_.empty()) RebuildGrid();
    last_library_sync_ = std::chrono::steady_clock::now();
    refresh_schedule_.Begin(last_library_sync_,!background);
    const auto deadline=last_library_sync_+game_feed::RefreshSchedule::kBudget;
    LogRuntimeEvent("library","refresh_begin", "background="+std::to_string(background)+" cached_games="+std::to_string(games_.size()));
    status_label_->setText(Tr("Syncing your GeForce NOW library..."));
    AuthSession session = *state.session();
    const auto owner = game_feed::OwnerKey(session.user.user_id, session.provider.code, session.provider.streaming_service_url);
    library_owner_ = owner;
    GfnClient client = client_;
    const auto alive = alive_;
    const auto requests = requests_;
    const auto ticket = requests->Begin();
    const auto revision=game_feed::Revision();
    auto owns = [alive, requests, ticket] { return alive->load() && requests->Current(ticket); };
    auto valid = [owns, revision] { return owns() && revision==game_feed::Revision(); };
    auto same_account = [owner] {
        const auto& current = AppState::Instance();
        return current.HasSession() && owner == game_feed::OwnerKey(current.session()->user.user_id,
            current.session()->provider.code, current.session()->provider.streaming_service_url);
    };
    brls::async([this, alive, client, session = std::move(session), background, force_full, same_account, valid, owns, deadline]() mutable {
        try {
            HttpCancellationScope cancellation([valid,deadline] { return !valid() || std::chrono::steady_clock::now()>=deadline; });
            auto preview = [this, alive, same_account, valid](std::vector<GameInfo> games, bool full) {
                brls::sync([this, alive, same_account, valid, games = std::move(games), full]() mutable {
                    if (!valid() || !same_account()) return;
                    games_ = std::move(games); library_sync_complete_ = full; feed_complete_=full;refresh_ui_dirty_=true;
                    AppState::Instance().SetLibraryGames(games_,full);
                    games_=AppState::Instance().library_games();
                    // A shortcut/launch dialog owns focus. Do not rebuild its
                    // underlying menu or surface library UI over that dialog.
                    if (IsFrontmostLibrary() && network::launch_count.load() == 0) {
                        RebuildGrid();refresh_ui_dirty_=false;
                        status_label_->setText(Tr(full ? "Showing saved library. Checking for updates..." :
                            "First games loaded. Completing your library in the background..."));
                    }
                });
            };
            bool complete = false;
            auto games = client.FetchLibraryGames(session, &complete, preview, force_full);
            brls::sync([this, alive, same_account, valid, games = std::move(games), session = std::move(session), background, complete, owns]() mutable {
                if (!owns()) return;
                loading_ = false;
                if (!valid() || !same_account()) {refresh_schedule_.Reset();return;}
                if(complete)refresh_schedule_.Complete(std::chrono::steady_clock::now());
                else refresh_schedule_.Failed(std::chrono::steady_clock::now());
                refresh_ui_dirty_=true;
                LogRuntimeEvent("library","refresh_finished","complete="+std::to_string(complete)+" games="+std::to_string(games.size()));
                games_ = std::move(games); library_sync_complete_ = complete; feed_complete_ = complete;
                last_library_sync_ = std::chrono::steady_clock::now();
                auto& current = AppState::Instance(); current.SetLibraryGames(games_,complete);games_=current.library_games();
                if (!IsFrontmostLibrary() || network::launch_count.load() != 0) return;
                current.SetSession(std::move(session));
                UpdateSessionUi(); RebuildGrid();refresh_ui_dirty_=false;
                if (!background) brls::Application::notify(Tr(complete ? "Library refreshed" : "Library sync incomplete. Refresh to retry."));
            });
        } catch (const game_feed::Deferred&) {
            brls::sync([this, alive, same_account, valid, owns] {
                if (!owns()) return;
                loading_ = false;
                if (!valid() || !same_account()) {refresh_schedule_.Reset();return;}
                refresh_schedule_.Deferred(std::chrono::steady_clock::now());refresh_ui_dirty_=true;
                LogRuntimeEvent("library","refresh_deferred");
                last_library_sync_ = {}; // resume on next menu appearance
                if (IsFrontmostLibrary() && games_.empty()) RebuildGrid();
                if (IsFrontmostLibrary()) status_label_->setText(Tr("Library refresh paused while launching the game."));
            });
        } catch (const std::exception& ex) {
            const std::string error = ex.what();
            brls::sync([this, alive, error, background, same_account, valid, owns] {
                if (!owns()) return;
                loading_ = false;
                if (!valid() || !same_account()) {refresh_schedule_.Reset();return;}
                refresh_schedule_.Failed(std::chrono::steady_clock::now());refresh_ui_dirty_=true;
                LogRuntimeEvent("library","refresh_failed","cached_games="+std::to_string(games_.size()));
                if (IsFrontmostLibrary() && games_.empty()) RebuildGrid();
                if (game_feed::ShowFailure(false, background, IsFrontmostLibrary() && network::launch_count.load() == 0, true)) {
                    ShowError(opennow::Tr("Library Sync Failed"), error); UpdateSessionUi();
                } else if (IsFrontmostLibrary()) {
                    status_label_->setText(Tr(games_.empty() ? "Library refresh unavailable. Press X to retry." :
                        "Showing cached library. NVIDIA background refresh is temporarily unavailable."));
                }
            });
        }
    }, true);
}

void LibraryTab::RebuildGrid()
{
    if (rebuilding_)
        return;
    rebuilding_ = true;
    struct ResetFlag { bool& flag; ~ResetFlag() { flag = false; } } reset {rebuilding_};
    const auto focused_game = grid_focus_.Capture();

    const bool paging_had_focus = paging_container_ && paging_container_->Owns(brls::Application::getCurrentFocus());
    MoveFocusBeforeDestroy(grid_container_, search_button_);
    DetachPagingButton();
    grid_container_->clearViews();
    card_rows_.clear();
    grid_focus_.Clear();
    first_card_ = nullptr;
    load_more_pending_ = false;

    const auto& state = AppState::Instance();
    if (!state.HasSession())
    {
        WireVerticalGridNavigation(NavigationHeaderRows());
        grid_container_->addView(MakeParagraph(
            opennow::Tr("After login, this screen will show your owned GeForce NOW titles with cover art and store labels."),
            0.0f));
        return;
    }

    BuildPage();
    if (!grid_focus_.Restore(focused_game) && paging_had_focus && paging_button_attached_)
        brls::Application::giveFocus(paging_container_->NextTarget());
}

void LibraryTab::BuildPage()
{
    const bool load_more_had_focus = paging_container_ && paging_container_->Owns(brls::Application::getCurrentFocus());
    std::vector<size_t> filtered_indices;
    std::string lower_query = ToLower(search_query_);

    for (size_t i = 0; i < games_.size(); ++i)
    {
        const GameInfo& game = games_[i];
        const bool matches_query = lower_query.empty() || ToLower(game.title).find(lower_query) != std::string::npos;
        if (matches_query && MatchesStoreFilter(game, store_filter_index_))
        {
            filtered_indices.push_back(i);
        }
    }

    SortLibraryIndices(
        filtered_indices, games_, static_cast<LibrarySortMode>(sort_mode_index_));

    filtered_count_ = filtered_indices.size();
    const auto page = game_feed::Window(filtered_count_, page_index_);
    page_index_ = page.index;
    std::string status = TrFormat(
        "Loaded {0} games.", {std::to_string(games_.size())});
    status += " " + TrFormat(
        "Filter: {0}.", {Tr(kStoreFilters[store_filter_index_])});
    status += " " + TrFormat(
        "Sort: {0}.", {Tr(kLibrarySortModes[sort_mode_index_])});
    if (!search_query_.empty())
    {
        status += " " + TrFormat("Searching for: {0}", {search_query_});
        status += " " + TrFormat(
            "Found {0} matches.", {std::to_string(filtered_count_)});
    }
    if (filtered_count_ > 0)
        status += " " + TrFormat("Showing {0}-{1} of {2} (page {3}/{4}).",
            {std::to_string(page.start+1),std::to_string(page.end),std::to_string(filtered_count_),
             std::to_string(page.index+1),std::to_string(page.count)});
    else status += " " + Tr("Press X to refresh.");
    if (!library_sync_complete_) status += " " + Tr("Library sync incomplete. Refresh to retry.");
    status_label_->setText(status);
    filter_button_->setText("ZL  " + Tr(kStoreFilters[store_filter_index_]));
    sort_button_->setText("ZR  " + Tr(kLibrarySortModes[sort_mode_index_]));

    if (filtered_indices.empty())
    {
        grid_container_->addView(MakeParagraph(EmptyFeedMessage(loading_, !games_.empty(), feed_complete_), 0.0f));
        WireVerticalGridNavigation(NavigationHeaderRows());
        return;
    }

    brls::View* first_new_card = nullptr;

    for (size_t start = page.start; start < page.end; start += kCardsPerRow)
    {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setMarginBottom(2);
        std::vector<brls::View*> card_row;

        const size_t end = std::min(start + kCardsPerRow, page.end);
        for (size_t i = start; i < end; ++i)
        {
            const size_t index = filtered_indices[i];
            const GameInfo& game = games_[index];

            GameCardDisplay display;
            display.title     = game.title;
            display.subtitle  = PrimaryStore(game);
            display.badge     = BuildMembershipBadge(
                Tr(game.last_played.empty() ? "In library" : "Recently played"),
                Tr(game.membership_tier_label));
            display.image_url = game.image_url;

            auto* card = new GameCardView(display, [this, index]() {
                OpenGameDialog(index);
            });

            if (!first_new_card)
                first_new_card = card;
            if (!first_card_)
                first_card_ = card;

            grid_focus_.Remember(card, game.id);
            row->addView(card);
            card_row.push_back(card);
        }

        grid_container_->addView(row);
        card_rows_.push_back(std::move(card_row));
    }

    auto navigation_rows = NavigationHeaderRows();
    navigation_rows.insert(navigation_rows.end(), card_rows_.begin(), card_rows_.end());
    WireVerticalGridNavigation(navigation_rows);
    
    AttachPagingButton();
    navigation_rows.push_back(paging_container_->Buttons());
    WireVerticalGridNavigation(navigation_rows);
    if (!card_rows_.empty())
        for (auto* card : card_rows_.back())
            card->setCustomNavigationRoute(brls::FocusDirection::DOWN, paging_container_->NextTarget());

    if (first_new_card && (load_more_had_focus || !brls::Application::getCurrentFocus()))
    {
        brls::Application::giveFocus(first_new_card);
    }
}

void LibraryTab::EnsurePagingButton()
{
    if (!paging_container_)
        paging_container_ = new GameFeedPager([this, alive=alive_](size_t page) { if (alive->load()) GoToPage(page); });
}

void LibraryTab::DetachPagingButton()
{
    if (!paging_container_ || !paging_button_attached_) return;
    grid_container_->removeView(paging_container_, false);
    paging_button_attached_ = false;
}

void LibraryTab::AttachPagingButton()
{
    EnsurePagingButton();
    DetachPagingButton();
    paging_container_->Update(game_feed::Window(filtered_count_, page_index_));
    grid_container_->addView(paging_container_);
    paging_button_attached_ = true;
}

void LibraryTab::GoToPage(size_t page)
{
    const auto target = game_feed::Window(filtered_count_, page).index;
    if (load_more_pending_ || filtered_count_ == 0 || target == page_index_) return;
    load_more_pending_ = true;
    // Rebuild after the click action has returned; its button stays alive.
    brls::Application::giveFocus(more_button_);
    const auto alive = alive_;
    const auto owner = library_owner_;
    brls::sync([this, alive, target, owner] {
        if (!alive->load() || owner != library_owner_) return;
        RunUiAction("library_tab.page", [this, target] {
            page_index_ = target;
            RebuildGrid();
            if (first_card_) brls::Application::giveFocus(first_card_);
        });
        load_more_pending_ = false;
    });
}

void LibraryTab::HandlePagingButton()
{
    if (game_feed::Window(filtered_count_, page_index_).HasNext()) GoToPage(page_index_+1);
    else ReloadLibrary(false, true);
}

void LibraryTab::LoadMoreOrRefresh()
{
    if (loading_)
        return;

    if (!AppState::Instance().HasSession())
    {
        BeginLogin();
        return;
    }

    if (game_feed::Window(filtered_count_, page_index_).HasNext()) {
        HandlePagingButton(); return;
    }

    ReloadLibrary(false, true);
}

void LibraryTab::CycleStoreFilter()
{
    MoveFocusBeforeDestroy(grid_container_, filter_button_);
    store_filter_index_ = (store_filter_index_ + 1) % kStoreFilters.size();
    page_index_ = 0;
    RebuildGrid();
    brls::Application::notify(opennow::Tr("Store filter: ") + std::string(kStoreFilters[store_filter_index_]));
}

void LibraryTab::CycleSortMode()
{
    MoveFocusBeforeDestroy(grid_container_, sort_button_);
    sort_mode_index_ = (sort_mode_index_ + 1) % kLibrarySortModes.size();
    page_index_ = 0;
    RebuildGrid();
    brls::Application::notify(opennow::Tr("Sort: ") + std::string(kLibrarySortModes[sort_mode_index_]));
}

bool LibraryTab::OpenGameDialog(size_t index)
{
    if (index >= games_.size())
        return false;

    brls::Application::pushActivity(new brls::Activity(new GameDetailView(
        client_,
        MakeLibraryGameDetail(games_[index]))));
    return true;
}

} // namespace opennow
