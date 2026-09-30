# crosspoint-anki: design decisions

This fork adds Anki flashcard review and "add word from a book" to CrossPoint
Reader, backed by an [AnkiDo](https://github.com/H1D/AnkiDo) server or by
AnkiConnect in Anki desktop. This file
is the record of every decision that is not derivable from the code. Newest
entries at the bottom of each section. Dates are when the decision was made.

## Why a fork, not a plugin (2026-09-26)

Upstream's SD-plugin system (PR #3114, unmerged) runs no code on the device: a
plugin is browser JavaScript, a declarative "browse and download" catalog, or an
HTTP template fired by a fixed list of events. A review screen (show card, flip,
grade) needs firmware code. Upstream's SCOPE.md rules interactive apps out, so
this stays a fork. The fork rebases on upstream `develop`; Anki code is kept in
`lib/AnkiClient/` and `src/anki/` with small touch points elsewhere so rebases
stay cheap.

## Server (2026-09-26)

- **AnkiDo is the only backend** (reversed 2026-09-28, see "AnkiConnect
  backend" below). The device never holds AnkiWeb credentials; it holds an
  AnkiDo base URL, a profile name and a bearer token with scopes
  `read,review,add`. `sync` scope is not needed: `POST /exchange` syncs inline.
- Development instance: Docker Compose in `ankido-dev/` next to the clone (not
  in this repo), published on host port 8766 (8765 was taken on the dev
  machine), profile `test`. The AnkiWeb test login lives only in its
  `secrets/test.env` (mode 0600).

## Accounts (2026-09-27)

- One account entry = `name, url, profile, token, enabled, model, decks[],
  cacheSize`. Several entries may point at one AnkiDo instance.
  Flat list, max 8 entries (same ceiling as OPDS servers).
- Exactly one account is the **review account** (`reviewAccount` index). Add-word
  can target any enabled account; review uses only that one. Chosen in
  settings, not at app start.
- Stored in `/.crosspoint/anki.json` through the same `PersistableStore`
  pattern as OPDS servers; the token is stored as `token_obf` (XOR with the
  device MAC, base64). A hand-written plain `token` key is accepted and
  rewritten obfuscated on first load, so users can drop a file on the SD card.
- Token entry paths: the device's web Settings page (an "Anki" card next to the
  OPDS card, same `/api/anki` GET/POST/delete shape as `/api/opds`) or the SD
  file. The on-device screens can also type every field on the keyboard, but
  that is the fallback, not the intended path. The web UI lives on the existing
  Settings page rather than a new `/anki` page: it mirrors how Wi-Fi and OPDS
  are managed there and costs no extra flash for a page.
- Deck lists come from `GET /decks` during every sync and are cached per
  account (`/.crosspoint/anki/<n>/decks.json`); deck pickers (device and web)
  use the cache. No browser-to-AnkiDo calls, so no CORS setup.

## Sync (2026-09-27)

- One session = auto-connect to the saved Wi-Fi network, then per enabled
  account: `POST /exchange` (pending reviews + `want` for the review account,
  reviews only for the others), `POST /notes` for queued words, `GET /decks`.
  Then disconnect and silent-restart, like every other Wi-Fi session in
  CrossPoint (heap defragmentation).
- Triggers: manual in the review app (restart back into the app); on review app
  exit when something is pending (restart to home); at sleep entry when
  something is pending, battery > 20 %, and a last-connected Wi-Fi network is
  saved, with a 15 s overall deadline and a "Syncing Anki…" line on screen. No
  timer. Automatic failures are silent; pending items stay queued.
- Reviews are journaled on SD (`reviews.jsonl`) with `client_id =
  <deviceid>-<counter>` and `answered_at` from the RTC when the clock is
  plausible (>= 2024), otherwise omitted so the server dates the review at
  receipt. Every `client_id` echoed back by the server (applied, duplicate or
  rejected) is dropped from the journal; the rest stay for the next session.
- Card cache is one JSON object per line (`cards.jsonl`) written while the
  response streams, so the response body never sits in RAM (ESP32-C3 budget).
  A card line is parsed on demand with the client's own streaming reader.
- AnkiDo's `next` strings carry Unicode bidi isolates (U+2068/U+2069). They are
  stripped before drawing; the fonts have no glyphs for them.
- No media in v1: `[img:...]` becomes `[image]`, audio markers are dropped. The
  cache line keeps the `media` list so a later version can fetch it.

## Review app (2026-09-27)

- Home menu entry "Anki". Reviews only the review account.
- Card text uses the reader font settings. `**bold**`, `_italic_` are rendered;
  `[cloze]` is drawn boxed. Header: deck, kind, remaining counts. Footer:
  the four grade labels with AnkiDo's `next` intervals.
- Confirm flips. Grades are one press each: Left = Again, Right = Good, side
  Down = Hard, side Up = Easy; on touch devices four bottom strips do the same.
- No undo. Empty cache offers Sync.

## Add word (2026-09-27)

- EPUB reader only (TXT/XTC have no word model). Entry points: reader menu item
  "Add to Anki" and a long-press Confirm option. Reuses the dictionary
  word-selection screen in a second mode.
- Modal: account and deck preselected to last used; Up/Down changes account,
  Left/Right changes deck, Confirm adds. Toast "Queued, N pending".
- Note: Front = `<b>word</b><br><br>sentence` (word bolded in the sentence),
  Back = StarDict translation when a dictionary is configured and the lookup
  hits, else empty. `dedupe: update` so a later edit on the phone is never
  clobbered by a re-send. Tags: `crosspoint`, `book:<title>`. Model per account,
  default `Basic`; first field = front, second = back.
- Sentence context is taken from the words on the current page between the
  nearest `.!?` boundaries; a sentence spanning a page break is cut.
- Long-press option list: upstream hides "Reader menu" on boards without a home
  key by truncating the option list. Settings store the list index, so the new
  "Add to Anki" value (5) can only be appended if the list is never truncated.
  The fork therefore shows "Reader menu" on every board.

## Release, OTA and flashing (2026-09-27)

- Version tags: `<upstream version>-anki.<n>`, equal to `version` in
  `platformio.ini` (the release workflow enforces it). Publishing a GitHub
  release builds all five upstream targets and attaches
  `crosspoint-<tag>-<device>.bin`.
- The firmware's OTA updater points at `H1D/crosspoint-anki` releases.
- `site/` is a static esptool-js flasher deployed to GitHub Pages; it lists the
  fork's releases via the GitHub API (version picker, pre-releases marked) and
  writes the app image into the inactive OTA slot using the same partition
  layouts as crosspointreader.com's flasher.

## Strings (2026-09-27)

All 34 UI languages receive the new strings. Non-English translations were
machine-drafted by the assistant and are unreviewed.

## Process (2026-09-27)

Built in one pass, committed directly to `develop` on the fork, reviewed as a
whole afterwards.

## Implementation notes (2026-09-27)

- **Note fields are `Front` and `Back`.** `POST /notes` needs exact field
  names per note type; the fork assumes the account's note type has fields
  named `Front` and `Back` (true for Anki's `Basic` family). Other note types
  are accepted in settings but will fail with `unknown_field` in the sync
  summary until per-account field mapping exists.
- **Every echoed `client_id` is dropped from the journal**, including
  `rejected`/`card_not_found`. AnkiDo allows a retry for `card_not_found`, but
  a card deleted on another device is not worth keeping a stuck entry for.
- **Own streaming JSON reader** (`lib/AnkiClient/AnkiJson.*`) instead of the
  repo's `StreamingJsonParser`: that one caps tokens at 512 bytes and does
  not decode `\uXXXX`; card text needs both. It uses `std::function`
  callbacks (a few KB of flash) because host-testability outweighed the
  CLAUDE.md preference for plain function pointers here; revisit if flash
  gets tight.
- **`answered_at` only when the RTC year is >= 2024**, otherwise the review is
  sent without a timestamp and AnkiDo dates it at receipt. No `elapsed_s`:
  uptime does not survive deep sleep.
- **Sync order per session**: exchange (reviews + queue) → notes → decks. A
  deck-list failure does not fail the session.
- **Inline AnkiWeb sync timeout**: 15 s for manual/app syncs. The sleep-entry
  sync only uploads (reviews exchange without `want`, then notes; no deck fetch,
  no card download) with a 4 s HTTP timeout per request, a 3 s inline sync
  timeout, and the 15 s budget checked before every request.
- **Build environment**: PlatformIO core 6.2.0 requires `tool-scons
  4.41101.0` but the pioarduino platform pinned 4.40801.0 and deleted the
  package mid-build, which broke the ESP32-C3 link step. Fixed locally by
  pointing `~/.platformio/platforms/espressif32/platform.json` at
  `platformio/tool-scons ~4.41101.0` (lost on `pio pkg update`; CI installs
  fresh and is unaffected).
- **Simulator**: `[env:simulator]` in `platformio.ini` expects the
  crosspoint-simulator checkout as a sibling directory (`../simulator`), with
  the fork's `SecureHttpClient` streaming API added to the simulator's stub.
  Headless runs: `SDL_VIDEODRIVER=dummy CROSSPOINT_SIM_INPUT_SCRIPT=...
  CROSSPOINT_SIM_SCREENSHOTS=... .pio/build/simulator/program`; SD root is
  `./fs_/`.
- **Known, accepted (review 2026-09-27)**: the journal/queue rewrite after an
  acknowledgement is temp-file + remove + rename, not atomic on FAT; a power
  loss in that window loses only entries the server had not acknowledged, and
  the next session re-sends nothing wrong. A sleep-entry sync started while
  the File Transfer hotspot is up drops the AP before that screen's own exit
  runs; sleep follows anyway.
- **No per-device new-card cap (2026-09-27, reversed).** The plan had a
  "New cards per day" setting per account. Anki's deck options already own
  that limit and AnkiDo applies them server-side; AnkiDo's
  `max_new_per_day` only caps a single fetch and forgets earlier fetches, so
  the setting duplicated the deck option under a misleading name. Removed
  from the device and web UI; the device sends no cap. The `maxNewPerDay`
  field stays in the store (default 0 = none) so old files still load.
- **Account picker instead of a fixed review account (2026-09-27, reversed).**
  With more than one enabled account the review app asks which one to review
  every time it opens (rows: account name, subtitle profile@url; a hint row
  says that disabling accounts in Settings skips the question). With exactly
  one enabled account it opens directly. The "Review this account" setting
  and the web "Review" radio are gone; `reviewAccount` in anki.json now means
  "last reviewed" and is written by the picker, so exit- and sleep-entry syncs
  refresh that account's queue.
- **Default deck is a setting (2026-09-27, reversed).** The add-word modal
  used to preselect the deck last chosen in the modal and overwrite it on
  every add. Now each account has an explicit "Deck for new words" setting
  (device editor and web card); the modal preselects it and a different pick
  in the modal is a one-off. The JSON key stays `lastDeck` for compatibility.

## AnkiConnect backend (2026-09-28)

- **Per-account "Server type": AnkiDo or AnkiConnect.** AnkiConnect is the
  add-on inside Anki desktop (`http://<pc>:8765`), so people who run Anki on a
  PC in the same network need no extra server. The account fields are reused:
  `url` is the AnkiConnect endpoint, `profile` is the Anki profile to load
  first (optional; `loadProfile` switches the desktop app), `token` is the
  add-on's `apiKey` (optional). Stored as `"backend": "ankido" | "ankiconnect"`
  in anki.json; absent = AnkiDo, so old files load unchanged.
- **Same steps, same SD files.** `AnkiConnectClient` (lib/AnkiClient) fills
  cards.jsonl, decks.json, reviews.jsonl and notes.jsonl exactly as the AnkiDo
  path does; the review app, add-word and the pickers do not know which
  backend an account uses. `AnkiSyncEngine` dispatches on `account.backend`.
- **Queue = three searches, then cardsInfo in batches of 8.** AnkiConnect has
  no scheduler queue call, so the device searches `is:learn`, `is:due
  -is:learn` and `is:new` (suspended/buried excluded) within the account's
  decks, in that order, up to "Cards per sync". New cards are additionally
  capped by the decks' `getDeckStats.new_count`, which already honours the
  deck's daily limit and today's introduced cards. Learning cards are taken
  whether or not their intraday due time has passed: the device is offline
  between syncs. `findCards` order is Anki's default (creation order), which
  approximates new-card position.
- **Card HTML is rendered on the device.** `cardsInfo` returns the note
  type's CSS plus the rendered HTML; `AnkiHtml` turns it into the same
  `**bold**`/`_italic_`/`[cloze]`/`[img:]` markup AnkiDo produces
  (`collection/render.py` is the reference), so `AnkiMarkup` and the review
  screen stay shared. The answer side is cut at `<hr id=answer>`. The converter
  runs per string as the JSON streams, so one card's HTML is the most that is
  ever in RAM.
- **No "next" interval labels.** AnkiConnect has no per-card scheduling
  preview; the grade footer shows only Again/Hard/Good/Easy for these
  accounts. `card.next[]` stays empty in the cache line.
- **Reviews: `answerCards`, graded "now".** There is no `client_id` and no
  `answered_at`; a review is applied at the time of the sync. The whole batch
  is dropped from the journal once the request was processed (`false` in the
  result means the card no longer exists). A response lost after the server
  applied it would grade those cards twice on the next sync; accepted, the
  same as any AnkiConnect client.
- **Notes: one `addNote` per queued note.** `addNotes` raises a combined
  exception when any note fails and hides which ones were added, so notes go
  one at a time. Field names come from `modelFieldNames` (first two fields of
  the account's note type, cached per session; `Front`/`Back` when the lookup
  fails), which removes the Front/Back limitation for this backend.
  `allowDuplicate:false` with deck scope: a duplicate or empty note is dropped
  as done (AnkiConnect cannot "update" like AnkiDo's `dedupe: update`); a
  missing deck or note type keeps the note queued and shows the message.
- **No AnkiWeb sync trigger.** AnkiConnect's `sync` action opens the login
  dialog on the desktop when AnkiWeb is not configured; the desktop app syncs
  on its own schedule instead. Can be revisited as an opt-in.
- **Auth failure detection.** A wrong `apiKey` comes back as HTTP 200 with
  `"error": "valid api key must be provided"`; that text sets `authFailed`, so
  the sync screen shows the same "rejected" hint as an AnkiDo 401.
- **Sleep-entry sync** works unchanged: `exchange(wantCards=false)` is one
  `answerCards` request (plus `loadProfile` when a profile is set) and
  `pushNotes` is one request per note, all under the same 4 s HTTP timeout.
- **Add-word modal is a checklist (2026-09-28).** One toggle row per enabled
  account (subtitle: the deck this add goes to, Left/Right changes it for
  this add only) and an "Add" row; the note is queued once per checked
  account with its own client id. The checked set is remembered between adds
  (first time: the last reviewed account). Tap works everywhere, so touch-only
  boards no longer depend on a physical Confirm.

## Add-word screen (2026-09-28)

- Per enabled account: a checkbox row and a **Deck** row. Deck opens the
  account's synced deck list (or the keyboard before its first sync);
  tapping Deck on an unticked account ticks it. Left/Right still cycle the
  highlighted account's deck. The choice is for this add only; the
  persistent default is the account's "Deck for new words".
- A successful Add closes word selection too and returns to the page; Back
  from the add screen returns to word selection to pick another word.
- **Rows are accounts only (2026-09-30, reversed).** The Deck rows made the
  screen twice as long as the choice it offers. Each account is one checkbox
  row with its target deck as the subtitle; a long press on the row (Confirm
  hold or touch) opens the deck list and ticks the account, Left/Right still
  cycle it. The note band above the list is one line, "word — translation";
  the sentence is not shown (it is still sent).
- **Back = the translations only (2026-09-30).** A WikDict entry holds IPA,
  part of speech, English sense glosses and the translations; flattening all
  of it made the Back field unreadable. `WikDict::shortTranslation`
  (src/util/WikDict) keeps the translations only: up to 6 distinct terms,
  the first of each sense before the rest, senses of several entries (noun,
  verb) taken in turn, stress accents dropped. Dictionaries in any other
  layout still get the whole entry as plain text, capped at 600 bytes.
- **Front = the dictionary form (2026-09-30).** When the lookup lands on a
  different headword ("kwam" -> "komen", "walked" -> "walk"), the card's
  top line is the headword and the sentence bolds the word as written; the
  add screen shows "kwam (komen) — ...". Two forms of one word then make one
  note instead of two.
- **Touch long-press without Anki (2026-09-30).** With no enabled account, a
  long press on a word opens the dictionary picker on it instead of doing
  nothing (when a dictionary is selected).
- **Definition first, then add (2026-09-30, reverses the one-line band).**
  A picked word opens the normal definition screen; with an Anki account
  enabled it has an "Add to Anki" button (Confirm on button boards) that
  opens the add screen. Reading the translation and adding the card were
  one cramped line before. The add screen now previews the card: front (the
  dictionary form), the example sentence (3 lines) and back, each under a
  dimmed caption, in a bordered box above the account rows. Without a
  dictionary, or when the word is not in it, "Add to Anki" still opens the
  add screen directly with an empty back.
- **"hold to change" after the deck (2026-09-30).** The long-press on an
  account row was undiscoverable. FreeInkUI draws a row subtitle as one
  run, so the dimmed hint is drawn after it at the list's row geometry.
  Dimmed text is black ink thinned to a checkerboard: the renderer has no
  dithered text, so FUI's gray text colors come out solid black. The
  selected row keeps the hint in solid ink; on the gray selection pill the
  checkerboard is unreadable.
- **Last used deck is saved (2026-09-30, reverses "Default deck is a
  setting").** Each add stores the deck it went to as the account's "Deck
  for new words", so the next add starts there. Picking a deck and then
  backing out saves nothing.
- **Back button on the definition screen (2026-09-30).** The definition
  screen drew its own header, so touch boards (no button hints) had no
  visible way back but the edge swipe. It now uses the theme header, which
  carries the tappable back arrow.

## Dictionaries (2026-09-30)

- **One dictionary per book language, no new setting.** The Settings choice
  stays the default; a book whose EPUB language has a matching dictionary
  on the card uses that one instead (`DictionaryRegistry::pickForBook`,
  cached per book in the reader). "None" still disables lookups everywhere,
  so there is no separate on/off. Rejected: trying every dictionary in turn
  until one hits, because short Dutch words ("is", "hem", "was") exist in
  English dictionaries too and would get the wrong language's entry.
- **Dictionary language from the .ifo or its name.** StarDict has no
  standard language key. Order: a `lang=` line (first code = headword
  language), then a trailing "(xx-yy)" in `bookname` (every WikDict/FreeDict
  file), then "xx-yy" at the end of the folder name. A dictionary with none
  of these only serves as the default.
- **Stemming per language.** The English suffix rules stay for every
  language but Dutch (their behaviour before this change). Dutch gets its own
  rules, spelling-aware (bomen -> boom, not bom; zegt -> zeggen, not zegen)
  and a table of ~290 irregular verb forms in flash (`DictStemmer`). The
  irregular form's verb comes first and the word's own entry, if any,
  follows: in running text "was" and "lag" are far more often verbs, but
  "roken" (to smoke) and "vroeg" (early) are words of their own. The card
  takes the verb as its headword.
  On the Dutch "Vadertje Langbeen" (45k words) against WikDict nl-ru the
  hit rate went from 56% to 71%; most remaining misses are pronouns and
  1920s spellings the 14k-word dictionary lacks. Rejected: generating a .syn
  file from Wiktionary inflection data. Better coverage, but it works only
  for dictionaries rebuilt with a script, not for downloaded ones.
- **Every entry of a headword.** WikDict stores noun and verb as separate
  .idx entries with the same headword; the lookup used to read only the
  first ("run" gave only noun senses). It now reads up to 4 adjacent entries
  and appends them.
- **Compact WikDict view.** The definition screen lays WikDict entries out
  as "part of speech", then "1. *gloss* — translations" per sense, without
  the IPA line (the reader fonts have no IPA glyphs, it drew as boxes). The
  delivered HTML nests gloss and translation lists in a way the EPUB layout
  engine ran together ("distanceближний"). Other HTML dictionaries are shown
  as before.
