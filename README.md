# crosspoint-anki

A fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader),
the open-source firmware for Xteink X3/X4-class e-readers, with [Anki](https://apps.ankiweb.net/)
flashcards built in. Everything else is stock CrossPoint; for reading features,
supported devices, and the user guide see the
[upstream README](https://github.com/crosspoint-reader/crosspoint-reader#readme).

## What this fork adds

- **Anki on the home screen.** Review your due cards on the e-ink display: flip
  with Confirm, grade with one press (Left = Again, Right = Good, side Down =
  Hard, side Up = Easy, or tap the four strips on touch devices). Anki's own
  scheduler does the scheduling; the reader only shows cards and records grades.
- **Add a word from a book.** In an EPUB, long-press a word (touch), hold
  Confirm, or use "Add to Anki" in the reader menu. A note is created with the
  word and its sentence on the front and the dictionary translation on the back
  (when a StarDict dictionary is set up), tagged with the book title.
- **Offline first.** Cards are cached on the SD card; grades and new notes are
  queued there and sent on the next sync. Sync happens on demand, when you
  leave the review screen with pending grades, and briefly when the device goes
  to sleep (if a saved Wi-Fi network is in reach and the battery is above 20 %).
- **Several accounts.** Each account is an AnkiDo profile; when more than one
  is enabled the review screen asks which one to use.
- **Settings → System → Anki accounts**, plus an "Anki accounts" card on the
  device's web settings page (`/settings` while File Transfer is running).

The firmware talks to an [AnkiDo](https://github.com/H1D/AnkiDo) server, which
syncs with AnkiWeb on your behalf. The device never holds your AnkiWeb
password, only a per-device AnkiDo token.

Other differences from upstream: releases are tagged `<upstream version>-anki.<n>`
(for example `1.6.5-anki.1`), the built-in updater checks this fork's releases,
and all 34 UI languages carry the new strings (translations other than English
are machine-drafted).

Design decisions are recorded in [docs/anki/DECISIONS.md](docs/anki/DECISIONS.md);
the code layout in [docs/anki/ARCHITECTURE.md](docs/anki/ARCHITECTURE.md).

## Flashing

**Web flasher (recommended):** open <https://h1d.github.io/crosspoint-anki/> in
Chrome or Edge (WebSerial is required; Firefox and Safari do not support it).

1. Connect the reader over USB and switch it on. No button combination is
   needed; the flasher puts the chip into download mode itself.
2. Pick your device (X3/X4, X4 Pro, X4C, Sticky, PaperMono) and the release
   (newest preselected; pre-releases are marked).
3. Click **Connect & Flash** and choose the serial port. The image is written
   to the inactive firmware slot and the device reboots into it.

Close any program that holds the serial port first (PlatformIO monitor,
Arduino IDE). If the flasher cannot download a release, download
`crosspoint-<version>-<device>.bin` from the
[releases page](https://github.com/H1D/crosspoint-anki/releases) and use the
flasher's "Flash a local .bin" option.

**Already running this fork?** Settings → System → Check for updates installs
newer releases over Wi-Fi.

**Coming from stock Xteink firmware:** some units bought from third-party
stores have USB flashing locked. If the device never appears as a serial port,
use the [Xteink Unlocker](https://crosspointreader.com/#unlock-tool) first.
Sticky and PaperMono need a first install with the
[official CrossPoint flasher](https://crosspointreader.com/) (it writes the
boot region); after that this flasher and OTA work.

## First-time setup

1. Run an AnkiDo server ([quickstart](https://github.com/H1D/AnkiDo#quickstart))
   and create a device token: `ankido token create --profile <name> --scopes read,review,add`.
2. Put the account on the reader, either by starting File Transfer on the
   device and filling in the "Anki accounts" card at `http://<reader-ip>/settings`,
   or by placing a file at `/.crosspoint/anki.json` on the SD card:

   ```json
   {"accounts":[{"name":"me","url":"http://192.168.1.10:8766","profile":"me","token":"akd_...","enabled":true}]}
   ```

   The token is stored obfuscated after the first boot.
3. Save a Wi-Fi network on the device (Settings → System → Wi-Fi networks).
4. Home → Anki → Sync.

Add-word notes use a note type with fields named `Front` and `Back` (Anki's
`Basic` family); set the note type per account in the account settings.

## Building from source

Same as upstream: PlatformIO, `git submodule update --init`, then
`pio run -e x4pro` (or `default` for X3/X4, `x4c`, `sticky`, `papermono`).
`pio run -e simulator_x4_pro` builds the desktop simulator when the
[crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator)
checkout sits next to this directory as `../simulator`.

## License

Same license as upstream; see [LICENSE](LICENSE).
