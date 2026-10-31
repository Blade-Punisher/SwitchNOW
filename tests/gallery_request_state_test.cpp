#include "gallery_request_state.hpp"
#include <cassert>
#include <functional>
#include <vector>
int main() {
    opennow::GalleryRequestState state;
    std::vector<std::function<void()>> ui;
    int displayed = 0;
    const auto first = state.Begin();
    // The old download finished before the user switched; UI dispatch is delayed.
    ui.push_back([&] { if (state.IsCurrent(first)) displayed = 1; });
    const auto second = state.Begin();
    ui.push_back([&] { if (state.IsCurrent(second)) displayed = 2; });
    ui[1](); ui[0]();
    assert(displayed == 2);
    // Returning to the first URL is still a new request, not the first token.
    const auto revisit = state.Begin();
    assert(!state.IsCurrent(first) && !state.IsCurrent(second));
    assert(state.IsCurrent(revisit));
    state.Close();
    assert(!state.IsCurrent(revisit));
    const auto after_close = state.Begin();
    assert(!state.IsCurrent(after_close));
}
