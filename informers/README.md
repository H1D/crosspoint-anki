# Informers

Small screens (weather, calendar, ...) rendered by a Cloudflare Worker and
shown by the reader as plain images. The firmware stays a thin client: each
informer is an SD-card plugin whose `sleep.enter` handler downloads a finished
BMP (see `docs/plugin-events.md`). Adding an informer never needs a firmware
build.

## Pieces

- `worker/` is the Worker, deployed as
  `https://crosspoint-informers.doorcomp.workers.dev`. `GET /<name>.bmp`
  returns a 480x800 1-bit BMP; `GET /` lists the informers. No dependencies:
  `src/canvas.js` draws shapes and text into a pixel buffer, text comes from
  glyph bitmaps baked from Noto Sans (`scripts/bake_fonts.py` → `src/fonts.js`),
  `src/icons.js` draws weather icons. Responses are cached for 5 minutes.
- `worker/src/informers/<name>.js` is one informer: a default export with
  `render(params)` returning a `Canvas`. Register it in `src/index.js`.
- `sd/plugins/<name>/` is the plugin folder to copy onto the SD card.
  `device.json` declares the download; `config.json` holds the server address,
  query parameters, and destination path.
- `sd/informers/` is where the images land. Create this folder on the card:
  the download does not create missing folders.

## Informers

| Name | Data | Query |
| --- | --- | --- |
| `weather` | Buienradar feed (nearest station, 4-day forecast) and Buienalarm rain nowcast; simple Dutch for an 8-year-old | `lat`, `lon`, `place`, `demo=rain` |

## Develop and deploy

```sh
cd informers/worker
bun scripts/render.mjs weather /tmp/weather.bmp "lat=52.37&lon=4.90&place=Amsterdam"
CLOUDFLARE_API_TOKEN=... CLOUDFLARE_ACCOUNT_ID=8a97ec747d5396c10e3e0b739d75ef22 wrangler deploy
```

`render.mjs` runs an informer under Bun without Wrangler, fetching live data.
The deploy token is the account-owned `crosspoint-informers-deploy` token
(Workers Scripts read/write only).

## On the reader

1. Copy `sd/plugins/weather` to `/plugins/weather` on the card, create
   `/informers`, and edit `config.json` (lat, lon, place).
2. Connect the reader to WiFi once, then restart it so the plugin subscribes.
3. Put the reader to sleep. It joins WiFi (battery at least 20%), downloads the
   image, and sleeps.
4. Open `/informers` in the file browser and pick an image. Left and Right flip
   between informers; files are ordered by name, hence the number prefixes.

To show an informer as the sleep screen, set its `dest` to `/sleep.bmp` and
set Sleep Screen to Custom. Images refresh only on sleep; each one prints the
time it was rendered.

Limits from the plugin system: one `sleep.enter` handler per plugin (so one
image per plugin), up to 8 subscribed plugins, 4 handlers per sleep (extra
informers refresh on the next sleep), 1 MB per download.
