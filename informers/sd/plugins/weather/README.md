Weather for kids (Weer)

How to see it:
1. Press the power button to put the reader to sleep, then wake it. While going to sleep the reader fetches a new forecast (it needs WiFi it has connected to before, and at least 20% battery).
2. Go to Browse Files, open the "informers" folder, and open 1-weather.bmp. Press Left or Right to flip to other informers. Press Back to leave.

What it shows, in simple Dutch: the temperature now, whether it will rain in the next two hours, what to wear, and the next four days. The time at the top ("om 18:43") says when the forecast was made.

If the time does not change after a sleep, the reader could not get online that time; it keeps the last forecast and tries again next sleep.

Changing the location or other settings: edit config.json in this plugin's folder on a computer (take out the SD card, or use File Transfer). The reader cannot edit it itself.
- lat, lon: the location, in decimal degrees (Netherlands only)
- place: the name shown at the top; write spaces as %20
- depth: 2 for gray shades (this reader supports them), 1 for plain black and white
- lang: nl for Dutch (the default), en for English
- dest: the image file. Set it to /sleep.bmp and choose Settings, Sleep Screen, Custom to show the forecast as the sleep picture instead.

About "Receives: sleep and current book": the reader tells every plugin that listens for sleep which book is open. This plugin does not use or send it; it only downloads the forecast image.
