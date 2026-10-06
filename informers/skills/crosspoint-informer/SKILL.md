---
name: crosspoint-informer
description: "Create, change, or deploy an informer for the CrossPoint e-reader (Xteink X4): a small glanceable screen such as weather, a calendar, a school timetable, or Anki stats. A Cloudflare Worker renders it as a 480x800 1-bit BMP and an SD-card plugin downloads it each time the reader sleeps. Use when the user asks for a new informer, widget, or dashboard page on the e-reader, or to change or redeploy an existing one."
metadata:
  hermes:
    tags: [crosspoint, e-reader, xteink, informer, cloudflare-workers, e-ink]
---

# CrossPoint informers

The reader is a thin client. Each informer is:

1. a module in the `crosspoint-informers` Cloudflare Worker that draws a
   480x800 image, served at `GET /<name>.bmp?<query>`, and
2. an SD-card plugin folder whose `sleep.enter` handler downloads that image
   into `/informers/` on the card.

The firmware never changes. New informer = new Worker module + new plugin folder.

Live Worker: `https://crosspoint-informers.doorcomp.workers.dev` (`GET /` lists informers).

## Where things are

The code lives in the `H1D/crosspoint-anki` repo, folder `informers/`.
Find the checkout before doing anything:

- `$CROSSPOINT_REPO` if set; otherwise look for `~/crosspoint-anki/wt-informers`,
  `~/crosspoint-anki/upstream`, `~/.hermes/work/crosspoint-anki`.
- If none exists, `git clone https://github.com/H1D/crosspoint-anki` and check
  out the branch that contains `informers/worker/` (`feat/informers`, later `develop`).

```
informers/
  worker/src/index.js              registry: const INFORMERS = { weather, ... }
  worker/src/informers/<name>.js   one informer each
  worker/src/canvas.js             drawing API (see references/canvas-api.md)
  worker/src/fonts.js              baked glyph bitmaps (generated, do not edit)
  worker/src/icons.js              weather icons, raindrop
  worker/src/time.js               Dutch clock/date in Europe/Amsterdam
  worker/src/render.js             builds the canvas for the requested depth, encodes the BMP
  worker/src/fetch.js              fetchJson(url, { headers, ttl }): upstream JSON via the edge cache
  worker/wrangler.jsonc            Worker config (name, compatibility date)
  worker/scripts/render.mjs        render one informer locally to a BMP
  worker/scripts/preview.py        BMPs (1- or 2-bit) to a side-by-side PNG
  worker/scripts/deploy.sh         deploy with the scoped token
  sd/plugins/<name>/               plugin folder for the SD card
```

Read `informers/worker/src/informers/weather.js` before writing a new one; copy its shape.

## Add an informer

1. **Pick the data.** Prefer keyless public JSON APIs. Fetch them with
   `fetchJson(url, { ttl })` from `../fetch.js`: it caches the upstream
   response at the edge for `ttl` seconds (default 300; a day for data that
   changes daily) and throws on non-2xx. Do not cache the finished image: it
   is drawn per request so its printed time is always the time of the sleep.
   Data behind a personal login (school news from Parro) is not fetched by the
   Worker: a job on clawd pushes it into KV and the informer reads it there;
   copy `informers/school/` and `src/informers/school.js` for that shape.
   A secret (API key) goes in as a Worker secret:
   `npx wrangler secret put NAME` (same env vars as deploy) and is read from
   `env.NAME` in `render(c, params, env)`.
2. **Write `worker/src/informers/<name>.js`:**

   ```js
   import { BLACK, DARK_GRAY, GRAY } from "../canvas.js";
   import { fetchJson } from "../fetch.js";
   import * as F from "../fonts.js";
   import { clock, dateLong } from "../time.js";

   export default {
     title: "Rooster",
     // Query: document every parameter the plugin passes (depth is handled for you).
     async render(c, params, env) {
       const data = await fetchJson("https://example.org/api"); // throws -> 502, reader keeps its old image

       // c is a white 480x800 canvas; c.gray is true when the reader asked for 4-level gray.
       c.text("Rooster", 24, 50, F.title);
       c.text(dateLong(Date.now()), 24, 86, F.body);
       c.text(`om ${clock(Date.now())}`, c.width - 24, 86, F.small, { align: "right" });
       if (c.gray) c.rect(20, 110, c.width - 40, 60, GRAY); // shading only where gray is real
       c.paragraph("Korte, duidelijke zinnen.", 24, 150, c.width - 48, F.body);
     },
   };
   ```

   `name` must match `[a-z0-9-]+`. Register it in `worker/src/index.js`
   (`import x from "./informers/x.js"` and add to `INFORMERS`).
3. **Render locally and look at it.** From `informers/worker/`:

   ```sh
   node scripts/render.mjs <name> /tmp/<name>-1.bmp "a=1&b=2"           # black and white
   node scripts/render.mjs <name> /tmp/<name>-2.bmp "a=1&b=2&depth=2"   # 4-level gray
   python3 scripts/preview.py /tmp/<name>-1.bmp /tmp/<name>-2.bmp /tmp/<name>.png
   ```

   `preview.py` puts both versions side by side (Pillow cannot open 2-bit
   BMPs). Open the PNG and check both against the design rules below. Iterate until
   nothing overlaps, overflows, or is cut off. If the data has states (rain/no
   rain, empty list), add a `demo=` query switch like weather's `demo=rain` and
   render each state.
4. **Deploy:**

   ```sh
   set -a; . ~/.hermes/secrets/crosspoint-informers-cf.env; set +a   # on clawd
   # on brick: CLOUDFLARE_API_TOKEN="$(systemd-creds --user decrypt ~/.config/agent-secrets/cloudflare/crosspoint-informers-hermes.cred -)"
   scripts/deploy.sh
   ```

   `deploy.sh` defaults `CLOUDFLARE_ACCOUNT_ID`; only the token is needed.
   The token can edit only this Worker. Plain `wrangler deploy` fails on it
   (it reads the account's workers.dev subdomain); `deploy.sh` uploads a version
   and promotes it. It ends by printing `GET /`, which must list the new informer.
5. **Check the live image:**
   `curl -s -o /tmp/live.bmp -w '%{http_code}\n' "https://crosspoint-informers.doorcomp.workers.dev/<name>.bmp?<query>"`
   must print `200`, and `file /tmp/live.bmp` must say `480 x 800 x 1`
   (`x 2` with `depth=2`). Images are not cached; a fresh deploy can take a
   few seconds to reach every edge, so retry briefly if you still get the old
   output.
6. **Add the plugin folder** `informers/sd/plugins/<name>/` (folder name at most
   23 bytes):

   `device.json`
   ```json
   {
     "title": "Rooster",
     "description": "School timetable, refreshed each time the reader sleeps",
     "config": { "file": "config.json" },
     "events": {
       "sleep.enter": {
         "download": {
           "url": "{cfg.server}/<name>.bmp?a={cfg.a}&depth={cfg.depth}",
           "dest": "{cfg.dest}"
         }
       }
     }
   }
   ```

   `config.json`
   ```json
   {
     "server": "https://crosspoint-informers.doorcomp.workers.dev",
     "a": "1",
     "depth": "2",
     "dest": "/informers/2-<name>.bmp"
   }
   ```

   `README.md`: plain text shown on the reader (Settings, System, Plugins).
   The reader is a parent, not a developer: plain English, no jargon. Copy the
   structure of `sd/plugins/weather/README.md`: first "How to see it" (sleep
   and wake, then Browse Files, informers, the file name, Left/Right, Back),
   then what it shows, then settings. Say that `config.json` is edited on a
   computer (the reader's file browser shows only books and images), what each
   key means (`depth`: 2 for gray, 1 for black and white), and keep the note
   explaining the "Receives: sleep and current book" line the reader shows.
   Keep `description` in `device.json` under about 32 characters; the plugin
   list cuts longer ones off.

   Pick `dest` as the next free number: list the taken ones with
   `grep -h '"dest"' informers/sd/plugins/*/config.json` and use
   `/informers/<next>-<name>.bmp`. Never reuse a number.
7. **Update the table** in `informers/README.md`, commit on a branch with
   explicit paths, and do not push without the user's approval. No
   `Co-Authored-By` or other AI-attribution trailers in commit messages.
8. **Install on the reader** (tell the user, or do it when the reader is
   reachable). Two ways:
   - SD card: copy the folder to `/plugins/<name>/`; make sure `/informers/` exists.
   - Reader web server (reader in File Transfer mode, on the LAN):
     `curl -X POST "http://<reader-ip>/mkdir" --data-urlencode "path=/plugins/<name>"`,
     then for each file
     `curl -F "file=@device.json" "http://<reader-ip>/upload?path=/plugins/<name>"`.
     The user's X4 has been at 192.168.68.114.

   Then restart the reader (subscriptions are read at boot) and put it to sleep
   once. The image appears in `/informers/`; open it in the file browser, and
   Left/Right flips between informers.

## Design rules (480x800 e-ink)

- Every informer must look right in both outputs. With `depth=2` the BMP is
  2-bit with the panel's four native levels (`BLACK` 0, `DARK_GRAY` 85, `GRAY`
  170, `WHITE` 255) and text is anti-aliased. With `depth=1` it is 1-bit:
  text is solid, `GRAY` becomes a light dot pattern (1 pixel in 4 black) and
  `DARK_GRAY` a dark one (3 in 4). So: lines and text in `BLACK`; grays only
  for filled areas; nothing that matters may depend on gray alone. Use
  `c.gray` for purely decorative shading (the weather informer fills clouds
  and the tip box only in gray mode), and pick `DARK_GRAY` over `GRAY` when a
  fill must stay visible in black and white.
- Readable from arm's length: body text `F.body` (26px) or larger; `F.small`
  (20px) only for labels. Leave 24px side margins.
- Show when it was made (`om HH:MM`): the image only refreshes on sleep, so it
  can be hours old.
- One idea per line. If the audience is a child (the weather informer is for
  an 8-year-old), use simple Dutch: short sentences, everyday words, whole
  numbers, a concrete tip ("Neem je regenjas mee!").
- Fonts contain ASCII plus `°éëèêïöüáóúàç–·`. Any other character renders as
  `?`. To add characters or sizes, edit `scripts/bake_fonts.py` and run
  `python3 scripts/bake_fonts.py > src/fonts.js` (needs Noto Sans in
  `/usr/share/fonts/noto`, Pillow).
- Wrap long text with `paragraph()`; measure with `textWidth()` before placing
  things side by side.

## Limits

- Worker free plan: about 10 ms CPU per request. Drawing a full screen is a few
  ms; avoid per-pixel work over large areas beyond what `rect`/`circle` do.
- Any non-2xx response keeps the reader's previous image.
- Plugin system: one `sleep.enter` handler per plugin (one image per plugin),
  8 subscribed plugins, 4 handlers per sleep (the rest on the next sleep),
  1 MB per download, config values are substituted without URL-encoding (use
  `%20` for spaces).
- The download only happens on sleep, with battery at least 20% and a saved
  WiFi network. There is no "refresh now".

See `references/canvas-api.md` for the full drawing API.
