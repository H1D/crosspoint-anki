School news on the sleep screen

Shows the school news picture (see the "school" plugin) as the picture the reader shows while it sleeps, so a "no school tomorrow" message is the first thing a child sees when picking up the reader.

Setup:
1. Settings, Sleep Screen: choose Custom. For gray shades, set the sleep cover filter to none.
2. Put the reader to sleep once. While going to sleep it fetches the latest school news (it needs WiFi it has connected to before, and at least 20% battery) and shows it right away.

The picture is only updated when the reader goes to sleep. If the reader has been asleep since before the news came in (07:50 and 19:00), it still shows the older picture; the bottom line says when the news was collected.

Changing settings: edit config.json in this plugin's folder on a computer (take out the SD card, or use File Transfer). Use the same kid and key as the "school" plugin.
- kid: which child this reader belongs to
- key: the access code for the news service (ask Hermes; keep it private)
- depth: 2 for gray shades, 1 for plain black and white
- dest: keep /sleep.bmp

About "Receives: sleep and current book": the reader tells every plugin that listens for sleep which book is open. This plugin does not use or send it.
