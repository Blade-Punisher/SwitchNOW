#pragma once

#include "gfn_client.hpp"

#include <borealis.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "stream_settings.hpp"

namespace opennow
{

class SettingsTab : public brls::Box
{
  public:
    SettingsTab();
    ~SettingsTab() override;

    void willAppear(bool resetState) override;

  private:
    enum class Category
    {
        Account,
        Stream,
        Game,
        Controls,
        Audio,
        Storage,
        Interface,
    };

    void EnsureSessionLoaded();
    void RefreshSummary();
    void SelectCategory(Category category);
    void RebuildCategory();
    void ScheduleCategoryRebuild();
    void BuildAccountPage();
    void BuildStreamPage();
    void BuildGamePage();
    void BuildControlsPage();
    void BuildAudioPage();
    void BuildStoragePage();
    void BuildInterfacePage();
    void UpdateCategoryChrome();
    void UpdateOptionValues();
    void MarkDirty();
    bool SaveChanges(brls::View* view);
    bool RevertChanges(brls::View* view);
    bool CycleResolution(brls::View* view);
    bool CycleFrameRate(brls::View* view);
    bool CycleBitrate(brls::View* view);
    bool CycleImageQuality(brls::View* view);
    bool ChooseServerLocation(brls::View* view);
    bool RefreshServerLocations(brls::View* view);
    void BeginServerLocationLoad(bool open_when_ready, bool notify_result);
    std::string ServerLocationValue() const;

    brls::Box* MakeSection(const std::string& title, const std::string& subtitle = {});
    brls::Box* MakeOptionRow(
        const std::string& title,
        const std::string& description,
        std::function<std::string()> value,
        std::function<bool(brls::View*)> action);
    brls::Box* MakeActionRow(
        const std::string& title,
        const std::string& description,
        const std::string& button_text,
        std::function<bool(brls::View*)> action,
        bool destructive = false);
    brls::Label* AddInfoLine(
        brls::Box* parent,
        const std::string& label,
        const std::string& value);

    bool ShowSessionDialog(brls::View* view);
    bool TestTokenRefresh(brls::View* view);
    bool ClearSavedLogin(brls::View* view);
    bool SwitchSavedAccount(brls::View* view);
    bool ClearAllSavedLogins(brls::View* view);
    bool ClearSavedPasswords(brls::View* view);
    bool ShowCacheState(brls::View* view);
    bool ClearCoverCache(brls::View* view);
    bool CycleStreamPreset(brls::View* view);
    bool ResetStreamPreset(brls::View* view);
    bool CycleVideoBackend(brls::View* view);
    bool ChooseGameLanguage(brls::View* view);
    bool TogglePersistGameSettings(brls::View* view);
    bool ToggleControllerLayout(brls::View* view);
    bool ToggleAudio(brls::View* view);
    bool CycleAudioVolume(brls::View* view);
    bool CycleAudioBuffer(brls::View* view);
    bool ToggleDebugDiagnostics(brls::View* view);
    bool ToggleStatsOverlay(brls::View* view);
    bool ToggleProxy(brls::View* view);
    bool EditProxy(brls::View* view, int field);
    bool ShowDiagnostics(brls::View* view);
    bool ChooseInterfaceLanguage(brls::View* view);
    bool CheckForUpdates(brls::View* view);

    GfnClient client_;
    brls::Label* title_label_ = nullptr;
    brls::Label* hint_label_ = nullptr;
    brls::Label* nav_label_ = nullptr;
    brls::Label* nav_hint_ = nullptr;
    brls::Label* page_title_ = nullptr;
    brls::Label* page_subtitle_ = nullptr;
    brls::Label* save_status_ = nullptr;
    brls::ScrollingFrame* scrolling_frame_ = nullptr;
    brls::Box* content_container_ = nullptr;
    std::vector<brls::Button*> category_buttons_;
    std::vector<std::pair<brls::Button*, std::function<std::string()>>> option_values_;
    Category category_ = Category::Stream;
    StreamSettings saved_settings_;
    StreamSettings draft_settings_;
    bool settings_loaded_ = false;
    bool dirty_ = false;
    bool rebuild_scheduled_ = false;
    std::vector<StreamRegion> server_locations_;
    bool server_locations_loaded_ = false;
    bool server_locations_loading_ = false;
    bool open_server_location_when_ready_ = false;
    std::string server_locations_error_;
    std::shared_ptr<std::atomic_bool> alive_ =
        std::make_shared<std::atomic_bool>(true);
};

} // namespace opennow
