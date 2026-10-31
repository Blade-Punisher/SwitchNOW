#pragma once
#include <functional>
#include <exception>
#include <stdexcept>
#include <vector>
#include <cstdint>
namespace opennow::shortcut {
inline bool VerifyHomeArtworkAndNotify(const std::function<bool()>& installed_matches,
    const std::function<bool()>& cached_matches,const std::function<void()>& notify) {
    if(!installed_matches())throw std::runtime_error("Installed HOME icon differs after metadata commit");
    if(!cached_matches())throw std::runtime_error("HOME cache differs from installed icon");
    notify();
    return true;
}
struct ArtworkCommitOperations {
    std::function<std::vector<uint8_t>()> read;
    std::function<void(const std::vector<uint8_t>&)> write;
    std::function<void()> commit;
    std::function<void()> invalidate;
    std::function<bool()> verify_icon;
    std::function<void(bool)> rollback_result;
};
inline void CommitHomeArtwork(const std::vector<uint8_t>& previous,
    const std::vector<uint8_t>& next, const ArtworkCommitOperations& operations) {
    if(operations.read()!=previous)
        throw std::runtime_error("Shortcut metadata changed during artwork preparation");
    try {
        operations.write(next);operations.commit();
        if(operations.read()!=next)throw std::runtime_error("Committed metadata mismatch");
        operations.invalidate();
        if(!operations.verify_icon())throw std::runtime_error("HOME control icon did not refresh");
    }catch(...) {
        const auto original=std::current_exception();bool restored=false;
        try {operations.write(previous);operations.commit();restored=operations.read()==previous;}
        catch(...) {}
        operations.rollback_result(restored);
        try {operations.invalidate();}catch(...) {}
        std::rethrow_exception(original);
    }
}
}
