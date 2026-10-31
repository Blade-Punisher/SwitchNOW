#include "stream_overlay_policy.hpp"

#include <cassert>

int main()
{
    using namespace opennow::input;

    OverlayChordState state;
    auto decision = EvaluateOverlayChord(true, false, state, 1000);
    assert(!decision.toggle_overlay);
    assert(decision.preempt_input);
    state = decision.next_state;

    decision = EvaluateOverlayChord(true, true, state, 1050);
    assert(decision.toggle_overlay);
    assert(decision.preempt_input);

    state = {};
    decision = EvaluateOverlayChord(false, true, state, 2000);
    assert(!decision.toggle_overlay);
    assert(decision.preempt_input);
    state = decision.next_state;

    decision = EvaluateOverlayChord(false, true, state, 2120);
    assert(!decision.toggle_overlay);
    assert(!decision.preempt_input);
    assert(decision.next_state.disqualified);

    state = decision.next_state;
    decision = EvaluateOverlayChord(true, true, state, 2130);
    assert(!decision.toggle_overlay);
    assert(!decision.preempt_input);

    decision = EvaluateOverlayChord(false, false, state, 2200);
    assert(!decision.toggle_overlay);
    assert(!decision.preempt_input);
    assert(decision.next_state.pending_half == OverlayChordHalf::None);

    static_assert(kOverlayActionCount == 4);
    static_assert(MoveOverlaySelection(0, -1) == 3);
    static_assert(MoveOverlaySelection(3, 1) == 0);
    assert(SelectedOverlayAction(MoveOverlaySelection(1, 1)) ==
           OverlayAction::Close);
    return 0;
}
