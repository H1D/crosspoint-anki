#include "KeyboardLayoutSet.h"

#include "CrossPointSettings.h"

namespace keyboard_layouts {

namespace {

enum class Script : uint8_t { Latin, Cyrillic, Hebrew, Arabic };

uint8_t forLanguage(Language language) {
  // One Portuguese layout serves both UI variants.
  if (language == Language::P2) language = Language::PT;
  for (uint8_t i = 0; i < COUNT; ++i) {
    if (ALL[i].language == language) return i;
  }
  return ENGLISH;
}

constexpr uint16_t ALL_BITS = static_cast<uint16_t>((uint32_t{1} << COUNT) - 1);

Script scriptOfLayout(const uint8_t index) {
  switch (ALL[index].id) {
    case freeink::ui::KeyboardLayoutId::CyrillicRu:
    case freeink::ui::KeyboardLayoutId::CyrillicUk:
    case freeink::ui::KeyboardLayoutId::CyrillicBe:
    case freeink::ui::KeyboardLayoutId::CyrillicKk:
      return Script::Cyrillic;
    case freeink::ui::KeyboardLayoutId::HebrewIl:
      return Script::Hebrew;
    case freeink::ui::KeyboardLayoutId::ArabicAr:
      return Script::Arabic;
    default:
      return Script::Latin;
  }
}

// Script of the first letter in `text`; false when it has none.
bool scriptOfText(const char* text, Script& out) {
  for (const auto* p = reinterpret_cast<const unsigned char*>(text); *p;) {
    uint32_t cp = *p;
    int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
    if (extra) cp &= 0x3F >> extra;
    p++;
    for (; extra > 0 && (*p & 0xC0) == 0x80; --extra) cp = (cp << 6) | (*p++ & 0x3F);
    if (cp >= 0x400 && cp <= 0x4FF) {
      out = Script::Cyrillic;
      return true;
    }
    if (cp >= 0x590 && cp <= 0x5FF) {
      out = Script::Hebrew;
      return true;
    }
    if (cp >= 0x600 && cp <= 0x6FF) {
      out = Script::Arabic;
      return true;
    }
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= 0xC0 && cp <= 0x24F)) {
      out = Script::Latin;
      return true;
    }
  }
  return false;
}

}  // namespace

uint16_t enabled() {
  // Drop bits naming no layout: the mask comes from a hand-editable file and
  // survives downgrades, so it can carry bits this build does not have. Left
  // in, such a mask would read as "configured" while enabling nothing.
  const uint16_t configured = static_cast<uint16_t>(SETTINGS.keyboardLayouts & ALL_BITS);
  if (configured != 0) {
    if (configured & LATIN_BITS) return configured;
    return static_cast<uint16_t>(configured | bitAt(ENGLISH));
  }
  // Unconfigured: the UI language's layout plus English. An English UI collapses
  // to one layout and the language key disappears -- there is nowhere to go.
  return static_cast<uint16_t>(bitAt(forLanguage(I18N.getLanguage())) | bitAt(ENGLISH));
}

uint8_t startingLayout() {
  const uint8_t preferred = forLanguage(I18N.getLanguage());
  if (enabled() & bitAt(preferred)) return preferred;
  // Switched off: opening on it anyway would ignore a deliberate choice.
  return next(preferred);
}

uint8_t next(const uint8_t current) {
  const uint16_t mask = enabled();
  // A current layout the table does not list still has to lead somewhere.
  const uint8_t start = current < COUNT ? current : 0;
  for (uint8_t step = 1; step <= COUNT; ++step) {
    const uint8_t i = static_cast<uint8_t>((start + step) % COUNT);
    if (mask & bitAt(i)) return i;
  }
  return current;
}

uint8_t forScriptOf(const char* sample, const uint8_t current) {
  Script wanted;
  if (!sample || !scriptOfText(sample, wanted)) return current;
  if (current < COUNT && scriptOfLayout(current) == wanted) return current;
  const uint16_t mask = enabled();
  for (uint8_t i = 0; i < COUNT; ++i) {
    if ((mask & bitAt(i)) && scriptOfLayout(i) == wanted) return i;
  }
  return current;
}

const freeink::ui::KeyboardLayout& layer(const uint8_t index, const bool shifted, const bool symbols,
                                         const bool langKey) {
  const LayoutInfo& info = ALL[index < COUNT ? index : ENGLISH];
  if (symbols) return freeink::ui::builtinKeyboardLayout(info.id, shifted, true);
  if (const freeink::ui::KeyboardLayout* app = eu_keyboard::letters(info.letters, shifted, langKey)) return *app;
  return freeink::ui::builtinKeyboardLayout(info.id, shifted, false, /*numberRow=*/true, langKey);
}

}  // namespace keyboard_layouts
