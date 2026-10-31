#pragma once

#include "gfn_client.hpp"
#include "models.hpp"

#include <borealis.hpp>

#include <functional>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace opennow
{

class ProvidersTab : public brls::Box
{
  public:
    explicit ProvidersTab(std::function<void()> on_success = {});
    ~ProvidersTab() override;

    void willAppear(bool resetState) override;

  private:
    void ReloadProviders();
    void RebuildList();
    void ShowListMessage(const std::string& title, const std::string& detail);
    void SetStatus(const std::string& title, const std::string& detail, bool error = false);
    bool ClosePicker();
    bool OpenProviderDialog(brls::View* view, size_t index);

    GfnClient client_;
    std::vector<LoginProvider> providers_;
    brls::Label* status_label_             = nullptr;
    brls::Label* status_detail_label_      = nullptr;
    brls::Button* refresh_button_          = nullptr;
    brls::ScrollingFrame* scrolling_frame_ = nullptr;
    brls::Box* list_container_             = nullptr;
    brls::View* first_provider_card_        = nullptr;
    std::function<void()> on_success_;
    std::shared_ptr<std::atomic_bool> alive_ =
        std::make_shared<std::atomic_bool>(true);
    bool loading_                          = false;
    bool closing_                          = false;
};

} // namespace opennow
