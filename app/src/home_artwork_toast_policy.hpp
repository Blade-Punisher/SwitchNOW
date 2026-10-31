#pragma once
#include <cstdint>
namespace opennow::shortcut {
struct ArtworkToastTimer {
    int64_t started = -1;
    void Show(int64_t now_ms) { started=now_ms; }
    bool Visible(int64_t now_ms) const {
        return started>=0 && now_ms>=started && now_ms-started<10000;
    }
};
}
