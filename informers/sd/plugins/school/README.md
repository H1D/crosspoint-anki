School news (Schoolnieuws)

How to see it:
1. Press the power button to put the reader to sleep, then wake it. While going to sleep the reader fetches the latest school news (it needs WiFi it has connected to before, and at least 20% battery).
2. Go to Browse Files, open the "informers" folder, and open 3-school.bmp. Press Left or Right to flip to other informers. Press Back to leave.

What it shows: this child's own news from Parro, the school app, in short simple Dutch. Messages for another class, and messages only for parents (money, forms, meetings), are left out. Each message has a label (LET OP, MEENEMEN / DOEN, ACTIVITEIT, INFO) and the day it is about ("morgen", "vrijdag"). If there is no school, or the children should stay home, today or tomorrow, a big black box at the top says so.

The news is collected from Parro at 07:50 and 19:00, and whenever you ask Hermes to refresh it. The reader picks it up the next time it goes to sleep. The bottom line says when the news was collected; "Let op: oud nieuws" means it is more than a day old. The time at the top says when the image was made.

To make school alarms impossible to miss, also install the "school-sleep" plugin: it puts the same picture on the sleep screen.

Changing settings: edit config.json in this plugin's folder on a computer (take out the SD card, or use File Transfer). The reader cannot edit it itself.
- kid: which child this reader belongs to (the name the news service uses, in lower case)
- key: the access code for the news service (ask Hermes; keep it private)
- depth: 2 for gray shades (this reader supports them), 1 for plain black and white
- dest: the image file

About "Receives: sleep and current book": the reader tells every plugin that listens for sleep which book is open. This plugin does not use or send it.
