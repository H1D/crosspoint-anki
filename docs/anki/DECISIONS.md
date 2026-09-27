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
