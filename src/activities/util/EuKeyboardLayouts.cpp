#include "EuKeyboardLayouts.h"

// Latin letter layers the SDK does not ship (or ships without the letters a
// language needs): a digit row, an optional row of the language's accented
// letters, three letter rows, and the SDK's bottom row arrangement. Same key
// ids as the SDK tables: ASCII letters by character, the rest by code point,
// control keys by the QWERTY_KEY_* ids. Generated from one letter list per
// language; uppercase layers mirror the lowercase ones key for key.

namespace fui = freeink::ui;

namespace eu_keyboard {

namespace {

#define K(label, output, value) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, 1, true }
#define KA(label, output, value, alt) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, 1, true, alt }
#define KS(label, kind, value, units) \
  fui::KeyboardKey { label, nullptr, kind, fui::StateNormal, value, units, true }

const fui::KeyboardKey NUM_ROW[] = {KA("1", "1", '1', "!"), KA("2", "2", '2', "@"), KA("3", "3", '3', "#"),
                                    KA("4", "4", '4', "$"), KA("5", "5", '5', "%"), KA("6", "6", '6', "^"),
                                    KA("7", "7", '7', "&"), KA("8", "8", '8', "*"), KA("9", "9", '9', "("),
                                    KA("0", "0", '0', ")")};
const fui::KeyboardKey NUM_SHIFT_ROW[] = {KA("!", "!", '!', "1"), KA("@", "@", '@', "2"), KA("#", "#", '#', "3"),
                                          KA("$", "$", '$', "4"), KA("%", "%", '%', "5"), KA("^", "^", '^', "6"),
                                          KA("&", "&", '&', "7"), KA("*", "*", '*', "8"), KA("(", "(", '(', "9"),
                                          KA(")", ")", ')', "0")};
const fui::KeyboardKey BOTTOM_ROW[] = {
    KS("?123", fui::KeyKind::Mode, fui::QWERTY_KEY_MODE, 2), KS("Shift", fui::KeyKind::Shift, fui::QWERTY_KEY_SHIFT, 2),
    KS("Space", fui::KeyKind::Space, fui::QWERTY_KEY_SPACE, 4), KS("OK", fui::KeyKind::Ok, fui::QWERTY_KEY_ENTER, 2)};
const fui::KeyboardKey BOTTOM_LANG_ROW[] = {
    KS("?123", fui::KeyKind::Mode, fui::QWERTY_KEY_MODE, 2), KS(nullptr, fui::KeyKind::Lang, fui::QWERTY_KEY_LANG, 1),
    KS("Shift", fui::KeyKind::Shift, fui::QWERTY_KEY_SHIFT, 2),
    KS("Space", fui::KeyKind::Space, fui::QWERTY_KEY_SPACE, 3), KS("OK", fui::KeyKind::Ok, fui::QWERTY_KEY_ENTER, 2)};

// AZERTY with the French accents on their own row; ' sits where AZERTY's ù would.
const fui::KeyboardKey FR_ACCENTS[] = {K("é", "é", 233),       K("è", "è", 232),      KA("ê", "ê", 234, "ë"),
                                       KA("à", "à", 224, "â"), K("ç", "ç", 231),      KA("ù", "ù", 249, "û"),
                                       KA("î", "î", 238, "ï"), KA("ô", "ô", 244, "œ")};
const fui::KeyboardKey FR_ROW1[] = {K("a", "a", 'a'), K("z", "z", 'z'), K("e", "e", 'e'), K("r", "r", 'r'),
                                    K("t", "t", 't'), K("y", "y", 'y'), K("u", "u", 'u'), K("i", "i", 'i'),
                                    K("o", "o", 'o'), K("p", "p", 'p')};
const fui::KeyboardKey FR_ROW2[] = {K("q", "q", 'q'), K("s", "s", 's'), K("d", "d", 'd'), K("f", "f", 'f'),
                                    K("g", "g", 'g'), K("h", "h", 'h'), K("j", "j", 'j'), K("k", "k", 'k'),
                                    K("l", "l", 'l'), K("m", "m", 'm')};
const fui::KeyboardKey FR_ROW3[] = {K("w", "w", 'w'),  K("x", "x", 'x'),
                                    K("c", "c", 'c'),  K("v", "v", 'v'),
                                    K("b", "b", 'b'),  K("n", "n", 'n'),
                                    K("'", "'", '\''), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardKey FR_SHIFT_ACCENTS[] = {K("É", "É", 201),       K("È", "È", 200),      KA("Ê", "Ê", 202, "Ë"),
                                             KA("À", "À", 192, "Â"), K("Ç", "Ç", 199),      KA("Ù", "Ù", 217, "Û"),
                                             KA("Î", "Î", 206, "Ï"), KA("Ô", "Ô", 212, "Œ")};
const fui::KeyboardKey FR_SHIFT_ROW1[] = {K("A", "A", 'A'), K("Z", "Z", 'Z'), K("E", "E", 'E'), K("R", "R", 'R'),
                                          K("T", "T", 'T'), K("Y", "Y", 'Y'), K("U", "U", 'U'), K("I", "I", 'I'),
                                          K("O", "O", 'O'), K("P", "P", 'P')};
const fui::KeyboardKey FR_SHIFT_ROW2[] = {K("Q", "Q", 'Q'), K("S", "S", 'S'), K("D", "D", 'D'), K("F", "F", 'F'),
                                          K("G", "G", 'G'), K("H", "H", 'H'), K("J", "J", 'J'), K("K", "K", 'K'),
                                          K("L", "L", 'L'), K("M", "M", 'M')};
const fui::KeyboardKey FR_SHIFT_ROW3[] = {
    K("W", "W", 'W'),  K("X", "X", 'X'),
    K("C", "C", 'C'),  K("V", "V", 'V'),
    K("B", "B", 'B'),  K("N", "N", 'N'),
    K("'", "'", '\''), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardRow FR_ROWS[] = {{NUM_ROW, 10, 0}, {FR_ACCENTS, 8, 0}, {FR_ROW1, 10, 0},
                                    {FR_ROW2, 10, 0}, {FR_ROW3, 8, 0},    {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout FR_LAYOUT{FR_ROWS, 6};
const fui::KeyboardRow FR_LANG_ROWS[] = {{NUM_ROW, 10, 0}, {FR_ACCENTS, 8, 0}, {FR_ROW1, 10, 0},
                                         {FR_ROW2, 10, 0}, {FR_ROW3, 8, 0},    {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout FR_LANG_LAYOUT{FR_LANG_ROWS, 6};
const fui::KeyboardRow FR_SHIFT_ROWS[] = {{NUM_SHIFT_ROW, 10, 0}, {FR_SHIFT_ACCENTS, 8, 0}, {FR_SHIFT_ROW1, 10, 0},
                                          {FR_SHIFT_ROW2, 10, 0}, {FR_SHIFT_ROW3, 8, 0},    {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout FR_SHIFT_LAYOUT{FR_SHIFT_ROWS, 6};
const fui::KeyboardRow FR_SHIFT_LANG_ROWS[] = {{NUM_SHIFT_ROW, 10, 0}, {FR_SHIFT_ACCENTS, 8, 0},
                                               {FR_SHIFT_ROW1, 10, 0}, {FR_SHIFT_ROW2, 10, 0},
                                               {FR_SHIFT_ROW3, 8, 0},  {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout FR_SHIFT_LANG_LAYOUT{FR_SHIFT_LANG_ROWS, 6};

// QWERTZ with ü ö ä at the ends of the letter rows and ß beside m, as on German keyboards.
const fui::KeyboardKey DE_ROW1[] = {K("q", "q", 'q'), K("w", "w", 'w'), K("e", "e", 'e'), K("r", "r", 'r'),
                                    K("t", "t", 't'), K("z", "z", 'z'), K("u", "u", 'u'), K("i", "i", 'i'),
                                    K("o", "o", 'o'), K("p", "p", 'p'), K("ü", "ü", 252)};
const fui::KeyboardKey DE_ROW2[] = {K("a", "a", 'a'), K("s", "s", 's'), K("d", "d", 'd'), K("f", "f", 'f'),
                                    K("g", "g", 'g'), K("h", "h", 'h'), K("j", "j", 'j'), K("k", "k", 'k'),
                                    K("l", "l", 'l'), K("ö", "ö", 246), K("ä", "ä", 228)};
const fui::KeyboardKey DE_ROW3[] = {
    K("y", "y", 'y'), K("x", "x", 'x'), K("c", "c", 'c'),
    K("v", "v", 'v'), K("b", "b", 'b'), K("n", "n", 'n'),
    K("m", "m", 'm'), K("ß", "ß", 223), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardKey DE_SHIFT_ROW1[] = {K("Q", "Q", 'Q'), K("W", "W", 'W'), K("E", "E", 'E'), K("R", "R", 'R'),
                                          K("T", "T", 'T'), K("Z", "Z", 'Z'), K("U", "U", 'U'), K("I", "I", 'I'),
                                          K("O", "O", 'O'), K("P", "P", 'P'), K("Ü", "Ü", 220)};
const fui::KeyboardKey DE_SHIFT_ROW2[] = {K("A", "A", 'A'), K("S", "S", 'S'), K("D", "D", 'D'), K("F", "F", 'F'),
                                          K("G", "G", 'G'), K("H", "H", 'H'), K("J", "J", 'J'), K("K", "K", 'K'),
                                          K("L", "L", 'L'), K("Ö", "Ö", 214), K("Ä", "Ä", 196)};
const fui::KeyboardKey DE_SHIFT_ROW3[] = {
    K("Y", "Y", 'Y'), K("X", "X", 'X'), K("C", "C", 'C'),
    K("V", "V", 'V'), K("B", "B", 'B'), K("N", "N", 'N'),
    K("M", "M", 'M'), K("ß", "ß", 223), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardRow DE_ROWS[] = {
    {NUM_ROW, 10, 0}, {DE_ROW1, 11, 0}, {DE_ROW2, 11, 0}, {DE_ROW3, 9, 0}, {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout DE_LAYOUT{DE_ROWS, 5};
const fui::KeyboardRow DE_LANG_ROWS[] = {
    {NUM_ROW, 10, 0}, {DE_ROW1, 11, 0}, {DE_ROW2, 11, 0}, {DE_ROW3, 9, 0}, {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout DE_LANG_LAYOUT{DE_LANG_ROWS, 5};
const fui::KeyboardRow DE_SHIFT_ROWS[] = {
    {NUM_SHIFT_ROW, 10, 0}, {DE_SHIFT_ROW1, 11, 0}, {DE_SHIFT_ROW2, 11, 0}, {DE_SHIFT_ROW3, 9, 0}, {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout DE_SHIFT_LAYOUT{DE_SHIFT_ROWS, 5};
const fui::KeyboardRow DE_SHIFT_LANG_ROWS[] = {{NUM_SHIFT_ROW, 10, 0},
                                               {DE_SHIFT_ROW1, 11, 0},
                                               {DE_SHIFT_ROW2, 11, 0},
                                               {DE_SHIFT_ROW3, 9, 0},
                                               {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout DE_SHIFT_LANG_LAYOUT{DE_SHIFT_LANG_ROWS, 5};

// QWERTY plus the Italian grave/acute vowels.
const fui::KeyboardKey IT_ACCENTS[] = {K("à", "à", 224), K("è", "è", 232), K("é", "é", 233),
                                       K("ì", "ì", 236), K("ò", "ò", 242), K("ù", "ù", 249)};
const fui::KeyboardKey IT_ROW1[] = {K("q", "q", 'q'), K("w", "w", 'w'), K("e", "e", 'e'), K("r", "r", 'r'),
                                    K("t", "t", 't'), K("y", "y", 'y'), K("u", "u", 'u'), K("i", "i", 'i'),
                                    K("o", "o", 'o'), K("p", "p", 'p')};
const fui::KeyboardKey IT_ROW2[] = {K("a", "a", 'a'), K("s", "s", 's'), K("d", "d", 'd'),
                                    K("f", "f", 'f'), K("g", "g", 'g'), K("h", "h", 'h'),
                                    K("j", "j", 'j'), K("k", "k", 'k'), K("l", "l", 'l')};
const fui::KeyboardKey IT_ROW3[] = {
    K("z", "z", 'z'), K("x", "x", 'x'),  K("c", "c", 'c'),
    K("v", "v", 'v'), K("b", "b", 'b'),  K("n", "n", 'n'),
    K("m", "m", 'm'), K("'", "'", '\''), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardKey IT_SHIFT_ACCENTS[] = {K("À", "À", 192), K("È", "È", 200), K("É", "É", 201),
                                             K("Ì", "Ì", 204), K("Ò", "Ò", 210), K("Ù", "Ù", 217)};
const fui::KeyboardKey IT_SHIFT_ROW1[] = {K("Q", "Q", 'Q'), K("W", "W", 'W'), K("E", "E", 'E'), K("R", "R", 'R'),
                                          K("T", "T", 'T'), K("Y", "Y", 'Y'), K("U", "U", 'U'), K("I", "I", 'I'),
                                          K("O", "O", 'O'), K("P", "P", 'P')};
const fui::KeyboardKey IT_SHIFT_ROW2[] = {K("A", "A", 'A'), K("S", "S", 'S'), K("D", "D", 'D'),
                                          K("F", "F", 'F'), K("G", "G", 'G'), K("H", "H", 'H'),
                                          K("J", "J", 'J'), K("K", "K", 'K'), K("L", "L", 'L')};
const fui::KeyboardKey IT_SHIFT_ROW3[] = {
    K("Z", "Z", 'Z'), K("X", "X", 'X'),  K("C", "C", 'C'),
    K("V", "V", 'V'), K("B", "B", 'B'),  K("N", "N", 'N'),
    K("M", "M", 'M'), K("'", "'", '\''), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardRow IT_ROWS[] = {{NUM_ROW, 10, 0}, {IT_ACCENTS, 6, 0}, {IT_ROW1, 10, 0},
                                    {IT_ROW2, 9, 0},  {IT_ROW3, 9, 0},    {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout IT_LAYOUT{IT_ROWS, 6};
const fui::KeyboardRow IT_LANG_ROWS[] = {{NUM_ROW, 10, 0}, {IT_ACCENTS, 6, 0}, {IT_ROW1, 10, 0},
                                         {IT_ROW2, 9, 0},  {IT_ROW3, 9, 0},    {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout IT_LANG_LAYOUT{IT_LANG_ROWS, 6};
const fui::KeyboardRow IT_SHIFT_ROWS[] = {{NUM_SHIFT_ROW, 10, 0}, {IT_SHIFT_ACCENTS, 6, 0}, {IT_SHIFT_ROW1, 10, 0},
                                          {IT_SHIFT_ROW2, 9, 0},  {IT_SHIFT_ROW3, 9, 0},    {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout IT_SHIFT_LAYOUT{IT_SHIFT_ROWS, 6};
const fui::KeyboardRow IT_SHIFT_LANG_ROWS[] = {{NUM_SHIFT_ROW, 10, 0}, {IT_SHIFT_ACCENTS, 6, 0},
                                               {IT_SHIFT_ROW1, 10, 0}, {IT_SHIFT_ROW2, 9, 0},
                                               {IT_SHIFT_ROW3, 9, 0},  {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout IT_SHIFT_LANG_LAYOUT{IT_SHIFT_LANG_ROWS, 6};

// QWERTY plus every Portuguese accented letter (12 keys, the widest row the panel fits).
const fui::KeyboardKey PT_ACCENTS[] = {K("á", "á", 225), K("â", "â", 226), K("ã", "ã", 227), K("à", "à", 224),
                                       K("é", "é", 233), K("ê", "ê", 234), K("í", "í", 237), K("ó", "ó", 243),
                                       K("ô", "ô", 244), K("õ", "õ", 245), K("ú", "ú", 250), K("ç", "ç", 231)};
const fui::KeyboardKey PT_ROW1[] = {K("q", "q", 'q'), K("w", "w", 'w'), K("e", "e", 'e'), K("r", "r", 'r'),
                                    K("t", "t", 't'), K("y", "y", 'y'), K("u", "u", 'u'), K("i", "i", 'i'),
                                    K("o", "o", 'o'), K("p", "p", 'p')};
const fui::KeyboardKey PT_ROW2[] = {K("a", "a", 'a'), K("s", "s", 's'), K("d", "d", 'd'),
                                    K("f", "f", 'f'), K("g", "g", 'g'), K("h", "h", 'h'),
                                    K("j", "j", 'j'), K("k", "k", 'k'), K("l", "l", 'l')};
const fui::KeyboardKey PT_ROW3[] = {K("z", "z", 'z'), K("x", "x", 'x'),
                                    K("c", "c", 'c'), K("v", "v", 'v'),
                                    K("b", "b", 'b'), K("n", "n", 'n'),
                                    K("m", "m", 'm'), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardKey PT_SHIFT_ACCENTS[] = {K("Á", "Á", 193), K("Â", "Â", 194), K("Ã", "Ã", 195), K("À", "À", 192),
                                             K("É", "É", 201), K("Ê", "Ê", 202), K("Í", "Í", 205), K("Ó", "Ó", 211),
                                             K("Ô", "Ô", 212), K("Õ", "Õ", 213), K("Ú", "Ú", 218), K("Ç", "Ç", 199)};
const fui::KeyboardKey PT_SHIFT_ROW1[] = {K("Q", "Q", 'Q'), K("W", "W", 'W'), K("E", "E", 'E'), K("R", "R", 'R'),
                                          K("T", "T", 'T'), K("Y", "Y", 'Y'), K("U", "U", 'U'), K("I", "I", 'I'),
                                          K("O", "O", 'O'), K("P", "P", 'P')};
const fui::KeyboardKey PT_SHIFT_ROW2[] = {K("A", "A", 'A'), K("S", "S", 'S'), K("D", "D", 'D'),
                                          K("F", "F", 'F'), K("G", "G", 'G'), K("H", "H", 'H'),
                                          K("J", "J", 'J'), K("K", "K", 'K'), K("L", "L", 'L')};
const fui::KeyboardKey PT_SHIFT_ROW3[] = {
    K("Z", "Z", 'Z'), K("X", "X", 'X'),
    K("C", "C", 'C'), K("V", "V", 'V'),
    K("B", "B", 'B'), K("N", "N", 'N'),
    K("M", "M", 'M'), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardRow PT_ROWS[] = {{NUM_ROW, 10, 0}, {PT_ACCENTS, 12, 0}, {PT_ROW1, 10, 0},
                                    {PT_ROW2, 9, 0},  {PT_ROW3, 8, 0},     {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout PT_LAYOUT{PT_ROWS, 6};
const fui::KeyboardRow PT_LANG_ROWS[] = {{NUM_ROW, 10, 0}, {PT_ACCENTS, 12, 0}, {PT_ROW1, 10, 0},
                                         {PT_ROW2, 9, 0},  {PT_ROW3, 8, 0},     {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout PT_LANG_LAYOUT{PT_LANG_ROWS, 6};
const fui::KeyboardRow PT_SHIFT_ROWS[] = {{NUM_SHIFT_ROW, 10, 0}, {PT_SHIFT_ACCENTS, 12, 0}, {PT_SHIFT_ROW1, 10, 0},
                                          {PT_SHIFT_ROW2, 9, 0},  {PT_SHIFT_ROW3, 8, 0},     {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout PT_SHIFT_LAYOUT{PT_SHIFT_ROWS, 6};
const fui::KeyboardRow PT_SHIFT_LANG_ROWS[] = {{NUM_SHIFT_ROW, 10, 0}, {PT_SHIFT_ACCENTS, 12, 0},
                                               {PT_SHIFT_ROW1, 10, 0}, {PT_SHIFT_ROW2, 9, 0},
                                               {PT_SHIFT_ROW3, 8, 0},  {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout PT_SHIFT_LANG_LAYOUT{PT_SHIFT_LANG_ROWS, 6};

// QWERTY plus the Dutch accents and diaereses; ' for 's and auto's.
const fui::KeyboardKey NL_ACCENTS[] = {K("é", "é", 233), K("ë", "ë", 235), K("è", "è", 232), K("ï", "ï", 239),
                                       K("ö", "ö", 246), K("ü", "ü", 252), K("ó", "ó", 243), K("á", "á", 225)};
const fui::KeyboardKey NL_ROW1[] = {K("q", "q", 'q'), K("w", "w", 'w'), K("e", "e", 'e'), K("r", "r", 'r'),
                                    K("t", "t", 't'), K("y", "y", 'y'), K("u", "u", 'u'), K("i", "i", 'i'),
                                    K("o", "o", 'o'), K("p", "p", 'p')};
const fui::KeyboardKey NL_ROW2[] = {K("a", "a", 'a'), K("s", "s", 's'), K("d", "d", 'd'),
                                    K("f", "f", 'f'), K("g", "g", 'g'), K("h", "h", 'h'),
                                    K("j", "j", 'j'), K("k", "k", 'k'), K("l", "l", 'l')};
const fui::KeyboardKey NL_ROW3[] = {
    K("z", "z", 'z'), K("x", "x", 'x'),  K("c", "c", 'c'),
    K("v", "v", 'v'), K("b", "b", 'b'),  K("n", "n", 'n'),
    K("m", "m", 'm'), K("'", "'", '\''), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardKey NL_SHIFT_ACCENTS[] = {K("É", "É", 201), K("Ë", "Ë", 203), K("È", "È", 200), K("Ï", "Ï", 207),
                                             K("Ö", "Ö", 214), K("Ü", "Ü", 220), K("Ó", "Ó", 211), K("Á", "Á", 193)};
const fui::KeyboardKey NL_SHIFT_ROW1[] = {K("Q", "Q", 'Q'), K("W", "W", 'W'), K("E", "E", 'E'), K("R", "R", 'R'),
                                          K("T", "T", 'T'), K("Y", "Y", 'Y'), K("U", "U", 'U'), K("I", "I", 'I'),
                                          K("O", "O", 'O'), K("P", "P", 'P')};
const fui::KeyboardKey NL_SHIFT_ROW2[] = {K("A", "A", 'A'), K("S", "S", 'S'), K("D", "D", 'D'),
                                          K("F", "F", 'F'), K("G", "G", 'G'), K("H", "H", 'H'),
                                          K("J", "J", 'J'), K("K", "K", 'K'), K("L", "L", 'L')};
const fui::KeyboardKey NL_SHIFT_ROW3[] = {
    K("Z", "Z", 'Z'), K("X", "X", 'X'),  K("C", "C", 'C'),
    K("V", "V", 'V'), K("B", "B", 'B'),  K("N", "N", 'N'),
    K("M", "M", 'M'), K("'", "'", '\''), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardRow NL_ROWS[] = {{NUM_ROW, 10, 0}, {NL_ACCENTS, 8, 0}, {NL_ROW1, 10, 0},
                                    {NL_ROW2, 9, 0},  {NL_ROW3, 9, 0},    {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout NL_LAYOUT{NL_ROWS, 6};
const fui::KeyboardRow NL_LANG_ROWS[] = {{NUM_ROW, 10, 0}, {NL_ACCENTS, 8, 0}, {NL_ROW1, 10, 0},
                                         {NL_ROW2, 9, 0},  {NL_ROW3, 9, 0},    {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout NL_LANG_LAYOUT{NL_LANG_ROWS, 6};
const fui::KeyboardRow NL_SHIFT_ROWS[] = {{NUM_SHIFT_ROW, 10, 0}, {NL_SHIFT_ACCENTS, 8, 0}, {NL_SHIFT_ROW1, 10, 0},
                                          {NL_SHIFT_ROW2, 9, 0},  {NL_SHIFT_ROW3, 9, 0},    {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout NL_SHIFT_LAYOUT{NL_SHIFT_ROWS, 6};
const fui::KeyboardRow NL_SHIFT_LANG_ROWS[] = {{NUM_SHIFT_ROW, 10, 0}, {NL_SHIFT_ACCENTS, 8, 0},
                                               {NL_SHIFT_ROW1, 10, 0}, {NL_SHIFT_ROW2, 9, 0},
                                               {NL_SHIFT_ROW3, 9, 0},  {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout NL_SHIFT_LANG_LAYOUT{NL_SHIFT_LANG_ROWS, 6};

// QWERTY plus the nine Polish letters.
const fui::KeyboardKey PL_ACCENTS[] = {K("ą", "ą", 261), K("ć", "ć", 263), K("ę", "ę", 281),
                                       K("ł", "ł", 322), K("ń", "ń", 324), K("ó", "ó", 243),
                                       K("ś", "ś", 347), K("ź", "ź", 378), K("ż", "ż", 380)};
const fui::KeyboardKey PL_ROW1[] = {K("q", "q", 'q'), K("w", "w", 'w'), K("e", "e", 'e'), K("r", "r", 'r'),
                                    K("t", "t", 't'), K("y", "y", 'y'), K("u", "u", 'u'), K("i", "i", 'i'),
                                    K("o", "o", 'o'), K("p", "p", 'p')};
const fui::KeyboardKey PL_ROW2[] = {K("a", "a", 'a'), K("s", "s", 's'), K("d", "d", 'd'),
                                    K("f", "f", 'f'), K("g", "g", 'g'), K("h", "h", 'h'),
                                    K("j", "j", 'j'), K("k", "k", 'k'), K("l", "l", 'l')};
const fui::KeyboardKey PL_ROW3[] = {K("z", "z", 'z'), K("x", "x", 'x'),
                                    K("c", "c", 'c'), K("v", "v", 'v'),
                                    K("b", "b", 'b'), K("n", "n", 'n'),
                                    K("m", "m", 'm'), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardKey PL_SHIFT_ACCENTS[] = {K("Ą", "Ą", 260), K("Ć", "Ć", 262), K("Ę", "Ę", 280),
                                             K("Ł", "Ł", 321), K("Ń", "Ń", 323), K("Ó", "Ó", 211),
                                             K("Ś", "Ś", 346), K("Ź", "Ź", 377), K("Ż", "Ż", 379)};
const fui::KeyboardKey PL_SHIFT_ROW1[] = {K("Q", "Q", 'Q'), K("W", "W", 'W'), K("E", "E", 'E'), K("R", "R", 'R'),
                                          K("T", "T", 'T'), K("Y", "Y", 'Y'), K("U", "U", 'U'), K("I", "I", 'I'),
                                          K("O", "O", 'O'), K("P", "P", 'P')};
const fui::KeyboardKey PL_SHIFT_ROW2[] = {K("A", "A", 'A'), K("S", "S", 'S'), K("D", "D", 'D'),
                                          K("F", "F", 'F'), K("G", "G", 'G'), K("H", "H", 'H'),
                                          K("J", "J", 'J'), K("K", "K", 'K'), K("L", "L", 'L')};
const fui::KeyboardKey PL_SHIFT_ROW3[] = {
    K("Z", "Z", 'Z'), K("X", "X", 'X'),
    K("C", "C", 'C'), K("V", "V", 'V'),
    K("B", "B", 'B'), K("N", "N", 'N'),
    K("M", "M", 'M'), KS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
const fui::KeyboardRow PL_ROWS[] = {{NUM_ROW, 10, 0}, {PL_ACCENTS, 9, 0}, {PL_ROW1, 10, 0},
                                    {PL_ROW2, 9, 0},  {PL_ROW3, 8, 0},    {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout PL_LAYOUT{PL_ROWS, 6};
const fui::KeyboardRow PL_LANG_ROWS[] = {{NUM_ROW, 10, 0}, {PL_ACCENTS, 9, 0}, {PL_ROW1, 10, 0},
                                         {PL_ROW2, 9, 0},  {PL_ROW3, 8, 0},    {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout PL_LANG_LAYOUT{PL_LANG_ROWS, 6};
const fui::KeyboardRow PL_SHIFT_ROWS[] = {{NUM_SHIFT_ROW, 10, 0}, {PL_SHIFT_ACCENTS, 9, 0}, {PL_SHIFT_ROW1, 10, 0},
                                          {PL_SHIFT_ROW2, 9, 0},  {PL_SHIFT_ROW3, 8, 0},    {BOTTOM_ROW, 4, 0}};
const fui::KeyboardLayout PL_SHIFT_LAYOUT{PL_SHIFT_ROWS, 6};
const fui::KeyboardRow PL_SHIFT_LANG_ROWS[] = {{NUM_SHIFT_ROW, 10, 0}, {PL_SHIFT_ACCENTS, 9, 0},
                                               {PL_SHIFT_ROW1, 10, 0}, {PL_SHIFT_ROW2, 9, 0},
                                               {PL_SHIFT_ROW3, 8, 0},  {BOTTOM_LANG_ROW, 5, 0}};
const fui::KeyboardLayout PL_SHIFT_LANG_LAYOUT{PL_SHIFT_LANG_ROWS, 6};

#undef K
#undef KA
#undef KS

}  // namespace

const fui::KeyboardLayout* letters(const Layout layout, const bool shifted, const bool langKey) {
  switch (layout) {
    case Layout::Fr:
      if (shifted) return langKey ? &FR_SHIFT_LANG_LAYOUT : &FR_SHIFT_LAYOUT;
      return langKey ? &FR_LANG_LAYOUT : &FR_LAYOUT;
    case Layout::De:
      if (shifted) return langKey ? &DE_SHIFT_LANG_LAYOUT : &DE_SHIFT_LAYOUT;
      return langKey ? &DE_LANG_LAYOUT : &DE_LAYOUT;
    case Layout::It:
      if (shifted) return langKey ? &IT_SHIFT_LANG_LAYOUT : &IT_SHIFT_LAYOUT;
      return langKey ? &IT_LANG_LAYOUT : &IT_LAYOUT;
    case Layout::Pt:
      if (shifted) return langKey ? &PT_SHIFT_LANG_LAYOUT : &PT_SHIFT_LAYOUT;
      return langKey ? &PT_LANG_LAYOUT : &PT_LAYOUT;
    case Layout::Nl:
      if (shifted) return langKey ? &NL_SHIFT_LANG_LAYOUT : &NL_SHIFT_LAYOUT;
      return langKey ? &NL_LANG_LAYOUT : &NL_LAYOUT;
    case Layout::Pl:
      if (shifted) return langKey ? &PL_SHIFT_LANG_LAYOUT : &PL_SHIFT_LAYOUT;
      return langKey ? &PL_LANG_LAYOUT : &PL_LAYOUT;
    case Layout::None:
    default:
      return nullptr;
  }
}

}  // namespace eu_keyboard
