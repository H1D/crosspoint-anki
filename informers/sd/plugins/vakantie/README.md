Holiday countdown informer (Vakantie)

How many nights still to sleep until the next Dutch school holiday, in simple Dutch for children: the number of days, which holiday it is, when it starts and ends, a box per night still to sleep, and the two holidays after that. While a holiday is running it counts the days of freedom left instead.

Dates come from the Rijksoverheid open data for school holidays, so they follow the official national calendar.

Each time the reader goes to sleep it joins WiFi, downloads the image, and saves it to /informers/2-vakantie.bmp.

To view it: open the file browser, go to /informers, and open the image. Left and Right flip between informers.

To show it on the sleep screen instead, set "dest" in config.json to /sleep.bmp and set Sleep Screen to Custom.

Settings, in config.json next to this file:
- server: the informer Worker address
- regio: your school holiday region, noord, midden, or zuid (Amsterdam is midden). Kerstvakantie and meivakantie are the same everywhere.
- depth: 2 for 4-level gray (smooth text, gray shading; X4 and other gray panels), 1 for black and white
- dest: where the image is saved

The image refreshes only when the reader sleeps; it shows the time it was made. Needs a battery of at least 20% and a saved WiFi network.
