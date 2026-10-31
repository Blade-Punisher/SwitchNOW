#pragma once

#include "gfn_client.hpp"
#include "models.hpp"
#include "home_shortcut.hpp"
#include "game_autologin.hpp"

#include <borealis.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace opennow
{

class GalleryImage;

struct GameDetailData
{
    std::string title;
    std::string game_id;
    std::string subtitle;
    std::string image_url;
    std::string poster_url;
    std::string hero_url;
    std::vector<std::string> screenshots;
    std::string launch_app_id;
    std::string publisher;
    std::string developer;
    std::string description;
    std::string stores;
    std::string membership_tier_label;
    std::string last_played;
    std::vector<std::string> genres;
    std::vector<std::string> feature_labels;
    std::vector<std::string> content_ratings;
    std::vector<std::string> supported_controls;
    bool owned = false;
    std::vector<GameVariant> variants;
};

class GameDetailView : public brls::Box
{
  public:
    GameDetailView(const GfnClient& client, GameDetailData data);
    ~GameDetailView() override;
    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;
    void ShowShortcutResult(const shortcut::InstallerResult& result);

  private:
    void Play();
    void LoadDescription();
    brls::Label* description_label_ = nullptr;
    brls::Label* description_heading_ = nullptr;
    brls::ScrollingFrame* description_frame_ = nullptr;
    float description_max_height_ = 203.0f;
    std::shared_ptr<std::atomic_bool> description_cancelled_ =
        std::make_shared<std::atomic_bool>(false);
    void MarkAsOwned();
    void ConfirmUnowned();
    void SetOwnership(bool owned);
    void UpdateOwnershipButton();
    void ShowStoreSelector(bool launch_after_selection);
    void LaunchSelectedVariant();
    void CreateSwitchShortcut();
    void UpdateStoreButton();
    void OpenAutoLoginCredentialsMenu(const autologin::Profile* profile = nullptr);
    void ConfigureAutoLoginCredentials(const autologin::Profile* profile = nullptr);
    bool EpicAutoLoginAvailable() const;
    void UpdateAutoLoginButton();
    void ShowPreviousScreenshot();
    void ShowNextScreenshot();
    void UpdateScreenshot();
    std::string ActiveUserId() const;

    GfnClient client_;
    GameDetailData data_;
    brls::Button* ownership_button_ = nullptr;
    brls::Button* remove_ownership_button_ = nullptr;
    bool ownership_pending_ = false;
    brls::Button* play_button_ = nullptr;
    brls::Button* store_button_ = nullptr;
    brls::Button* autologin_button_ = nullptr;
    GalleryImage* screenshot_image_ = nullptr;
    brls::Label* screenshot_status_ = nullptr;
    std::vector<brls::Label*> screenshot_dots_;
    std::vector<std::string> media_urls_;
    std::shared_ptr<std::atomic_uint64_t> screenshot_generation_ =
        std::make_shared<std::atomic_uint64_t>(0);
    std::shared_ptr<std::atomic_bool> alive_ =
        std::make_shared<std::atomic_bool>(true);
    size_t screenshot_index_ = 0;
    int screenshot_direction_ = 1;
    bool gallery_paused_ = false;
    size_t selected_variant_index_ = 0;
    bool launcher_preference_loaded_ = false;
};

void SaveShortcutReturnContext(const GameDetailData& data);
bool RestoreShortcutGameDetail(const shortcut::InstallerResult& result);

GameDetailData MakeLibraryGameDetail(const GameInfo& game);
GameDetailData MakeCatalogGameDetail(const PublicGame& game);

} // namespace opennow
