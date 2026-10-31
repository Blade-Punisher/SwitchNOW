#include "catalog_tab.hpp"

#include "app_state.hpp"
#include "game_detail_view.hpp"
#include "game_card_view.hpp"
#include "game_grid_navigation.hpp"
#include "game_feed_pager.hpp"
#include "ui_action_guard.hpp"
#include "ui_helpers.hpp"
#include "localization.hpp"
#include "http_cancellation.hpp"
#include "network_resource_policy.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace opennow
{
namespace
{

constexpr size_t kInitialVisibleGameLimit = game_feed::kPageSize;
constexpr size_t kCardsPerRow      = 5;
constexpr std::array<const char*, 6> kStoreFilters = {"All", "Steam", "Epic", "Ubisoft", "Xbox", "Battle.net"};
constexpr std::array<const char*, 3> kCatalogSortModes = {"A-Z", "Store", "Publisher"};

brls::Label* MakeParagraph(const std::string& text, float bottom_margin = 16.0f)
{
    auto* label = new brls::Label();
    label->setText(Tr(text));
    label->setFontSize(18);
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

bool MatchesStoreFilter(const PublicGame& game, size_t filter_index)
{
    if (filter_index == 0 || filter_index >= kStoreFilters.size())
        return true;

    const std::string filter = kStoreFilters[filter_index];
    if (ContainsText(game.store, filter) || ContainsText(game.publisher, filter))
        return true;

    if (filter == "Battle.net")
        return ContainsText(game.store, "battle") || ContainsText(game.publisher, "blizzard");

    return false;
}

} // namespace

CatalogTab::CatalogTab()
    : brls::Box(brls::Axis::COLUMN)
{
    setPadding(18, 32, 18, 32);
    setBackgroundColor(nvgRGB(11, 12, 15));

    auto* heading = new brls::Box(brls::Axis::ROW);
    heading->setAlignItems(brls::AlignItems::CENTER);
    heading->setMarginBottom(10);
    auto* accent = new brls::Rectangle();
    accent->setWidth(4);
    accent->setHeight(30);
    accent->setMarginRight(12);
    accent->setColor(nvgRGB(92, 238, 139));
    heading->addView(accent);
    auto* title = MakeParagraph(opennow::Tr("Store"), 0.0f);
    title->setFontSize(28);
    title->setTextColor(nvgRGB(248, 249, 251));
    heading->addView(title);
    addView(heading);

    auto* toolbar = new brls::Box(brls::Axis::ROW);
    toolbar->setAlignItems(brls::AlignItems::CENTER);
    toolbar->setMarginBottom(8);
    search_button_ = MakeToolbarButton("Y  " + Tr("Search"));
    filter_button_ = MakeToolbarButton("ZL  " + Tr("All stores"));
    sort_button_   = MakeToolbarButton("ZR  " + Tr("A-Z"));
    more_button_   = MakeToolbarButton("X  " + Tr("More / Refresh"));
    search_button_->setStyle(&brls::BUTTONSTYLE_HIGHLIGHT);
    toolbar_buttons_ = {search_button_, filter_button_, sort_button_, more_button_};
    for (brls::View* button : toolbar_buttons_)
        toolbar->addView(button);
    addView(toolbar);

    search_button_->registerClickAction([this](brls::View*) {
        return RunUiAction("catalog.search.button", [this]() { BeginSearch(); });
    });
    filter_button_->registerClickAction([this](brls::View*) {
        return RunUiAction("catalog.filter.button", [this]() { CycleStoreFilter(); });
    });
    sort_button_->registerClickAction([this](brls::View*) {
        return RunUiAction("catalog.sort.button", [this]() { CycleSortMode(); });
    });
    more_button_->registerClickAction([this](brls::View*) {
        return RunUiAction("catalog.more.button", [this]() { LoadMoreOrRefresh(); });
    });

    search_context_label_ = MakeParagraph("", 5.0f);
    search_context_label_->setFontSize(16.0f);
    search_context_label_->setTextColor(nvgRGB(88, 230, 146));
    search_context_label_->setVisibility(brls::Visibility::GONE);
    addView(search_context_label_);

    status_label_ = MakeParagraph(opennow::Tr("Loading the supported GeForce NOW catalog..."), 8.0f);
    status_label_->setFontSize(14);
    status_label_->setTextColor(nvgRGB(132, 139, 151));
    addView(status_label_);

    scrolling_frame_ = new brls::ScrollingFrame();
    scrolling_frame_->setGrow(1.0f);
    scrolling_frame_->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    list_container_ = new brls::Box(brls::Axis::COLUMN);
    list_container_->setPadding(0, 0, 32, 0);
    scrolling_frame_->setContentView(list_container_);

    addView(scrolling_frame_);

    registerAction(opennow::Tr("Search"), brls::BUTTON_Y, [this](brls::View* view) {
        (void)view;
        return RunUiAction("catalog.search.hotkey", [this]() { BeginSearch(); });
    }, false, false);

    registerAction(opennow::Tr("More / Refresh"), brls::BUTTON_X, [this](brls::View* view) {
        (void)view;
        return RunUiAction("catalog.more.hotkey", [this]() { LoadMoreOrRefresh(); });
    }, false, false);

    registerAction(opennow::Tr("Store Filter"), brls::BUTTON_LT, [this](brls::View* view) {
        (void)view;
        return RunUiAction("catalog.filter.hotkey", [this]() { CycleStoreFilter(); });
    }, false, false);

    registerAction(opennow::Tr("Sort"), brls::BUTTON_RT, [this](brls::View* view) {
        (void)view;
        return RunUiAction("catalog.sort.hotkey", [this]() { CycleSortMode(); });
    }, false, false);

    registerAction(Tr("Clear search"), brls::BUTTON_B, [this](brls::View*) {
        if (search_query_.empty())
            return false;
        return RunUiAction("catalog.search.clear", [this]() { ClearSearch(); });
    }, false, false);
}

CatalogTab::~CatalogTab()
{
    alive_->store(false);
    if (paging_container_ && !paging_button_attached_) delete paging_container_;
    LogUiAction("catalog", "destroy");
}

void CatalogTab::BeginSearch()
{
    const auto alive = alive_;
    brls::Application::giveFocus(search_button_);
    brls::Application::getImeManager()->openForText(
        [this, alive](std::string text) {
            if (!alive->load())
                return;
            RunUiAction("catalog.search.result", [this, text = std::move(text)]() mutable {
                MoveFocusBeforeDestroy(list_container_, search_button_);
                search_query_ = std::move(text);
                ++search_generation_;
                page_index_ = 0;
                UpdateSearchUi();
                if (search_query_.empty() && !base_games_.empty())
                {
                    games_ = base_games_;
                    RebuildList();
                }
                else if (!search_query_.empty() && AppState::Instance().HasSession())
                {
                    ReloadCatalog(search_query_);
                }
                else
                {
                    RebuildList();
                }
            });
        },
        Tr("Search Catalog"), Tr("Enter a title to filter games"), 64, search_query_);
}

bool CatalogTab::ClearSearch()
{
    if (search_query_.empty())
        return false;

    MoveFocusBeforeDestroy(list_container_, search_button_);
    search_query_.clear();
    ++search_generation_;
    page_index_ = 0;
    if (!base_games_.empty())
        games_ = base_games_;
    UpdateSearchUi();
    RebuildList();
    brls::Application::giveFocus(search_button_);
    return true;
}

void CatalogTab::UpdateSearchUi()
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

bool CatalogTab::IsFrontmostCatalog()
{
    const auto stack = brls::Application::getActivitiesStack();
    return !stack.empty() && stack.back() == getParentActivity();
}

void CatalogTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    const auto& state = AppState::Instance();
    if(feed_revision_!=game_feed::Revision()){
        feed_revision_=game_feed::Revision();requests_->Invalidate();loading_=false;last_catalog_sync_={};
        games_=AppState::Instance().public_games();base_games_=games_;
    }

    const auto owner = state.HasSession() ? game_feed::OwnerKey(state.session()->user.user_id,
        state.session()->provider.code, state.session()->provider.streaming_service_url) : std::string("public");
    if (owner != catalog_owner_) {
        catalog_owner_ = owner; requests_->Invalidate(); loading_ = false;
        games_.clear(); base_games_.clear(); feed_complete_ = false; last_catalog_sync_ = {}; page_index_ = 0; RebuildList();
    }
    UpdateSearchUi();
    if (games_.empty() && state.HasPublicGames()) {
        games_ = state.public_games(); base_games_ = games_; RebuildList();
    }
    else if (!games_.empty() || !card_rows_.empty()) {
        // A covered Store can receive new data while its old card views stay
        // alive. Rebind their indices before accepting clicks on return.
        RebuildList();
    }
    if (!loading_ && (last_catalog_sync_ == std::chrono::steady_clock::time_point{} ||
        std::chrono::steady_clock::now() - last_catalog_sync_ >= std::chrono::minutes(2)))
        ReloadCatalog(search_query_, true);
}

void CatalogTab::ReloadCatalog(const std::string& server_query, bool background)
{
    if (loading_) return;
    loading_ = true;
    feed_complete_ = false;
    if (games_.empty()) RebuildList();
    const auto request_generation = search_generation_;
    const auto requests = requests_;
    const auto ticket = requests->Begin();
    const auto alive = alive_;
    const auto revision=game_feed::Revision();
    auto valid = [alive, requests, ticket, revision] { return alive->load() && requests->Current(ticket) && revision==game_feed::Revision(); };
    const auto owner = catalog_owner_;
    auto same_account = [owner] {
        const auto& state = AppState::Instance();
        const auto current = state.HasSession() ? game_feed::OwnerKey(state.session()->user.user_id,
            state.session()->provider.code, state.session()->provider.streaming_service_url) : std::string("public");
        return current == owner;
    };
    status_label_->setText(Tr(server_query.empty() ? "Loading current GeForce NOW catalog..." :
        "Searching the GeForce NOW catalog..."));
    GfnClient client = client_;
    const bool authenticated = AppState::Instance().HasSession();
    AuthSession session;
    if (authenticated) session = *AppState::Instance().session();
    brls::async([this, valid, same_account, client, authenticated, session = std::move(session),
                 server_query, request_generation, background]() mutable {
        auto finish_stale = [this, valid, same_account, request_generation] {
            if (!valid()) return true;
            if (!same_account()) { loading_ = false; return true; }
            if (request_generation != search_generation_) {
                loading_ = false;
                ReloadCatalog(search_query_);
                return true;
            }
            return false;
        };
        try {
            HttpCancellationScope cancellation([valid] { return !valid(); });
            auto preview = [this, valid, same_account, server_query, request_generation](std::vector<PublicGame> games, bool full) {
                brls::sync([this, valid, same_account, server_query, request_generation, games = std::move(games), full]() mutable {
                    if (!valid() || !same_account() || request_generation != search_generation_) return;
                    AppState::Instance().ApplyConfirmedOwnership(games);
                    games_ = std::move(games);
                    if (server_query.empty() && full) {
                        base_games_ = games_; AppState::Instance().SetPublicGames(games_);games_=AppState::Instance().public_games();
                    }
                    if (IsFrontmostCatalog() && network::launch_count.load() == 0) {
                        RebuildList();
                        status_label_->setText(Tr(full ? "Showing saved games. Checking for updates..." :
                            "First games loaded. Loading the remaining games..."));
                    }
                });
            };
            auto games = client.FetchStoreGames(authenticated ? &session : nullptr, server_query, preview, !background);
            brls::sync([this, finish_stale, authenticated, games = std::move(games), session = std::move(session), server_query, background]() mutable {
                if (finish_stale()) return;
                loading_ = false; feed_complete_ = true; last_catalog_sync_ = std::chrono::steady_clock::now();
                AppState::Instance().ApplyConfirmedOwnership(games);
                games_ = std::move(games);
                if (authenticated) AppState::Instance().SetSession(std::move(session));
                if (server_query.empty()) { base_games_ = games_; AppState::Instance().SetPublicGames(games_); }
                if (!IsFrontmostCatalog() || network::launch_count.load() != 0) return;
                RebuildList();
                if (!background) brls::Application::notify(Tr(server_query.empty() ? "Store catalog refreshed" : "Catalog search completed"));
            });
        } catch (const game_feed::Deferred&) {
            brls::sync([this, finish_stale] {
                if (finish_stale()) return;
                loading_ = false; last_catalog_sync_ = {};
                if (IsFrontmostCatalog() && games_.empty()) RebuildList();
                if (IsFrontmostCatalog()) status_label_->setText(Tr("Game list refresh paused while launching the game."));
            });
        } catch (const std::exception& ex) {
            const std::string error = ex.what();
            brls::sync([this, finish_stale, error, background] {
                if (finish_stale()) return;
                loading_ = false;
                if (IsFrontmostCatalog() && games_.empty()) RebuildList();
                if (game_feed::ShowFailure(false, background, IsFrontmostCatalog() && network::launch_count.load() == 0, true))
                    ShowError(opennow::Tr("Catalog Request Failed"), error);
                if (IsFrontmostCatalog()) status_label_->setText(Tr("Catalog request failed. Press X to retry."));
            });
        }
    }, false);
}

void CatalogTab::RebuildList()
{
    if (rebuilding_)
        return;
    rebuilding_ = true;
    struct ResetFlag { bool& flag; ~ResetFlag() { flag = false; } } reset {rebuilding_};
    const auto focused_game = grid_focus_.Capture();

    brls::View* current_focus = brls::Application::getCurrentFocus();
    const bool paging_had_focus = paging_container_ && paging_container_->Owns(current_focus);

    MoveFocusBeforeDestroy(list_container_, search_button_);
    DetachPagingButton();
    list_container_->clearViews();
    card_rows_.clear();
    grid_focus_.Clear();
    first_card_ = nullptr;
    load_more_pending_ = false;

    std::vector<size_t> filtered_indices;
    std::string lower_query = ToLower(search_query_);

    for (size_t i = 0; i < games_.size(); ++i)
    {
        const PublicGame& game = games_[i];
        const bool matches_query = lower_query.empty() || ToLower(game.title).find(lower_query) != std::string::npos;
        if (matches_query && MatchesStoreFilter(game, store_filter_index_))
        {
            filtered_indices.push_back(i);
        }
    }

    std::sort(filtered_indices.begin(), filtered_indices.end(), [this](size_t left_index, size_t right_index) {
        const PublicGame& left = games_[left_index];
        const PublicGame& right = games_[right_index];

        if (sort_mode_index_ == 1 && left.store != right.store)
            return left.store < right.store;

        if (sort_mode_index_ == 2 && left.publisher != right.publisher)
            return left.publisher < right.publisher;

        return left.title < right.title;
    });

    filtered_count_ = filtered_indices.size();
    const auto page = game_feed::Window(filtered_count_, page_index_);
    page_index_ = page.index;
    const size_t total_pages = page.count, page_start = page.start, page_end = page.end;
    std::string status = TrFormat(
        "Loaded {0} supported games.", {std::to_string(games_.size())});
    status += " " + TrFormat(
        "Filter: {0}.", {Tr(kStoreFilters[store_filter_index_])});
    status += " " + TrFormat(
        "Sort: {0}.", {Tr(kCatalogSortModes[sort_mode_index_])});
    if (!search_query_.empty())
    {
        status += " " + TrFormat("Searching for: {0}", {search_query_});
        status += " " + TrFormat(
            "Found {0} matches.", {std::to_string(filtered_count_)});
    }
    if (filtered_count_ > 0)
        status += " " + TrFormat(
            "Showing {0}-{1} of {2} (page {3}/{4}).",
            {std::to_string(page_start + 1), std::to_string(page_end),
             std::to_string(filtered_count_), std::to_string(page_index_ + 1),
             std::to_string(total_pages)});
    else
        status += " " + Tr("Press X to refresh.");

    status_label_->setText(status);
    filter_button_->setText("ZL  " + Tr(kStoreFilters[store_filter_index_]));
    sort_button_->setText("ZR  " + Tr(kCatalogSortModes[sort_mode_index_]));

    if (filtered_indices.empty())
    {
        list_container_->addView(MakeParagraph(EmptyFeedMessage(loading_, !games_.empty(), feed_complete_), 0.0f));
        WireVerticalGridNavigation({toolbar_buttons_});
        return;
    }

    for (size_t start = page_start; start < page_end; start += kCardsPerRow)
    {
        auto* row = new brls::Box(brls::Axis::ROW);
        std::vector<brls::View*> card_row;
        const size_t end = std::min(start + kCardsPerRow, page_end);

        for (size_t i = start; i < end; ++i)
        {
            const size_t index = filtered_indices[i];
            const PublicGame& game = games_[index];

            GameCardDisplay display;
            display.title     = game.title;
            display.subtitle  = game.store;
            display.badge     = game.is_in_library
                ? Tr("In library")
                : (game.publisher.empty() ? Tr("GFN catalog") : game.publisher);
            display.image_url = game.image_url;

            auto* card = new GameCardView(display, [this, index]() {
                OpenGameDialog(nullptr, index);
            });

            if (!first_card_)
                first_card_ = card;

            grid_focus_.Remember(card, game.id);
            row->addView(card);
            card_row.push_back(card);
        }

        list_container_->addView(row);
        card_rows_.push_back(std::move(card_row));
    }

    std::vector<std::vector<brls::View*>> navigation_rows {toolbar_buttons_};
    navigation_rows.insert(navigation_rows.end(), card_rows_.begin(), card_rows_.end());
    AttachPagingButton(page_end < filtered_count_, filtered_count_ - page_end);
    navigation_rows.push_back(paging_container_->Buttons());
    WireVerticalGridNavigation(navigation_rows);
    if (!card_rows_.empty())
        for (auto* card : card_rows_.back())
            card->setCustomNavigationRoute(brls::FocusDirection::DOWN, paging_container_->NextTarget());

    if (grid_focus_.Restore(focused_game)) return;
    if (paging_had_focus && paging_button_attached_)
        brls::Application::giveFocus(paging_container_->NextTarget());
    else if (first_card_ && !brls::Application::getCurrentFocus())
        brls::Application::giveFocus(first_card_);
}

void CatalogTab::EnsurePagingButton()
{
    if (!paging_container_)
        paging_container_ = new GameFeedPager([this, alive=alive_](size_t page) { if (alive->load()) GoToPage(page); });
}

void CatalogTab::DetachPagingButton()
{
    if (!paging_container_ || !paging_button_attached_) return;
    list_container_->removeView(paging_container_, false);
    paging_button_attached_ = false;
}

void CatalogTab::AttachPagingButton(bool has_more, size_t remaining)
{
    (void)has_more; (void)remaining;
    EnsurePagingButton();
    DetachPagingButton();
    paging_container_->Update(game_feed::Window(filtered_count_, page_index_));
    list_container_->addView(paging_container_);
    paging_button_attached_ = true;
}

void CatalogTab::GoToPage(size_t page)
{
    const auto target = game_feed::Window(filtered_count_, page).index;
    if (load_more_pending_ || filtered_count_ == 0 || target == page_index_) return;
    load_more_pending_ = true;
    // Rebuild after the click action has returned; its button stays alive.
    brls::Application::giveFocus(more_button_);
    const auto alive = alive_;
    const auto owner = catalog_owner_;
    brls::sync([this, alive, target, owner] {
        if (!alive->load() || owner != catalog_owner_) return;
        RunUiAction("catalog_tab.page", [this, target] {
            page_index_ = target;
            RebuildList();
            if (first_card_) brls::Application::giveFocus(first_card_);
        });
        load_more_pending_ = false;
    });
}

void CatalogTab::HandlePagingButton()
{
    if (game_feed::Window(filtered_count_, page_index_).HasNext()) GoToPage(page_index_+1);
    else ReloadCatalog();
}

void CatalogTab::LoadMoreOrRefresh()
{
    if (loading_)
        return;

    if ((page_index_ + 1) * kInitialVisibleGameLimit < filtered_count_)
    {
        HandlePagingButton();
        return;
    }

    ReloadCatalog();
}

void CatalogTab::CycleStoreFilter()
{
    MoveFocusBeforeDestroy(list_container_, filter_button_);
    store_filter_index_ = (store_filter_index_ + 1) % kStoreFilters.size();
    page_index_         = 0;
    RebuildList();
    brls::Application::notify(opennow::Tr("Store filter: ") + std::string(kStoreFilters[store_filter_index_]));
}

void CatalogTab::CycleSortMode()
{
    MoveFocusBeforeDestroy(list_container_, sort_button_);
    sort_mode_index_ = (sort_mode_index_ + 1) % kCatalogSortModes.size();
    page_index_      = 0;
    RebuildList();
    brls::Application::notify(opennow::Tr("Sort: ") + std::string(kCatalogSortModes[sort_mode_index_]));
}

bool CatalogTab::OpenGameDialog(brls::View* view, size_t index)
{
    (void)view;

    if (index >= games_.size())
        return false;

    brls::Application::pushActivity(new brls::Activity(new GameDetailView(
        client_,
        MakeCatalogGameDetail(games_[index]))));
    return true;
}

} // namespace opennow
