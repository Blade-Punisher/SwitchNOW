#pragma once

#include "gfn_client.hpp"
#include "models.hpp"
#include "game_feed_policy.hpp"
#include "game_feed_focus.hpp"

#include <borealis.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace opennow
{
class GameFeedPager;

class CatalogTab : public brls::Box
{
  public:
    CatalogTab();
    ~CatalogTab() override;

    void willAppear(bool resetState) override;

  private:
    void ReloadCatalog(const std::string& server_query = {}, bool background = false);
    bool IsFrontmostCatalog();
    std::string catalog_owner_;
    std::chrono::steady_clock::time_point last_catalog_sync_ {};
    void BeginSearch();
    bool ClearSearch();
    void UpdateSearchUi();
    void RebuildList();
    void LoadMoreOrRefresh();
    void EnsurePagingButton();
    void DetachPagingButton();
    void AttachPagingButton(bool has_more, size_t remaining);
    void HandlePagingButton();
    void GoToPage(size_t page);
    void CycleStoreFilter();
    void CycleSortMode();
    bool OpenGameDialog(brls::View* view, size_t index);

    GameFeedFocus grid_focus_;
    GfnClient client_;
    std::vector<PublicGame> games_;
    std::vector<PublicGame> base_games_;
    std::vector<std::vector<brls::View*>> card_rows_;
    std::vector<brls::View*> toolbar_buttons_;
    brls::Label* status_label_                 = nullptr;
    brls::Label* search_context_label_          = nullptr;
    brls::Button* search_button_               = nullptr;
    brls::Button* filter_button_               = nullptr;
    brls::Button* sort_button_                 = nullptr;
    brls::Button* more_button_                 = nullptr;
    GameFeedPager* paging_container_               = nullptr;
    brls::ScrollingFrame* scrolling_frame_     = nullptr;
    brls::Box* list_container_                 = nullptr;
    brls::View* first_card_                    = nullptr;
    std::shared_ptr<game_feed::Requests> requests_ = std::make_shared<game_feed::Requests>();
    std::uint64_t feed_revision_ = game_feed::Revision();
    bool feed_complete_ = false;
    bool loading_                              = false;
    bool rebuilding_                           = false;
    bool load_more_pending_                    = false;
    bool paging_button_attached_               = false;
    std::shared_ptr<std::atomic_bool> alive_   = std::make_shared<std::atomic_bool>(true);
    std::string search_query_                  = "";
    std::uint64_t search_generation_           = 0;
    size_t filtered_count_                     = 0;
    size_t page_index_                         = 0;
    size_t store_filter_index_                 = 0;
    size_t sort_mode_index_                    = 0;
};

} // namespace opennow
