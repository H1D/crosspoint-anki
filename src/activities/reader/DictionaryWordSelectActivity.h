#pragma once

#include <Epub/Page.h>
#include <I18n.h>

#include <memory>
#include <string>

#include "activities/Activity.h"
#include "anki/AnkiAddNoteActivity.h"
#include "util/Dictionary.h"
#include "vocabulary/VocabularyStore.h"

// What an Add to Anki note needs beyond the looked-up text.
struct AnkiLookupNote {
  bool anki = false;                  // the definition offers Add to Anki
  bool addWithoutDefinition = false;  // no dictionary or a miss opens the add screen directly
  std::string bookTitle;
  std::string sentence;
};

// Controller for dictionary lookup: renders the background page, runs the
// lookup against the book's StarDict dictionary, and either opens
// DictionaryDefinitionActivity or displays a status popup (Looking up,
// Indexing, Not found, Error).
//
// With AnkiLookupNote::anki the definition view also offers "Add to Anki" with a note
// draft: the looked-up text, the sentence around it, and the dictionary's
// translation. AnkiLookupNote::addWithoutDefinition (the Add to Anki selection) needs no
// dictionary: with none, or when the lookup misses, AnkiAddNoteActivity opens
// directly with an empty translation. `dictionaryName` is the folder picked
// for the book (DictionaryRegistry::pickForBook), "" for none.
class DictionaryWordSelectActivity final : public Activity {
 public:
  explicit DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        std::unique_ptr<Page> page, int marginLeft, int marginTop,
                                        std::string lookupText = {}, VocabularyContext vocabulary = {},
                                        std::string dictionaryName = {}, AnkiLookupNote note = {})
      : Activity("DictionaryWordSelect", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop),
        lookupText(std::move(lookupText)),
        vocabulary(std::move(vocabulary)),
        dictionaryName(std::move(dictionaryName)),
        note(std::move(note)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Popup : uint8_t { None, Busy, NotFound, Error };

  void performLookup();
  // Draft for the looked-up text; `definition` (raw, as looked up) supplies
  // the translation, `headword` the dictionary form when it differs.
  AnkiNoteDraft noteDraft(const std::string* definition, std::string headword) const;
  void openAnkiAdd(AnkiNoteDraft draft);

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  int fontId = 0;

  std::string lookupText;
  VocabularyContext vocabulary;
  const std::string dictionaryName;
  const AnkiLookupNote note;
  bool lookupPending = false;
  Dictionary dict;
  bool dictOpenAttempted = false;
  bool dictOpenOk = false;
  bool dictNeedsIndex = false;

  Popup popup = Popup::None;
  StrId popupMsg = StrId::STR_DICT_NOT_FOUND;
  unsigned long popupTime = 0;
};
