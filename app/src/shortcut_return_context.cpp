#include "game_detail_view.hpp"
#include "atomic_file_replace.hpp"
#include "cover_image_cache.hpp"
#include "localization.hpp"
#include "runtime_journal.hpp"
#include <jansson.h>
#include <memory>
#include <sys/stat.h>
#include <cstdio>
#ifdef __SWITCH__
#include <borealis/platforms/switch/switch_video.hpp>
#include <stb_image_write.h>
#endif
namespace opennow {
namespace {
constexpr const char* kContext = "sdmc:/switch/SwitchNOW/.shortcut/game_context.json";
std::string Text(json_t* root, const char* key) {
    auto* value = json_object_get(root, key);
    return json_is_string(value) ? json_string_value(value) : "";
}
}
// Called before presentation on the UI thread, before opening the dialog. Read back only
// for this explicit action; never add a readback or queue wait to normal frames.
static bool SaveGamePageSnapshot() {
    constexpr const char* path = "sdmc:/switch/SwitchNOW/.shortcut/game_page.png";
    std::remove(path); // Never reuse another game's background after a failure.
#ifdef __SWITCH__
    auto* video = static_cast<brls::SwitchVideoContext*>(
        brls::Application::getPlatform()->getVideoContext());
    const auto width = brls::Application::windowWidth;
    const auto height = brls::Application::windowHeight;
    if (!video || !width || !height || width > 1920 || height > 1080) return false;
    auto device = video->getDeko3dDevice();
    auto queue = video->getQueue();
    const size_t size = (size_t(width) * height * 4 + DK_MEMBLOCK_ALIGNMENT - 1)
        & ~(size_t(DK_MEMBLOCK_ALIGNMENT) - 1);
    auto pixels = dk::MemBlockMaker{device, static_cast<uint32_t>(size)}.setFlags(
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached).create();
    auto commands = dk::MemBlockMaker{device, DK_MEMBLOCK_ALIGNMENT}.setFlags(
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached).create();
    auto copy = dk::CmdBufMaker{device}.create();
    if (!pixels || !commands || !copy) return false;
    queue.waitIdle();
    copy.addMemory(commands, 0, DK_MEMBLOCK_ALIGNMENT);
    copy.barrier(DkBarrier_Full, 0);
    copy.copyImageToBuffer(dk::ImageView{*video->getFramebuffer()},
        DkImageRect{0, 0, 0, width, height, 1},
        DkCopyBuf{pixels.getGpuAddr(), width * 4, 0});
    queue.submitCommands(copy.finishList());
    queue.waitIdle(); // CPU must not encode or release memory before copy completes.
    const bool saved = stbi_write_png(path, width, height, 4, pixels.getCpuAddr(), width * 4) != 0;
    LogRuntimeEvent("shortcut", "page_snapshot", saved ? "saved" : "write_failed");
    return saved;
#else
    return false;
#endif
}
void SaveShortcutReturnContext(const GameDetailData& data) {
    mkdir("sdmc:/switch/SwitchNOW/.shortcut", 0777);
    std::unique_ptr<json_t, decltype(&json_decref)> root(json_object(), json_decref);
    auto put = [&](const char* name, const std::string& value) {
        json_object_set_new(root.get(), name, json_string(value.c_str()));
    };
    if (SaveGamePageSnapshot())
        put("preview_background", "sdmc:/switch/SwitchNOW/.shortcut/game_page.png");
    else
        LogRuntimeEvent("shortcut", "page_snapshot_unavailable");
    put("title", data.title); put("game_id", data.game_id);
    put("launch_app_id", data.launch_app_id); put("stores", data.stores);
    put("subtitle", data.subtitle); put("image_url", data.image_url);
    put("poster_url", data.poster_url); put("hero_url", data.hero_url);
    put("publisher", data.publisher); put("developer", data.developer);
    put("description", data.description); put("language", GetInterfaceLanguage());
    put("preview_cover", CachedImageFilePath(data.image_url));
    json_object_set_new(root.get(), "owned", json_boolean(data.owned));
    auto* screenshots = json_array();
    auto* preview = json_array();
    for (const auto& url : data.screenshots) {
        json_array_append_new(screenshots, json_string(url.c_str()));
        if (json_array_size(preview) < 2)
            json_array_append_new(preview,
                json_string(CachedImageFilePath(ResizeNvidiaImageUrl(url, 1120)).c_str()));
    }
    json_object_set_new(root.get(), "screenshots", screenshots);
    json_object_set_new(root.get(), "preview_screenshots", preview);
    auto* variants = json_array();
    for (const auto& variant : data.variants)
        json_array_append_new(variants, json_pack("{s:s,s:s}",
            "id", variant.id.c_str(), "store", variant.store.c_str()));
    json_object_set_new(root.get(), "variants", variants);
    const std::string temporary = std::string(kContext) + ".tmp";
    if (json_dump_file(root.get(), temporary.c_str(), JSON_INDENT(2)) != 0 ||
        !storage::ReplaceWithTemporaryFile(temporary, kContext))
        LogRuntimeEvent("shortcut", "save_return_context_failed");
}
bool RestoreShortcutGameDetail(const shortcut::InstallerResult& result) {
    // The installer has exited; neither the restored live card nor its result
    // dialog needs the temporary background anymore, including failed returns.
    std::remove("sdmc:/switch/SwitchNOW/.shortcut/game_page.png");
    json_error_t error {};
    std::unique_ptr<json_t, decltype(&json_decref)> root(
        json_load_file(kContext, JSON_REJECT_DUPLICATES, &error), json_decref);
    if (!root || !json_is_object(root.get())) return false;
    GameDetailData data;
    data.title = Text(root.get(), "title");
    data.launch_app_id = Text(root.get(), "launch_app_id");
    if (data.title.empty() || !shortcut::IsNumericLaunchId(data.launch_app_id) ||
        (!result.title.empty() && result.title != data.title)) return false;
    data.game_id = Text(root.get(), "game_id"); data.stores = Text(root.get(), "stores");
    data.subtitle = Text(root.get(), "subtitle"); data.image_url = Text(root.get(), "image_url");
    data.poster_url = Text(root.get(), "poster_url"); data.hero_url = Text(root.get(), "hero_url");
    data.description = Text(root.get(), "description");
    data.publisher = Text(root.get(), "publisher"); data.developer = Text(root.get(), "developer");
    data.owned = json_is_true(json_object_get(root.get(), "owned"));
    size_t index; json_t* item;
    json_array_foreach(json_object_get(root.get(), "screenshots"), index, item)
        if (json_is_string(item)) data.screenshots.emplace_back(json_string_value(item));
    json_array_foreach(json_object_get(root.get(), "variants"), index, item) {
        GameVariant variant;
        variant.id = Text(item, "id"); variant.store = Text(item, "store");
        data.variants.push_back(std::move(variant));
    }
    auto* view = new GameDetailView(GfnClient(), std::move(data));
    brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
    // This queue executes on the UI thread before any subsequent input; the
    // new detail activity owns the view throughout the overlay's lifetime.
    view->ShowShortcutResult(result);
    return true;
}
}
