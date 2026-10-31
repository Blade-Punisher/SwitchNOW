#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>
#include <optional>
namespace opennow::game_feed {
inline constexpr std::size_t kPageSize = 15;
struct Page {
    std::size_t index, start, end, count, total;
    bool HasNext() const { return end < total; }
    std::size_t Remaining() const { return total - end; }
};
inline Page Window(std::size_t total, std::size_t requested) {
    const auto count = total == 0 ? 1 : 1 + (total - 1) / kPageSize;
    const auto index = std::min(requested, count - 1);
    const auto start = std::min(total, index * kPageSize);
    return {index, start, start + std::min(kPageSize, total-start), count, total};
}
}

namespace opennow::game_feed {
// 0 is a compact gap control that opens the numeric page picker.
inline std::vector<std::size_t> PageNumbers(const Page& page) {
    std::vector<std::size_t> numbers;
    if (page.total == 0) return numbers;
    if (page.count <= 11) {
        for (std::size_t n=1; n<=page.count; ++n) numbers.push_back(n);
        return numbers;
    }
    const auto current = page.index+1;
    const auto first = std::clamp(current > 3 ? current-3 : std::size_t{2}, std::size_t{2}, page.count-7);
    numbers.push_back(1);
    if (first>2) numbers.push_back(0);
    for (std::size_t n=first; n<first+7; ++n) numbers.push_back(n);
    if (first+7<page.count) numbers.push_back(0);
    numbers.push_back(page.count);
    return numbers;
}
inline std::optional<std::size_t> ParsePage(long number, std::size_t count) {
    if (number<1 || static_cast<std::size_t>(number)>count) return {};
    return static_cast<std::size_t>(number)-1;
}
enum class EmptyState { Loading, NoMatches, Empty, Incomplete };
inline EmptyState DescribeEmpty(bool loading, bool has_games, bool complete) {
    if (loading) return EmptyState::Loading;
    if (has_games) return EmptyState::NoMatches;
    return complete ? EmptyState::Empty : EmptyState::Incomplete;
}
}
