#pragma once

#include <cstdint>

namespace opennow::input
{

struct KeyboardStroke
{
    std::uint16_t keycode   = 0;
    std::uint16_t scancode  = 0;
    std::uint16_t modifiers = 0;
};

enum class KeyboardShortcut
{
    Escape,
    Tab,
    AltTab,
    Windows,
};

inline KeyboardStroke MapKeyboardShortcut(KeyboardShortcut shortcut)
{
    constexpr std::uint16_t kAlt  = 0x0004;
    constexpr std::uint16_t kMeta = 0x0008;

    switch (shortcut)
    {
        case KeyboardShortcut::Escape: return {0x1b, 0x01, 0};
        case KeyboardShortcut::Tab: return {0x09, 0x0f, 0};
        case KeyboardShortcut::AltTab: return {0x09, 0x0f, kAlt};
        case KeyboardShortcut::Windows: return {0x5b, 0xe05b, kMeta};
    }

    return {};
}

inline bool MapAsciiKey(char character, KeyboardStroke& stroke)
{
    constexpr std::uint16_t kShift = 0x0001;
    static constexpr std::uint8_t letter_scans[26] = {
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c,
    };
    if (character >= 'a' && character <= 'z')
    {
        const int index = character - 'a';
        stroke = {static_cast<std::uint16_t>('A' + index), letter_scans[index], 0};
        return true;
    }
    if (character >= 'A' && character <= 'Z')
    {
        const int index = character - 'A';
        stroke = {static_cast<std::uint16_t>('A' + index), letter_scans[index], kShift};
        return true;
    }
    if (character >= '1' && character <= '9')
    {
        stroke = {
            static_cast<std::uint16_t>(character),
            static_cast<std::uint16_t>(0x02 + character - '1'),
            0,
        };
        return true;
    }
    if (character == '0')
    {
        stroke = {'0', 0x0b, 0};
        return true;
    }

    switch (character)
    {
        case ' ': stroke = {0x20, 0x39, 0}; return true;
        case '@': stroke = {'2', 0x03, kShift}; return true;
        case '.': stroke = {0xbe, 0x34, 0}; return true;
        case ',': stroke = {0xbc, 0x33, 0}; return true;
        case '-': stroke = {0xbd, 0x0c, 0}; return true;
        case '_': stroke = {0xbd, 0x0c, kShift}; return true;
        case '=': stroke = {0xbb, 0x0d, 0}; return true;
        case '+': stroke = {0xbb, 0x0d, kShift}; return true;
        case '/': stroke = {0xbf, 0x35, 0}; return true;
        case '?': stroke = {0xbf, 0x35, kShift}; return true;
        case '\\': stroke = {0xdc, 0x2b, 0}; return true;
        case '|': stroke = {0xdc, 0x2b, kShift}; return true;
        case '[': stroke = {0xdb, 0x1a, 0}; return true;
        case '{': stroke = {0xdb, 0x1a, kShift}; return true;
        case ']': stroke = {0xdd, 0x1b, 0}; return true;
        case '}': stroke = {0xdd, 0x1b, kShift}; return true;
        case ':': stroke = {0xba, 0x27, kShift}; return true;
        case ';': stroke = {0xba, 0x27, 0}; return true;
        case '\'': stroke = {0xde, 0x28, 0}; return true;
        case '"': stroke = {0xde, 0x28, kShift}; return true;
        case '`': stroke = {0xc0, 0x29, 0}; return true;
        case '~': stroke = {0xc0, 0x29, kShift}; return true;
        case '<': stroke = {0xbc, 0x33, kShift}; return true;
        case '>': stroke = {0xbe, 0x34, kShift}; return true;
        case '!': stroke = {'1', 0x02, kShift}; return true;
        case '#': stroke = {'3', 0x04, kShift}; return true;
        case '$': stroke = {'4', 0x05, kShift}; return true;
        case '%': stroke = {'5', 0x06, kShift}; return true;
        case '^': stroke = {'6', 0x07, kShift}; return true;
        case '&': stroke = {'7', 0x08, kShift}; return true;
        case '*': stroke = {'8', 0x09, kShift}; return true;
        case '(': stroke = {'9', 0x0a, kShift}; return true;
        case ')': stroke = {'0', 0x0b, kShift}; return true;
        default: return false;
    }
}

} // namespace opennow::input
