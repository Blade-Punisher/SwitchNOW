#include "keyboard_input_policy.hpp"

#include <cassert>

int main()
{
    using opennow::input::KeyboardStroke;
    using opennow::input::KeyboardShortcut;
    using opennow::input::MapAsciiKey;
    using opennow::input::MapKeyboardShortcut;

    KeyboardStroke stroke;
    assert(MapAsciiKey('a', stroke));
    assert(stroke.keycode == 'A' && stroke.scancode == 0x1e && stroke.modifiers == 0);

    assert(MapAsciiKey('A', stroke));
    assert(stroke.keycode == 'A' && stroke.scancode == 0x1e && stroke.modifiers == 0x0001);

    assert(MapAsciiKey('@', stroke));
    assert(stroke.keycode == '2' && stroke.scancode == 0x03 && stroke.modifiers == 0x0001);

    assert(MapAsciiKey('?', stroke));
    assert(stroke.keycode == 0xbf && stroke.scancode == 0x35 && stroke.modifiers == 0x0001);

    assert(MapAsciiKey(' ', stroke));
    assert(stroke.keycode == 0x20 && stroke.scancode == 0x39 && stroke.modifiers == 0);

    assert(!MapAsciiKey('\t', stroke));

    stroke = MapKeyboardShortcut(KeyboardShortcut::Escape);
    assert(stroke.keycode == 0x1b && stroke.scancode == 0x01 && stroke.modifiers == 0);

    stroke = MapKeyboardShortcut(KeyboardShortcut::Tab);
    assert(stroke.keycode == 0x09 && stroke.scancode == 0x0f && stroke.modifiers == 0);

    stroke = MapKeyboardShortcut(KeyboardShortcut::AltTab);
    assert(stroke.keycode == 0x09 && stroke.scancode == 0x0f &&
           stroke.modifiers == 0x0004);

    stroke = MapKeyboardShortcut(KeyboardShortcut::Windows);
    assert(stroke.keycode == 0x5b && stroke.scancode == 0xe05b &&
           stroke.modifiers == 0x0008);
    return 0;
}
