#pragma once

#include <FreeInkUI.h>

#include <cstdint>

// App-defined Latin letter layers for European languages the SDK's builtin
// layouts do not cover (or cover without all their letters). The symbol pages
// still come from the SDK.
namespace eu_keyboard {

enum class Layout : uint8_t { None, Fr, De, It, Pt, Nl, Pl };

// The letter layer for `layout`, nullptr for Layout::None.
const freeink::ui::KeyboardLayout* letters(Layout layout, bool shifted, bool langKey);

}  // namespace eu_keyboard
