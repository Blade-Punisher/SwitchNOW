#pragma once

#include "gfn_client.hpp"
#include "models.hpp"
#include "game_feed_policy.hpp"
#include "game_feed_focus.hpp"
#include "library_refresh_policy.hpp"

#include <borealis.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

namespace opennow
{
class GameFeedPager;

class LibraryTab : public brls::Box
{
  public:
    LibraryTab();
    ~LibraryTab() override;

    void willAppear(bool resetState) override;

  private:
    void EnsureSessionLoaded();
    void ScheduleRefreshMaintenance();
    void MaintainRefresh();
    void UpdateSessionUi();
    void UpdateResumeCard();
    std::vector<std::vector<brls::View*>> NavigationHeaderRows() const;
    void BeginLogin();
    void Logout();
    void ReloadLibrary(bool background = false, bool force_full = false);
    bool IsFrontmostLibrary();
    void BeginSearch();
    bool ClearSearch();
    void UpdateSearchUi();
    void RebuildGrid();
    void BuildPage();
    void EnsurePagingButton();
    void DetachPagingButton();
    void AttachPagingButton();
    void HandlePagingButton();
    void GoToPage(size_t page);
    void LoadMoreOrRefresh();
    void CycleStoreFilter();
    void CycleSortMode();
    bool OpenGameDialog(size_t index);

    GameFeedFocus grid_focus_;
    GfnClient client_;
    std::vector<GameInfo> games_;
    std::vector<std::vector<brls::View*>> card_rows_;
    std::vector<brls::View*> account_buttons_;
    std::vector<brls::View*> toolbar_buttons_;
    brls::Label* account_label_         = nullptr;
    brls::Box* resume_card_ = nullptr;
    brls::Button* resume_button_ = nullptr;
    brls::Label* resume_detail_ = nullptr;
    brls::Label* status_label_          = nullptr;
    brls::Label* search_context_label_  = nullptr;
    brls::Button* search_button_        = nullptr;
    brls::Button* filter_button_        = nullptr;
    brls::Button* sort_button_          = nullptr;
    brls::Button* more_button_          = nullptr;
    brls::Button* sign_in_button_       = nullptr;
    brls::Button* refresh_button_       = nullptr;
    brls::Button* logout_button_        = nullptr;
    brls::ScrollingFrame* scrolling_frame_ = nullptr;
    brls::Box* grid_container_          = nullptr;
    brls::View* first_card_             = nullptr;
    std::shared_ptr<game_feed::Requests> requests_ = std::make_shared<game_feed::Requests>();
    std::uint64_t feed_revision_ = game_feed::Revision();
    bool feed_complete_ = false;
    bool loading_                       = false;
    bool refresh_ui_dirty_ = false;
    game_feed::RefreshSchedule refresh_schedule_;
    std::string library_owner_;
    bool library_sync_complete_         = true;
    bool rebuilding_                    = false;
    bool load_more_pending_ = false;
    bool paging_button_attached_ = false;
    GameFeedPager* paging_container_ = nullptr;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
    std::string search_query_           = "";
    size_t page_index_                  = 0;
    size_t filtered_count_              = 0;
    size_t store_filter_index_          = 0;
    size_t sort_mode_index_             = 0;
    std::chrono::steady_clock::time_point last_library_sync_ {};
};

} // namespace opennow
