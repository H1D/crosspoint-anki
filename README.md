# crosspoint-anki

[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) firmware with [Anki](https://apps.ankiweb.net/) built in.
Everything else is stock CrossPoint ([upstream README](https://github.com/crosspoint-reader/crosspoint-reader#readme)).

**Needs a server on your network:** either [AnkiDo](https://github.com/H1D/AnkiDo) (syncs with AnkiWeb for you) or Anki desktop with the [AnkiConnect](https://ankiweb.net/shared/info/2055492159) add-on (the reader talks to Anki on your PC directly; AnkiConnect must listen on your LAN address, see its `webBindAddress` setting).

## Features

| | |
|---|---|
| <img src="docs/anki/img/home.png" width="400"> | **Anki on the home screen.** |
| <img src="docs/anki/img/review.gif" width="400"> | **Review cards.** Confirm flips. Left = Again, Right = Good, side Down = Hard, side Up = Easy (or tap the strips). Anki does the scheduling. |
| <img src="docs/anki/img/add-word.gif" width="400"> | **Add a word from a book.** Long-press a word (or reader menu → Add to Anki): its translation opens with an **Add to Anki** button. The add screen previews the card (front: the word's dictionary form, the example sentence, back: just its translations) above your accounts. Tick one or more, each row shows its deck (hold a row to pick another; the last deck used is remembered), **Add**. |
| <img src="docs/anki/img/dictionary.gif" width="400"> | **Dictionaries per book language.** Put one [StarDict](docs/dictionary.md) dictionary per language in `/dictionaries/` ([WikDict](https://download.wikdict.com/dictionaries/stardict/) has most pairs); each book uses the one for its own language. Inflected words find their dictionary form (Dutch *staarde* → *staren*, *weilanden* → *weiland*; English *walked* → *walk*), and WikDict entries show as meaning → translations. Works without Anki too: long-press looks the word up. |
| <img src="docs/anki/img/settings.png" width="400"> | **Offline first.** Cards are cached on the SD card; grades and new notes queue up and sync on demand, when you leave the review screen, or when the device goes to sleep. |

## Flash

**<https://h1d.github.io/crosspoint-anki/>** — Chrome or Edge, USB cable, pick your device and release, Connect & Flash.

Then set up an account: File Transfer on the reader → `http://<reader-ip>/settings` → Anki accounts. Pick the server type, then either AnkiDo URL, profile and token (from `ankido token create --profile <name> --scopes read,review,add`), or the AnkiConnect URL (`http://<pc-ip>:8765`), optionally the Anki profile to load and the add-on's API key. Or drop `/.crosspoint/anki.json` on the SD card:

```json
{"accounts":[
  {"name":"me","url":"http://192.168.1.10:8766","profile":"me","token":"akd_...","enabled":true},
  {"name":"pc","backend":"ankiconnect","url":"http://192.168.1.20:8765","enabled":true}
]}
```

With AnkiConnect the grade buttons show no "next interval" hints (the add-on has no preview call), and reviews are dated at sync time.

Updates: Settings → System → Check for updates.

Notes: [docs/anki/DECISIONS.md](docs/anki/DECISIONS.md) · [docs/anki/ARCHITECTURE.md](docs/anki/ARCHITECTURE.md) · [LICENSE](LICENSE)
