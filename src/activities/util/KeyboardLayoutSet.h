#pragma once

#include <FreeInkUI.h>
#include <I18n.h>

#include <cstdint>

#include "EuKeyboardLayouts.h"

namespace keyboard_layouts {

struct LayoutInfo {
  // SDK layout: its letter layers unless `letters` replaces them, and always
  // its symbol pages.
  freeink::ui::KeyboardLayoutId id;
  Language language;
  eu_keyboard::Layout letters = eu_keyboard::Layout::None;
  const char* name = nullptr;  // Settings label when the language's own name is too specific
};

// Table position is the persisted bit assignment. Keep existing rows in place
// and append new layouts so SDK enum changes cannot reinterpret saved masks.
inline constexpr LayoutInfo ALL[] = {
    {freeink::ui::KeyboardLayoutId::QwertyEn, Language::EN},
    {freeink::ui::KeyboardLayoutId::AzertyFr, Language::FR, eu_keyboard::Layout::Fr},
    {freeink::ui::KeyboardLayoutId::QwertzDe, Language::DE, eu_keyboard::Layout::De},
    {freeink::ui::KeyboardLayoutId::SpanishEs, Language::ES},
    {freeink::ui::KeyboardLayoutId::CyrillicRu, Language::RU},
    {freeink::ui::KeyboardLayoutId::CyrillicUk, Language::UK},
    {freeink::ui::KeyboardLayoutId::CyrillicBe, Language::BE},
    {freeink::ui::KeyboardLayoutId::CyrillicKk, Language::KK},
    {freeink::ui::KeyboardLayoutId::HebrewIl, Language::HE},
    {freeink::ui::KeyboardLayoutId::ArabicAr, Language::AR},
    {freeink::ui::KeyboardLayoutId::QwertyEn, Language::IT, eu_keyboard::Layout::It},
    {freeink::ui::KeyboardLayoutId::QwertyEn, Language::PT, eu_keyboard::Layout::Pt, "Portugu\xC3\xAAs"},
    {freeink::ui::KeyboardLayoutId::QwertyEn, Language::NL, eu_keyboard::Layout::Nl},
    {freeink::ui::KeyboardLayoutId::QwertyEn, Language::PL, eu_keyboard::Layout::Pl},
};
inline constexpr uint8_t COUNT = sizeof(ALL) / sizeof(ALL[0]);
static_assert(COUNT <= 16, "keyboard layout mask is uint16_t");

inline constexpr uint16_t bitAt(const uint8_t i) { return static_cast<uint16_t>(1u << i); }
// Symbol layers have no Latin letters, so credentials and URLs require at
// least one of these layouts to remain enabled.
inline constexpr uint16_t LATIN_BITS =
    bitAt(0) | bitAt(1) | bitAt(2) | bitAt(3) | bitAt(10) | bitAt(11) | bitAt(12) | bitAt(13);
// The always-available fallback (QWERTY English).
inline constexpr uint8_t ENGLISH = 0;

// Layouts are named by their ALL[] index.
uint16_t enabled();
uint8_t startingLayout();
uint8_t next(uint8_t current);
// An enabled layout that types the script `sample` starts with (Latin,
// Cyrillic, Hebrew, Arabic): `current` when it already does or none does.
uint8_t forScriptOf(const char* sample, uint8_t current);
// Letter (or symbol) layer of layout `index`.
const freeink::ui::KeyboardLayout& layer(uint8_t index, bool shifted, bool symbols, bool langKey);

}  // namespace keyboard_layouts
