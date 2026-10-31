#include "home_artwork_toast.hpp"
#include "localization.hpp"
#include "home_artwork_toast_policy.hpp"
#include <chrono>
namespace opennow::shortcut {
namespace {
ArtworkToastTimer timer;
int64_t NowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
void ShowHomeArtworkToast() { timer.Show(NowMs()); }
void DrawHomeArtworkToast(NVGcontext* vg) {
    if(!timer.Visible(NowMs()))return;
    const auto text=Tr("HOME shortcut artwork updated");
    nvgSave(vg);
    nvgResetScissor(vg);
    nvgFontSize(vg,20);nvgFontFaceId(vg,brls::Application::getFont(brls::FONT_REGULAR));
    const float width=std::min(620.0f,nvgTextBounds(vg,0,0,text.c_str(),nullptr,nullptr)+48);
    const float x=1280-width-24,y=720-56-24;
    nvgBeginPath(vg);nvgRoundedRect(vg,x,y,width,56,10);
    nvgFillColor(vg,nvgRGBA(16,30,25,235));nvgFill(vg);
    nvgFillColor(vg,nvgRGB(114,239,165));nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE);
    nvgText(vg,x+24,y+28,text.c_str(),nullptr);nvgRestore(vg);
}
}
