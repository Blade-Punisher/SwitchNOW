#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>
namespace opennow::shortcut::nca {
using Bytes = std::vector<uint8_t>;
using Digest = std::array<uint8_t, 32>;
using Hash = std::function<Digest(std::span<const uint8_t>)>;
// Only plaintext-section NCA3 archives produced for SwitchNOW forwarders.
// The caller decrypts/encrypts the 0xC00-byte NCA header, never game content.
Bytes ReplaceControlIcon(const Bytes& decrypted_header_nca, uint64_t title_id,
                         const Bytes& jpeg, const Hash& hash);
Bytes ReplaceMetaControl(const Bytes& decrypted_header_nca, uint64_t title_id,
                         std::span<const uint8_t, 16> old_control,
                         const Digest& new_control_hash, uint64_t new_control_size,
                         const Hash& hash);
Bytes ReplaceDatabaseRecords(const Bytes& database,
    std::span<const uint8_t, 16> old_control, const Digest& control_hash, uint64_t control_size,
    std::span<const uint8_t, 16> old_meta, const Digest& meta_hash, uint64_t meta_size);
}
