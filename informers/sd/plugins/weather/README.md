Weather informer

Each time the reader goes to sleep it joins WiFi, downloads a forecast image from your informer server, and saves it to /informers/1-weather.bmp.

To view it: open the file browser, go to /informers, and open the image. Left and Right flip between informers.

To show the forecast on the sleep screen, set "dest" in config.json to /sleep.bmp and set Sleep Screen to Custom.

Setup (in config.json, next to this file):
- server: address of the informer server, e.g. http://192.168.1.10:8790
- lat, lon: your location
- place: label for the top of the image (use %20 for spaces)
- dest: where the image is saved

The image is refreshed on sleep only; it shows the time it was made. Needs battery of at least 20% and a saved WiFi network.
