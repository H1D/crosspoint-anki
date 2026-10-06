# Informers

Small screens (weather, calendar, ...) rendered on a server and shown by the
reader as plain images. The firmware stays a thin client: each informer is an
SD-card plugin whose `sleep.enter` handler downloads a finished BMP (see
`docs/plugin-events.md`). Adding an informer never needs a firmware build.

## Pieces

- `server/informer_server.py` renders 480x800 1-bit BMPs. Needs Python 3 and
  Pillow. Data for `/weather.bmp` comes from Open-Meteo (no API key).
- `sd/plugins/<name>/` is the plugin folder to copy onto the SD card.
  `device.json` declares the download; `config.json` holds the server address,
  location, and destination path.
- `sd/informers/` is where the images land. Create this folder on the card:
  the download does not create missing folders.

## Use

1. Run the server on a machine the reader can reach:
   `python3 informers/server/informer_server.py --port 8790`
2. Copy `sd/plugins/weather` to `/plugins/weather` on the card, create
   `/informers`, and edit `config.json` (server, lat, lon, place).
3. Connect the reader to WiFi once, then restart it so the plugin subscribes.
4. Put the reader to sleep. It joins WiFi (battery at least 20%), downloads the
   image, and sleeps.
5. Open `/informers` in the file browser and pick an image. Left and Right flip
   between informers; files are ordered by name, hence the number prefixes.

To show an informer as the sleep screen, set its `dest` to `/sleep.bmp` and
set Sleep Screen to Custom. Images refresh only on sleep; each one prints the
time it was rendered.

## Adding an informer

Add a route to the server that returns a BMP, then copy the weather plugin
folder, point `url` at the new route, and pick a new `dest` such as
`/informers/2-calendar.bmp`. Each plugin has one `sleep.enter` handler, so one
plugin produces one image. Up to 8 plugins can subscribe to events and 4
handlers run per sleep; extra informers refresh on the next sleep.
