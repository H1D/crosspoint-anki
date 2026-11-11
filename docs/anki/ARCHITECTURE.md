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
  AnkiHtml.*             Anki card HTML -> the same markup (AnkiConnect path)
  AnkiTypeAnswer.*       type-in cards: expected answer from a field/cloze, typed-vs-expected diff runs
  AnkiJournal.*          pending reviews (reviews.jsonl)
  AnkiNoteQueue.*        pending add-word notes (notes.jsonl) + ankinote:: builders
  AnkiCardCache.*        cached queue (cards.jsonl + cache.json)
  AnkiStats.*            reviews per day for the dashboard (stats.json): wire parsers, day anchor,
                         merge with the journal, streak, heatmap levels
  AnkiVocabulary.*       looked-up words (upstream's /Vocabulary.txt): record parser, row key, note fields,
                         window scan from the "vocabDone" marker
  AnkiSyncEngine.*       one sync session per account (exchange, stats, notes, decks); dispatches per backend
  AnkiConnectClient.*    the same steps spoken as AnkiConnect actions (Anki desktop)
test/anki_client/        gtest suite: cmake -S test -B build/test && cmake --build build/test --target AnkiClientTest
scripts/anki/mock_ankiconnect.py  stand-in AnkiConnect for simulator runs (python3 scripts/anki/mock_ankiconnect.py 8777)

src/anki/                firmware side
  AnkiStorageFs.*        AnkiFs over HalStorage      -> AnkiStorageFs::instance()
  AnkiSecureHttp.*       AnkiHttp over SecureHttpClient -> AnkiSecureHttp::instance()
  AnkiDevice.*           ankidevice::deviceId(), nextClientId(), nowEpoch() (RTC, for answered_at),
                         wallEpoch() (RTC or SNTP clock, for days on screen and the engine clock)
  AnkiAccountStore.*     ANKI_STORE singleton, /.crosspoint/anki.json (accounts + the "vocabDone" marker)
  settings/              account list / editor / deck picker activities   (agent: settings)
  review/                AnkiDashboardActivity (app entry), AnkiAccountPickerActivity,
                         AnkiReviewActivity, AnkiSyncActivity              (agent: review)
  AnkiAddNoteActivity.*  add-word modal                                    (agent: add-word)
  AnkiVocabularyActivity.*  Send Words to Anki table (reader menu)
```

## Using the core from an activity

```cpp
#include <AnkiSyncEngine.h>
#include "anki/AnkiAccountStore.h"
#include "anki/AnkiDevice.h"
#include "anki/AnkiSecureHttp.h"
#include "anki/AnkiStorageFs.h"

AnkiSyncEngine engine(AnkiStorageFs::instance(), AnkiSecureHttp::instance(), ankidevice::wallEpoch);
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

Dashboard numbers (read only; one call fills everything the screen draws):

```cpp
// `study` is an ankistats::Study activity member (~400 B), never a stack local.
ankistats::loadStudy(AnkiStorageFs::instance(), accountId, ankidevice::wallEpoch(), study);
// study.toReview / reviewedToday / streak / pending / syncedDaysAgo, study.levelOn(day) for the heatmap
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

SD layout per account, `/.crosspoint/anki/<id>/` (`AnkiPaths.h`):

```
cards.jsonl    cached queue, one card per line        engine (sync)
cache.json     counts + fetch time of the cache       engine (sync)
reviews.jsonl  grades not yet acknowledged            review screen appends, engine rewrites
notes.jsonl    add-word notes not yet sent            add-word appends, engine rewrites
decks.json     deck list from the last sync           engine (sync)
stats.json     reviews per day from the last fetch    engine (sync, and after every acked exchange)
```

Backends: `account.backend` (`AnkiBackend::AnkiDo` or `AnkiConnect`) decides
which server the engine talks to. Activities never branch on it; the only
visible differences are the editor labels (URL / token vs. AnkiConnect URL /
API key) and empty `card.next[]` labels for AnkiConnect cards.

## Invariants every agent must keep

- **RAM**: never read cards.jsonl whole; never keep more than one AnkiCard plus
  its runs alive. Responses are streamed by the engine. ESP32-C3 has ~380 KB.
- **Wi-Fi sessions end in a silent restart** (`SilentRestart.h`), like
  KOReaderSyncActivity. The Anki app restarts back onto its dashboard after a
  manual sync (`silentRestartToAnki()` -> `goToAnki(false)` in main.cpp);
  exit- and sleep-triggered syncs restart to home.
- **Review history**: `stats.json` counts what the server has; the journal
  holds what it has not. `AnkiSyncEngine::exchange` refetches stats whenever
  it acknowledged a review, so the two never hold the same review. Only the
  engine writes `stats.json`; the dashboard only reads.
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
