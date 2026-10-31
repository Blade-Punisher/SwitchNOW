#include "../extern/borealis/library/include/borealis/extern/nanovg/framework/CCmdMemorySpill.h"
#include <cassert>
#include <map>
#include <iostream>
namespace {
struct Block { unsigned size; bool busy=false; };
std::map<int,Block> blocks;
unsigned allocations=0; int serial=0;
struct Handle {
    int id=0;
    explicit operator bool() const {return id!=0;}
    unsigned getSize() const {return blocks.at(id).size;}
    void destroy() {if(id){assert(!blocks.at(id).busy);assert(blocks.erase(id)==1);id=0;}}
};
Handle allocate(unsigned size) {blocks.emplace(++serial,Block{size});++allocations;return {serial};}
}
int main() {
    CCmdMemorySpill<Handle> arena;
    arena.BeginFrame();auto a=arena.Acquire(300,allocate);auto b=arena.Acquire(300,allocate);
    assert(a.id!=b.id && allocations==2 && a.getSize()==131072);
    blocks[a.id].busy=blocks[b.id].busy=true;
    // Simulated completion fence, matching CCmdMemRing::begin in Flush.
    for(auto& item:blocks)item.second.busy=false;
    arena.BeginFrame();assert(arena.Acquire(300,allocate).id==a.id);
    assert(arena.Acquire(300,allocate).id==b.id && allocations==2);
    arena.BeginFrame();auto c=arena.Acquire(200000,allocate);
    assert(c.id!=a.id && !blocks.count(a.id) && c.getSize()==200704);
    arena.BeginFrame();assert(arena.Acquire(1,allocate).id==c.id);
    assert(arena.Acquire(1,allocate).id==b.id);
    const auto count=allocations;
    assert(!arena.Acquire(std::numeric_limits<size_t>::max(),allocate));
    assert(allocations==count);
    assert(!arena.Acquire(1,[](unsigned){return Handle{};}));
    arena.Destroy();assert(blocks.empty());
    std::cout<<"Production command-memory spill growth/reuse/retirement/bounds tests passed.\n";
}
