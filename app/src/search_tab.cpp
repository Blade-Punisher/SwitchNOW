#include "localization.hpp"
#include "game_feed_policy.hpp"
#include "search_tab.hpp"

#include "app_state.hpp"
#include "game_card_view.hpp"
#include "game_detail_view.hpp"
#include "membership_label.hpp"
#include "ui_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <unordered_set>
#include <utility>

namespace opennow
{
namespace
{

constexpr size_t kCardsPerRow = 5;
constexpr size_t kInitialVisibleResultLimit = 30;

brls::Label* MakeParagraph(const std::string& text, float bottom_margin = 16.0f)
{
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(18);
    label->setMarginBottom(bottom_margin);
    return label;
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string PrimaryStore(const GameInfo& game)
{
    if (!game.available_stores.empty())
        return game.available_stores.front();

    if (!game.publisher.empty())
        return game.publisher;

    return "GeForce NOW";
}

class InputBlocker
{
  public:
    InputBlocker()
    {
        brls::Application::blockInputs();
    }

    ~InputBlocker()
    {
        brls::Application::unblockInputs();
    }
};

} // namespace

SearchTab::SearchTab()
    : brls::Box(brls::Axis::COLUMN)
{
    setPadding(28, 40, 28, 40);

    auto* header = new brls::Header();
    header->setTitle(opennow::Tr("Search"));
    header->setSubtitle("Search your Library and the GeForce NOW Store. Y searches, X refreshes cached data.");
    addView(header);

    status_label_ = MakeParagraph(opennow::Tr("Press Y to search games. Press X to refresh Store/Library data."));
    addView(status_label_);

    auto* actions = new brls::Box(brls::Axis::ROW);
    actions->setMarginBottom(14);

    search_button_ = new brls::Button();
    search_button_->setText(opennow::Tr("Search games"));
    search_button_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    search_button_->setFocusable(false);
    search_button_->setMarginRight(12);
    search_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        OpenSearchIme();
        return true;
    });
    actions->addView(search_button_);

    refresh_button_ = new brls::Button();
    refresh_button_->setText(opennow::Tr("Refresh search data"));
    refresh_button_->setFocusable(false);
    refresh_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        RefreshData();
        return true;
    });
    actions->addView(refresh_button_);
    addView(actions);

    scrolling_frame_ = new brls::ScrollingFrame();
    scrolling_frame_->setGrow(1.0f);
    scrolling_frame_->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    results_container_ = new brls::Box(brls::Axis::COLUMN);
    results_container_->setPadding(0, 0, 30, 0);
    scrolling_frame_->setContentView(results_container_);
    addView(scrolling_frame_);

    registerAction(opennow::Tr("Search"), brls::BUTTON_Y, [this](brls::View* view) {
        (void)view;
        OpenSearchIme();
        return true;
    }, false, true);

    registerAction(opennow::Tr("Refresh"), brls::BUTTON_X, [this](brls::View* view) {
        (void)view;
        RefreshData();
        return true;
    }, false, true);
}

void SearchTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    EnsureSessionLoaded();

    const auto& state = AppState::Instance();
    if (state.HasLibraryGames())
        library_games_ = state.library_games();
    if (state.HasPublicGames())
        catalog_games_ = state.public_games();

    RebuildResults();
}

void SearchTab::EnsureSessionLoaded()
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

void SearchTab::OpenSearchIme()
{
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            search_query_ = std::move(text);
            visible_limit_ = kInitialVisibleResultLimit;
            RebuildResults();
        },
        Tr("Search Games"),
        Tr("Enter a title from Library or Store"),
        64,
        search_query_);
}

void SearchTab::RefreshData()
{
    if (loading_)
        return;

    loading_ = true;
    status_label_->setText(opennow::Tr("Refreshing Store and Library data..."));

    try
    {
        InputBlocker blocker;
        auto& state = AppState::Instance();

        catalog_games_ = client_.FetchPublicGames();
        state.SetPublicGames(catalog_games_);

        if (state.HasSession())
        {
            AuthSession session = *state.session();
            library_games_ = client_.FetchLibraryGames(session);
            state.SetSession(session);
            state.SetLibraryGames(library_games_);
        }
        else
        {
            library_games_.clear();
        }
    }
    catch (const game_feed::Deferred&)
    {
        loading_ = false;
        status_label_->setText(Tr("Library refresh paused while launching the game."));
        return;
    }
    catch (const std::exception& ex)
    {
        loading_ = false;
        ShowError(opennow::Tr("Search Refresh Failed"), ex.what());
        RebuildResults();
        return;
    }

    loading_ = false;
    RebuildResults();
    brls::Application::notify(opennow::Tr("Search data refreshed"));
}

void SearchTab::RebuildResults()
{
    const bool load_more_had_focus =
        load_more_button_ && brls::Application::getCurrentFocus() == load_more_button_;

    results_container_->clearViews();
    load_more_button_ = nullptr;
    result_count_ = 0;

    const std::string query = ToLower(search_query_);
    if (query.empty())
    {
        status_label_->setText(
            opennow::Tr("Press Y to search. Cached Library: ") + std::to_string(library_games_.size()) +
            opennow::Tr(" | Store: ") + std::to_string(catalog_games_.size()));
        results_container_->addView(MakeParagraph(
            opennow::Tr("Search combines your signed-in Library and NVIDIA's public Store catalog. Use X if data has not been loaded yet."),
            0.0f));
        return;
    }

    struct Result
    {
        bool library = false;
        size_t index = 0;
    };

    std::vector<Result> results;
    std::unordered_set<std::string> seen_titles;

    for (size_t i = 0; i < library_games_.size(); ++i)
    {
        if (ToLower(library_games_[i].title).find(query) == std::string::npos)
            continue;

        seen_titles.insert(ToLower(library_games_[i].title));
        results.push_back({true, i});
    }

    for (size_t i = 0; i < catalog_games_.size(); ++i)
    {
        const std::string title_key = ToLower(catalog_games_[i].title);
        if (title_key.find(query) == std::string::npos || seen_titles.count(title_key) > 0)
            continue;

        results.push_back({false, i});
    }

    result_count_ = results.size();
    const size_t visible_count = std::min(result_count_, visible_limit_);

    status_label_->setText(
        opennow::Tr("Query: ") + search_query_ + opennow::Tr(" | Results: ") + std::to_string(result_count_) +
        opennow::Tr(" | Showing: ") + std::to_string(visible_count) +
        opennow::Tr(" | Library: ") + std::to_string(library_games_.size()) +
        opennow::Tr(" | Store: ") + std::to_string(catalog_games_.size()));

    if (results.empty())
    {
        results_container_->addView(MakeParagraph(opennow::Tr("No matching Library or Store games found."), 0.0f));
        return;
    }

    brls::View* first_card = nullptr;
    brls::View* first_new_card = nullptr;
    const size_t previous_visible_limit =
        visible_limit_ > kInitialVisibleResultLimit ? visible_limit_ - kInitialVisibleResultLimit : 0;

    for (size_t start = 0; start < visible_count; start += kCardsPerRow)
    {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setMarginBottom(2);

        const size_t end = std::min(start + kCardsPerRow, visible_count);
        for (size_t i = start; i < end; ++i)
        {
            const Result result = results[i];
            GameCardDisplay display;

            if (result.library)
            {
                const GameInfo& game = library_games_[result.index];
                display.title = game.title;
                display.subtitle = PrimaryStore(game);
                display.badge = BuildMembershipBadge(Tr("In library"), Tr(game.membership_tier_label));
                display.image_url = game.image_url;
            }
            else
            {
                const PublicGame& game = catalog_games_[result.index];
                display.title = game.title;
                display.subtitle = game.store;
                display.badge = game.publisher.empty() ? Tr("GFN catalog") : game.publisher;
                display.image_url = game.image_url;
            }

            auto* card = new GameCardView(display, [this, result]() {
                if (result.library)
                    OpenLibraryResult(result.index);
                else
                    OpenCatalogResult(result.index);
            });

            if (!first_card)
                first_card = card;
            if (!first_new_card && i >= previous_visible_limit)
                first_new_card = card;

            row->addView(card);
        }

        results_container_->addView(row);
    }

    if (result_count_ > visible_count)
    {
        load_more_button_ = new brls::Button();
        load_more_button_->setText(
            opennow::Tr("Show more results (") + std::to_string(result_count_ - visible_count) + opennow::Tr(" left)"));
        load_more_button_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        load_more_button_->setMarginTop(14);
        load_more_button_->setMarginBottom(24);
        load_more_button_->registerClickAction([this](brls::View* view) {
            (void)view;
            LoadMoreResults();
            return true;
        });
        results_container_->addView(load_more_button_);
    }

    if (load_more_had_focus && first_new_card)
        brls::Application::giveFocus(first_new_card);
}

void SearchTab::LoadMoreResults()
{
    if (result_count_ > visible_limit_)
    {
        visible_limit_ += kInitialVisibleResultLimit;
        RebuildResults();
    }
}

bool SearchTab::OpenLibraryResult(size_t index)
{
    if (index >= library_games_.size())
        return false;

    brls::Application::pushActivity(new brls::Activity(new GameDetailView(
        client_,
        MakeLibraryGameDetail(library_games_[index]))));
    return true;
}

bool SearchTab::OpenCatalogResult(size_t index)
{
    if (index >= catalog_games_.size())
        return false;

    brls::Application::pushActivity(new brls::Activity(new GameDetailView(
        client_,
        MakeCatalogGameDetail(catalog_games_[index]))));
    return true;
}

} // namespace opennow
