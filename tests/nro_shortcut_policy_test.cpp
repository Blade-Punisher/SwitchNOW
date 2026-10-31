#include "nro_shortcut_policy.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main()
{
    using namespace opennow::shortcut;

    constexpr size_t executable_size = 0x100;
    constexpr size_t header_size = 0x38;
    constexpr size_t old_icon_size = 4;
    constexpr size_t nacp_size = 0x4000;
    constexpr size_t romfs_size = 5;

    std::vector<std::uint8_t> input(
        executable_size + header_size + old_icon_size + nacp_size + romfs_size, 0);
    detail::WriteU32(input, 0x18, executable_size);
    detail::WriteU32(input, executable_size, 0x54455341);
    detail::WriteU64(input, executable_size + 0x08, header_size);
    detail::WriteU64(input, executable_size + 0x10, old_icon_size);
    detail::WriteU64(input, executable_size + 0x18, header_size + old_icon_size);
    detail::WriteU64(input, executable_size + 0x20, nacp_size);
    detail::WriteU64(input, executable_size + 0x28, header_size + old_icon_size + nacp_size);
    detail::WriteU64(input, executable_size + 0x30, romfs_size);
    std::copy_n(
        reinterpret_cast<const std::uint8_t*>("OLD"), 3,
        input.begin() + static_cast<std::ptrdiff_t>(executable_size + header_size));
    std::copy_n(
        reinterpret_cast<const std::uint8_t*>("romfs"), romfs_size,
        input.end() - static_cast<std::ptrdiff_t>(romfs_size));

    const std::vector<std::uint8_t> icon {0xFF, 0xD8, 0xFF, 0xD9};
    std::vector<std::uint8_t> output;
    assert(BuildShortcutNro(input, icon, "Control", output));
    assert(detail::ReadU32(output, 0x18) == executable_size);
    assert(detail::ReadU32(output, executable_size) == 0x54455341);
    assert(detail::ReadU64(output, executable_size + 0x10) == icon.size());

    const size_t new_nacp = executable_size +
        static_cast<size_t>(detail::ReadU64(output, executable_size + 0x18));
    assert(std::string(
        reinterpret_cast<const char*>(output.data() + new_nacp)) == "Control");
    assert(std::string(
        reinterpret_cast<const char*>(output.data() + new_nacp + 0x200)) == "SwitchNOW");
    const size_t new_romfs = executable_size +
        static_cast<size_t>(detail::ReadU64(output, executable_size + 0x28));
    assert(std::string(
        reinterpret_cast<const char*>(output.data() + new_romfs), romfs_size) == "romfs");

    std::vector<std::uint8_t> unchanged;
    assert(BuildShortcutNro(output, icon, "Control", unchanged));
    assert(unchanged == output); // No SD write/reinstall for identical artwork.
    const std::vector<std::uint8_t> larger_icon {0xFF, 0xD8, 1, 2, 3, 0xFF, 0xD9};
    std::vector<std::uint8_t> refreshed;
    assert(BuildShortcutNro(output, larger_icon, "Control", refreshed));
    assert(refreshed != output);
    assert(std::equal(output.begin(), output.begin() + executable_size, refreshed.begin()));
    const auto moved_romfs = executable_size + detail::ReadU64(refreshed, executable_size + 0x28);
    assert(std::string(reinterpret_cast<const char*>(refreshed.data() + moved_romfs), romfs_size) == "romfs");
    assert(!BuildShortcutNro({}, icon, "Bad", output));
    assert(!BuildShortcutNro(input, {}, "Bad", output));
    input[executable_size] = 0;
    assert(!BuildShortcutNro(input, icon, "Bad", output));
    return 0;
}
