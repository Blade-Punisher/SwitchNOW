#pragma once
#include <borealis.hpp>
#include <cstdint>
namespace opennow::shortcut {
void ShowHomeArtworkToast();
void DrawHomeArtworkToast(NVGcontext* vg);
class ArtworkToastDialog final : public brls::Dialog {
public:
    explicit ArtworkToastDialog(brls::Box* content) : brls::Dialog(content) {}
    void draw(NVGcontext* vg,float x,float y,float width,float height,
              brls::Style style,brls::FrameContext* context) override {
        brls::Dialog::draw(vg,x,y,width,height,style,context);
        DrawHomeArtworkToast(vg);
    }
};
}
