Weather informer (Weer)

A forecast in simple Dutch for children: temperature, rain in the next two hours (Buienalarm), a clothing tip, and the next four days (Buienradar).

Each time the reader goes to sleep it joins WiFi, downloads the image, and saves it to /informers/1-weather.bmp.

To view it: open the file browser, go to /informers, and open the image. Left and Right flip between informers.

To show it on the sleep screen instead, set "dest" in config.json to /sleep.bmp and set Sleep Screen to Custom.

Settings, in config.json next to this file:
- server: the informer Worker address
- lat, lon: your location (Netherlands only; the data covers NL)
- place: name at the top of the image (use %20 for spaces)
- dest: where the image is saved

The image refreshes only when the reader sleeps; it shows the time it was made. Needs a battery of at least 20% and a saved WiFi network.
