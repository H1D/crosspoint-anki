# crosspoint-anki: design decisions

This fork adds Anki flashcard review and "add word from a book" to CrossPoint
Reader, backed by an [AnkiDo](https://github.com/H1D/AnkiDo) server. This file
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

- **AnkiDo is the only backend.** The device never holds AnkiWeb credentials; it
  holds an AnkiDo base URL, a profile name and a bearer token with scopes
  `read,review,add`. `sync` scope is not needed: `POST /exchange` syncs inline.
- Development instance: Docker Compose in `ankido-dev/` next to the clone (not
  in this repo), published on host port 8766 (8765 was taken on the dev
  machine), profile `test`. The AnkiWeb test login lives only in its
  `secrets/test.env` (mode 0600).

## Accounts (2026-09-27)

- One account entry = `name, url, profile, token, enabled, model, decks[],
  cacheSize, maxNewPerDay`. Several entries may point at one AnkiDo instance.
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
  A card line is parsed on demand with ArduinoJson.
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
