#pragma once
#include <utility>
#include <vector>
template<class Resource>
class CDeferredGpuRelease {
    std::vector<Resource> retired;
public:
    void Retire(Resource resource) { retired.push_back(std::move(resource)); }
    // The owner must wait for its submission fence before calling this.
    void ReleaseAfterFence() { retired.clear(); }
};
