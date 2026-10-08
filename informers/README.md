# Informers

Small screens (weather, calendar, ...) rendered by a Cloudflare Worker and
shown by the reader as plain images. The firmware stays a thin client: each
informer is an SD-card plugin whose `sleep.enter` handler downloads a finished
BMP (see `docs/plugin-events.md`). Adding an informer never needs a firmware
build.

## Pieces

- `worker/` is the Worker, deployed as
  `https://crosspoint-informers.doorcomp.workers.dev`. `GET /<name>.bmp`
  returns a 480x800 BMP; `GET /` lists the informers. `depth=2` returns a
  2-bit BMP with the panel's four native grays (anti-aliased text, gray
  fills); anything else returns 1-bit black and white with grays dithered.
  The reader shows 2-bit images in real gray in the image viewer and on the
  sleep screen (when the sleep cover filter is off). No dependencies:
  `src/canvas.js` draws shapes and text into a pixel buffer, text comes from
  glyph bitmaps baked from Noto Sans (`scripts/bake_fonts.py` → `src/fonts.js`),
  `src/icons.js` draws weather icons. Images are drawn per request; upstream
  JSON is cached at the edge (`src/fetch.js`). Data that needs a login (school
  news) is pushed into the `SCHOOL` KV namespace instead of fetched.
- `worker/src/informers/<name>.js` is one informer: a default export with
  `render(canvas, params, env)` that draws into the canvas it is given (check
  `canvas.gray` to add shading only in gray mode). Register it in
  `src/index.js`.
- `sd/plugins/<name>/` is the plugin folder to copy onto the SD card.
  `device.json` declares the download; `config.json` holds the server address,
  query parameters, and destination path.
- `sd/informers/` is where the images land. Create this folder on the card:
  the download does not create missing folders.

## Informers

| Name | Data | Query |
| --- | --- | --- |
| `weather` | Buienradar feed (nearest station, 4-day forecast) and Buienalarm rain nowcast; simple Dutch for an 8-year-old | `lat`, `lon`, `place`, `depth`, `demo=rain` |
| `vakantie` | Rijksoverheid open data school holidays; days until the next holiday per region, simple Dutch for an 8-year-old | `regio=noord\|midden\|zuid`, `depth`, `vandaag=YYYY-MM-DD` |
| `school` | Parro news per child, pushed twice a day by a job on Hermes's host (see `school/README.md`); stays in KV, needs `key` | `kid`, `key`, `depth`, `vandaag=YYYY-MM-DD` |
| `dag` | One child's day: weather with a rain row (raindrops per hour: one a little rain, three heavy rain; the next two hours from the radar), the day's agenda, school news about that day. The agenda is pushed hourly by a job on Hermes's host (see `agenda/README.md`); one-off events are white on black, repeating ones a plain line | `kid`, `key`, `offset=0\|1\|2` (today, tomorrow, day after), `lat`, `lon`, `depth`, `vandaag=YYYY-MM-DD` |

## Develop and deploy

```sh
cd informers/worker
node scripts/render.mjs weather /tmp/weather.bmp "lat=52.37&lon=4.90&place=Amsterdam&depth=2"
python3 scripts/preview.py /tmp/weather.bmp /tmp/weather.png   # Pillow cannot open 2-bit BMPs
CLOUDFLARE_API_TOKEN="$(systemd-creds --user decrypt ~/.config/agent-secrets/cloudflare/crosspoint-informers-hermes.cred -)" scripts/deploy.sh
```

`render.mjs` runs an informer under Node or Bun without Wrangler, fetching live
data. The deploy token is the account-owned `crosspoint-informers-hermes`
token, limited to this one Worker (Hermes keeps a copy in
`~/.hermes/secrets/crosspoint-informers-cf.env`). That token cannot read the
account's workers.dev subdomain, so plain `wrangler deploy` fails;
`deploy.sh` uploads a version and promotes it.

To add an informer, follow `skills/crosspoint-informer/SKILL.md` (also
installed for Hermes).

## On the reader

1. Copy `sd/plugins/weather` to `/plugins/weather` on the card (and
   `sd/plugins/vakantie`, `sd/plugins/school`, and either `sd/plugins/dag` or
   `sd/plugins/school-sleep`: both write the sleep screen),
   create `/informers`, and edit each `config.json` (weather: lat, lon, place;
   vakantie: regio; school and school-sleep: kid and key, from
   `~/.hermes/secrets/crosspoint-school.env` on clawd).
2. Connect the reader to WiFi once, then restart it so the plugin subscribes.
3. Put the reader to sleep. It joins WiFi (battery at least 20%), downloads the
   image, and sleeps.
4. Open `/informers` in the file browser and pick an image. Left and Right flip
   between informers; files are ordered by name, hence the number prefixes.

To show an informer as the sleep screen, set its `dest` to `/sleep.bmp` and
set Sleep Screen to Custom. Images refresh when the reader goes to sleep and
the plugin's last refresh is at least `refresh_minutes` old (120 in these
plugins, so most sleeps need no WiFi), and at the `wake` times a plugin lists (the school plugins ask for 07:55 and
19:05): the reader wakes itself, downloads, redraws the sleep screen, and
sleeps again (see `docs/plugin-events.md`). Each image prints the time it was
rendered. Every informer takes `lang=en` for English (`lang` in each
`config.json`); the default is Dutch.

Limits from the plugin system (weather, vakantie, school and dag together
use all 4 events of one sleep, 6 downloads): one `sleep.enter` handler per plugin, which
may download up to 4 images (the `dag` plugin saves today, tomorrow and the day
after), up to 8 subscribed plugins, 4 plugins' events per sleep (extra
informers refresh on the next sleep), 1 MB per download.
