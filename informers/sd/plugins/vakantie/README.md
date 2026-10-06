Holiday countdown (Vakantie)

How to see it:
1. Press the power button to put the reader to sleep, then wake it. While going to sleep the reader fetches a new countdown (it needs WiFi it has connected to before, and at least 20% battery).
2. Go to Browse Files, open the "informers" folder, and open 2-vakantie.bmp. Press Left or Right to flip to other informers. Press Back to leave.

What it shows, in simple Dutch: how many days until the next school holiday and which one, its dates, a box for every night still to sleep, and the two holidays after it. During a holiday it counts the free days left. Dates come from the official Dutch school holiday calendar (Rijksoverheid). The time at the top says when the image was made.

Changing settings: edit config.json in this plugin's folder on a computer (take out the SD card, or use File Transfer). The reader cannot edit it itself.
- regio: your school holiday region, noord, midden, or zuid (Amsterdam is midden)
- depth: 2 for gray shades (this reader supports them), 1 for plain black and white
- dest: the image file. Set it to /sleep.bmp and choose Settings, Sleep Screen, Custom to show it as the sleep picture instead.

About "Receives: sleep and current book": the reader tells every plugin that listens for sleep which book is open. This plugin does not use or send it.
