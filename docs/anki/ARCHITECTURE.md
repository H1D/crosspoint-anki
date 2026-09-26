# crosspoint-anki: architecture and contracts

Read this before touching any Anki code. Decisions and their reasons are in
[DECISIONS.md](DECISIONS.md); this file is the map of the pieces and the
contracts between them.

## Layers

```
lib/AnkiClient/          host-testable core (no Arduino types, no ArduinoJson)
  AnkiTypes.h            AnkiAccount, AnkiCard, AnkiDeck, AnkiCounts, AnkiEase
  AnkiFs.h / AnkiHttp.h  seams implemented by the firmware (below) and by tests
  AnkiPaths.h            SD layout under /.crosspoint/anki/<account id>/
  AnkiJson.*             streaming JSON reader + string quoting
  AnkiMarkup.*           AnkiDo text markup -> styled runs; bidi-control stripping
  AnkiJournal.*          pending reviews (reviews.jsonl)
  AnkiNoteQueue.*        pending add-word notes (notes.jsonl) + ankinote:: builders
  AnkiCardCache.*        cached queue (cards.jsonl + cache.json)
  AnkiSyncEngine.*       one sync session per account (exchange, notes, decks)
test/anki_client/        gtest suite: cmake -S test -B build/test && cmake --build build/test --target AnkiClientTest

src/anki/                firmware side
  AnkiStorageFs.*        AnkiFs over HalStorage      -> AnkiStorageFs::instance()
  AnkiSecureHttp.*       AnkiHttp over SecureHttpClient -> AnkiSecureHttp::instance()
  AnkiDevice.*           ankidevice::deviceId(), nextClientId(), nowEpoch()
  AnkiAccountStore.*     ANKI_STORE singleton, /.crosspoint/anki.json
  settings/              account list / editor / deck picker activities   (agent: settings)
  review/                AnkiReviewActivity, AnkiSyncActivity              (agent: review)
  AnkiAddNoteActivity.*  add-word modal                                    (agent: add-word)
```

## Using the core from an activity

```cpp
#include <AnkiSyncEngine.h>
#include "anki/AnkiAccountStore.h"
#include "anki/AnkiDevice.h"
#include "anki/AnkiSecureHttp.h"
#include "anki/AnkiStorageFs.h"

AnkiSyncEngine engine(AnkiStorageFs::instance(), AnkiSecureHttp::instance(), ankidevice::nowEpoch);
const int reviewIdx = ANKI_STORE.getReviewAccountIndex();
for (size_t i = 0; i < ANKI_STORE.getCount(); i++) {
  const AnkiAccount& a = ANKI_STORE.getAccounts()[i];
  if (!a.enabled) continue;
  const bool wantCards = static_cast<int>(i) == reviewIdx;
  if (!wantCards && !engine.hasPending(a)) continue;   // nothing to push
  AnkiSyncEngine::Result r = engine.syncAccount(a, wantCards);
  // r.ok / r.error / r.authFailed / r.cardsFetched / r.reviewsAcked / r.notesAcked
}
```

Grading a card (review app):

```cpp
AnkiJournal journal(AnkiStorageFs::instance(), ankipaths::reviewsFile(account.id));
journal.append({ankidevice::nextClientId(), card.cardId, static_cast<uint8_t>(AnkiEase::Good), ankidevice::nowEpoch()});
```

Queuing a note (add-word):

```cpp
AnkiNoteQueue queue(AnkiStorageFs::instance(), ankipaths::notesFile(account.id));
AnkiNoteQueue::Note n;
n.clientId = ankidevice::nextClientId();
n.deck = deck; n.model = account.model;
n.front = ankinote::frontHtml(word, sentence);
n.back = translation;                       // may be empty
n.tags = {"crosspoint", ankinote::bookTag(bookTitle)};
queue.append(n);
ANKI_STORE.setLastDeck(accountIndex, deck);
```

Reading the cache (review app):

```cpp
AnkiCardCache cache(AnkiStorageFs::instance(), ankipaths::cardsFile(id), ankipaths::cacheMetaFile(id));
const size_t n = cache.count();
AnkiCard card; cache.load(i, card);          // one line parsed at a time
// skip cards whose id is in journal.gradedCardIds()
auto runs = ankimarkup::parse(card.q);       // bold/italic/cloze/newline runs; "[image]" placeholders
```

Deck names for pickers: `engine.loadDecks(account.id)` (cached by the last sync).

## Invariants every agent must keep

- **RAM**: never read cards.jsonl whole; never keep more than one AnkiCard plus
  its runs alive. Responses are streamed by the engine. ESP32-C3 has ~380 KB.
- **Wi-Fi sessions end in a silent restart** (`SilentRestart.h`), like
  KOReaderSyncActivity. The review app restarts back into itself after a manual
  sync (`silentRestartToAnki()`, added by the review agent in main.cpp);
  exit- and sleep-triggered syncs restart to home.
- **Tokens never leave the device** except as the Authorization header to the
  account's own URL. The web API reports `hasToken`, never the token.
- **All user-facing text through `tr(STR_...)`**; keys are predefined at the
  end of `lib/I18n/translations/english.yaml` (all prefixed `STR_ANKI_`). The
  YAML format allows no comments. Add a key there only if none fits, then run
  `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/`; other locales
  are filled by the translation pass.
- **Formatting**: `./bin/clang-format-fix -g` before committing.
- **No bare `new`**: `makeUniqueNoThrow` (lib/Memory) for activities and buffers.

## Ownership (parallel implementation)

| Area | Files | Owner |
|---|---|---|
| Settings screens + web UI | `src/anki/settings/*`, `SettingsActivity.{h,cpp}` (action enum + row + dispatch), `CrossPointWebServer.{h,cpp}` (`/api/anki*`), `src/network/html/SettingsPage.html` | settings agent |
| Review app + sync | `src/anki/review/*`, `src/main.cpp` (restart target + sleep-entry sync), `SilentRestart.h`, `ActivityManager.{h,cpp}` (`goToAnki`, `HomeMenuItem::ANKI`), `HomeActivity.{h,cpp}`, `CoverGridHomeUi.*` if needed | review agent |
| Add word | `src/anki/AnkiAddNoteActivity.*`, `DictionaryWordSelectActivity.{h,cpp}` (second mode), `EpubReaderActivity.{h,cpp}`, `EpubReaderMenuActivity.{h,cpp}`, `CrossPointSettings.h` (LP_MENU_ANKI), `SettingsList.h` (long-press values) | add-word agent |
| Release + flasher | `.github/workflows/*`, `src/network/OtaUpdater.cpp` (repo constant), `platformio.ini` (version, simulator env), `site/*`, `README.md` | release agent |
| Translations | `lib/I18n/translations/*.yaml` except english | translation agent (after the others) |

Nobody else edits another owner's files. If you need a change in someone
else's file, message that agent (SendMessage) with the exact edit.
