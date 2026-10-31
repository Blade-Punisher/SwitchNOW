#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// The renderer must wait for the previous frame fence before BeginFrame,
// and wait for GPU idle before Destroy. Never overwrite in-flight commands.
template<class Handle>
class CCmdMemorySpill {
    std::vector<Handle> blocks;
    size_t next = 0;
public:
    static uint32_t ChunkSize(size_t minimum) {
        if (minimum > std::numeric_limits<uint32_t>::max() - 4095u) return 0;
        return (static_cast<uint32_t>(std::max<size_t>(minimum, 0x20000)) + 4095u) & ~4095u;
    }
    void BeginFrame() { next = 0; }
    template<class Allocate> Handle Acquire(size_t minimum, Allocate allocate) {
        const auto size = ChunkSize(minimum);
        if (!size) return {};
        if (next < blocks.size() && blocks[next].getSize() >= size)
            return blocks[next++];
        auto block = allocate(size);
        if (!block) return {};
        if (next == blocks.size()) blocks.push_back(block);
        else { blocks[next].destroy(); blocks[next] = block; }
        ++next;
        return block;
    }
    void Destroy() { for (auto& block : blocks) block.destroy(); blocks.clear(); next = 0; }
};
