#include "DictionaryWordSelectActivity.h"

#include <AnkiNoteQueue.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cctype>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "components/UITheme.h"
#include "util/HtmlToPlainText.h"
#include "util/WikDict.h"

namespace {

constexpr unsigned long POPUP_DURATION_MS = 1500;
// Longest dictionary definition kept as a note's Back field.
constexpr size_t MAX_TRANSLATION_BYTES = 600;
// Definition prefix the plain-text Back field is made from: markup included,
// ample for 600 bytes of text, and a bounded copy even for a 64KB entry.
constexpr size_t MAX_TRANSLATION_SOURCE_BYTES = 8 * MAX_TRANSLATION_BYTES;

void indexBuildYield(void*) { vTaskDelay(1); }

// Plain-text note field from a StarDict definition: HTML stripped, whitespace
// runs collapsed (wrappedText only breaks on spaces), cut at a UTF-8 boundary.
std::string definitionToTranslation(const std::string& definition) {
  std::string source = definition.substr(0, MAX_TRANSLATION_SOURCE_BYTES);
  std::replace(source.begin(), source.end(), '\0', '\n');  // multi-type separators
  std::string text = htmlToPlainText(source, ' ');
  source = std::string();
  std::string out;
  out.reserve(std::min(text.size(), MAX_TRANSLATION_BYTES));
  bool pendingSpace = false;
  for (const char c : text) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) {
      out.push_back(' ');
      pendingSpace = false;
    }
    out.push_back(c);
  }
  if (out.size() > MAX_TRANSLATION_BYTES) {
    size_t cut = MAX_TRANSLATION_BYTES;
    while (cut > 0 && (static_cast<uint8_t>(out[cut]) & 0xC0) == 0x80) cut--;
    out.resize(cut);
  }
  return out;
}

}  // namespace

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  lookupPending = true;
  popupMsg = StrId::STR_DICT_LOOKING_UP;
  popup = Popup::Busy;
  requestUpdate();
}

void DictionaryWordSelectActivity::performLookup() {
  if (dictionaryName.empty() && note.addWithoutDefinition) {
    openAnkiAdd(noteDraft(nullptr, {}));
    return;
  }
  {
    RenderLock lock;
    popupMsg = StrId::STR_DICT_LOOKING_UP;
    popup = Popup::Busy;
  }
  if (!dictOpenAttempted) {
    dictOpenAttempted = true;
    dictOpenOk = dict.open(dictionaryName.c_str());
    // needsIndex() opens and validates the .qidx sidecar, so ask it once per
    // open rather than once per word: the answer only changes when we build
    // the sidecar ourselves, which is handled below.
    dictNeedsIndex = dictOpenOk && dict.needsIndex();
  }
  {
    RenderLock lock;
    popupMsg = dictNeedsIndex ? StrId::STR_DICT_INDEXING : StrId::STR_DICT_LOOKING_UP;
  }
  requestUpdateAndWait();  // paint the page + busy popup before blocking on SD

  bool ok = dictOpenOk;
  Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
  if (ok && dictNeedsIndex) {
    ok = dict.buildIndex(&indexBuildYield, nullptr, &indexResult);
    dictNeedsIndex = !ok;  // a successful build leaves the sidecar fresh; a failed one retries
  }

  std::string definition;
  std::string headword;
  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
  const bool found = ok && dict.lookup(lookupText.c_str(), definition, headword, &result);

  if (found) {
    {
      RenderLock lock;
      popup = Popup::None;
      page.reset();
    }
    if (SETTINGS.vocabularyHistoryEnabled &&
        !VocabularyStore::append(vocabulary, lookupText, headword, definition, dict.definitionsAreHtml())) {
      LOG_ERR("VOCAB", "Could not save successful dictionary lookup");
    }
    AnkiNoteDraft draft;
    if (note.anki) draft = noteDraft(&definition, headword);
    // WikDict entries read better as "gloss — translations" per sense than
    // laid out as delivered (nested lists, IPA the fonts cannot draw).
    if (dict.definitionsAreHtml()) {
      std::string compact = WikDict::compactHtml(definition);
      if (!compact.empty()) definition = std::move(compact);
    }
    auto activity = makeUniqueNoThrow<DictionaryDefinitionActivity>(
        renderer, mappedInput, std::move(headword), std::move(definition), dict.definitionsAreHtml(), std::move(draft));
    if (!activity || !activity->enableLookup(dict, std::move(lookupText))) {
      LOG_ERR("DICT", "OOM: definition activity");
      popup = Popup::Error;
      popupMsg = StrId::STR_DICT_LOW_MEMORY;
      popupTime = millis();
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(activity), [this](const ActivityResult& result) {
      setResult(ActivityResult{result});
      finish();
    });
    return;
  }
  // Adding to Anki does not need the dictionary: a miss or a failure leaves
  // the Back field to fill in on the phone.
  if (note.addWithoutDefinition) {
    {
      RenderLock lock;
      popup = Popup::None;
    }
    openAnkiAdd(noteDraft(nullptr, {}));
    return;
  }
  // Name the failure: a genuine miss is "Not found"; a word that WAS found but
  // couldn't be read is a real error — and we distinguish decompression from a
  // low-memory allocation from a generic read error.
  if (!ok) {
    popup = Popup::Error;
    // An index build allocates a scan buffer, so it fails the same way lookups
    // do on a fragmented heap — name that rather than a generic error.
    switch (indexResult) {
      case Dictionary::IndexResult::LowMemory:
        popupMsg = StrId::STR_DICT_LOW_MEMORY;
        break;
      case Dictionary::IndexResult::ReadError:
        popupMsg = StrId::STR_DICT_READ_FAILED;
        break;
      case Dictionary::IndexResult::Ok:
      default:
        popupMsg = StrId::STR_DICT_ERROR;  // dict.open() failed, not the index
        break;
    }
  } else {
    switch (result) {
      case Dictionary::LookupResult::Decompress:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_DECOMPRESS_ERROR;
        break;
      case Dictionary::LookupResult::LowMemory:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_LOW_MEMORY;
        break;
      case Dictionary::LookupResult::ReadError:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_READ_FAILED;
        break;
      case Dictionary::LookupResult::NotFound:
      default:
        popup = Popup::NotFound;
        popupMsg = StrId::STR_DICT_NOT_FOUND;
        break;
    }
  }
  popupTime = millis();
  requestUpdate();
}

AnkiNoteDraft DictionaryWordSelectActivity::noteDraft(const std::string* definition, std::string headword) const {
  AnkiNoteDraft draft;
  draft.word = ankinote::cleanWord(lookupText);
  if (draft.word.empty()) return draft;
  draft.bookTitle = note.bookTitle;
  draft.sentence = note.sentence;
  if (definition) {
    // Just the translated words when the HTML has WikDict's layout, else the whole entry as text.
    if (dict.definitionsAreHtml()) draft.translation = WikDict::shortTranslation(*definition);
    if (draft.translation.empty()) draft.translation = definitionToTranslation(*definition);
  }
  // Only a different word is worth showing: "Walk" for "walk" is not.
  if (Dictionary::cleanWord(headword.c_str()) != Dictionary::cleanWord(draft.word.c_str())) {
    draft.headword = std::move(headword);
  }
  return draft;
}

void DictionaryWordSelectActivity::openAnkiAdd(AnkiNoteDraft draft) {
  auto modal =
      draft.word.empty() ? nullptr : makeUniqueNoThrow<AnkiAddNoteActivity>(renderer, mappedInput, std::move(draft));
  if (!modal) {
    LOG_ERR("DICT", "No add screen (empty word or OOM)");
    popup = Popup::Error;
    popupMsg = StrId::STR_ANKI_QUEUE_FAILED;
    popupTime = millis();
    requestUpdate();
    return;
  }
  // A queued note returns to the book; Back picks another word.
  startActivityForResult(std::move(modal), [this](const ActivityResult& added) {
    ActivityResult result;
    result.isCancelled = !added.isCancelled;
    setResult(std::move(result));
    finish();
  });
}

void DictionaryWordSelectActivity::loop() {
  if (lookupPending) {
    lookupPending = false;
    performLookup();
    return;
  }
  if (popup == Popup::NotFound || popup == Popup::Error) {
    if (millis() - popupTime >= POPUP_DURATION_MS) {
      result.isCancelled = popup == Popup::Error;
      finish();
    }
  }
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  if (page) page->render(renderer, fontId, marginLeft, marginTop);
  scope.endScanAndPrewarm();
  if (page) page->render(renderer, fontId, marginLeft, marginTop);

  if (popup != Popup::None) {
    GUI.drawPopup(renderer, I18N.get(popupMsg));
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
