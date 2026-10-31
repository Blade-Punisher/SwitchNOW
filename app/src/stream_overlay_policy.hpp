#pragma once

#include <cstddef>
#include <cstdint>

namespace opennow::input
{

enum class OverlayChordHalf
{
    None,
    Minus,
    Plus,
};

struct OverlayChordState
{
    OverlayChordHalf pending_half = OverlayChordHalf::None;
    std::int64_t pending_since_ms = 0;
    bool disqualified             = false;
};

struct OverlayChordDecision
{
    bool toggle_overlay = false;
    bool preempt_input  = false;
    OverlayChordState next_state;
};

enum class OverlayAction : std::size_t
{
    ToggleStatistics = 0,
    RefreshNetwork = 1,
    Close = 2,
    ExitStream = 3,
};

constexpr std::size_t kOverlayActionCount = 4;

constexpr std::size_t MoveOverlaySelection(
    std::size_t current, int direction)
{
    const std::size_t normalized = current % kOverlayActionCount;
    if (direction > 0)
        return (normalized + 1) % kOverlayActionCount;
    if (direction < 0)
        return (normalized + kOverlayActionCount - 1) % kOverlayActionCount;
    return normalized;
}

constexpr OverlayAction SelectedOverlayAction(std::size_t selection)
{
    return static_cast<OverlayAction>(selection % kOverlayActionCount);
}

inline OverlayChordDecision EvaluateOverlayChord(
    bool minus_pressed,
    bool plus_pressed,
    const OverlayChordState& state,
    std::int64_t now_ms,
    std::int64_t grace_ms = 120)
{
    if (minus_pressed && plus_pressed)
    {
        if (state.disqualified)
            return {false, false, state};
        return {true, true, {}};
    }

    const OverlayChordHalf pressed_half =
        minus_pressed == plus_pressed
            ? OverlayChordHalf::None
            : (minus_pressed ? OverlayChordHalf::Minus : OverlayChordHalf::Plus);
    if (pressed_half == OverlayChordHalf::None)
        return {};

    if (state.disqualified)
    {
        OverlayChordState next = state;
        if (state.pending_half != pressed_half)
            next = {pressed_half, now_ms, true};
        return {false, false, next};
    }

    if (state.pending_half == OverlayChordHalf::None ||
        state.pending_half != pressed_half)
    {
        return {false, true, {pressed_half, now_ms, false}};
    }

    OverlayChordState next = state;
    next.disqualified = now_ms - state.pending_since_ms >= grace_ms;
    return {false, !next.disqualified, next};
}

} // namespace opennow::input
