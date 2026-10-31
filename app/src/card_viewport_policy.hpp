#pragma once
#include <cmath>
namespace opennow::ui {
struct CardRect { float x, y, width, height; };
inline bool CardInViewport(CardRect card, CardRect viewport, float margin) {
    if (!std::isfinite(card.x) || !std::isfinite(card.y) ||
        card.width <= 0 || card.height <= 0 || viewport.width <= 0 || viewport.height <= 0)
        return false;
    return card.x + card.width >= viewport.x - margin &&
        card.x <= viewport.x + viewport.width + margin &&
        card.y + card.height >= viewport.y - margin &&
        card.y <= viewport.y + viewport.height + margin;
}
}
