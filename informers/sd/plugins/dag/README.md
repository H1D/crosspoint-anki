My day (Mijn dag)

How to see it:
1. Set Settings → Display → Sleep Screen to Custom. Today's picture is the sleep screen.
2. Press the power button to put the reader to sleep. While going to sleep the reader fetches three pictures when the last ones are more than 2 hours old (it needs WiFi it has connected to before, and at least 20% battery). It also wakes itself at 06:45, 07:55 and 19:05 to refresh them.
3. Tomorrow and the day after are in Browse Files → "informers": 0-morgen.bmp and 0-overmorgen.bmp. Press Left or Right to flip between informers. Press Back to leave.

What it shows, for one child and one day: the weather (today: now; tomorrow and the day after: the forecast) with, when it rains, a sentence and a row of hours with raindrops saying when (one drop: a little rain, three drops: heavy rain; black: rain is likely, gray: maybe; today the first two hours come from the rain radar), the day's agenda from the family calendars, and school news about that day. Something that happens only once (a dentist visit, a birthday) is white on black with a "!", so it stands out from the weekly routine. No school on that day puts a black bar at the top.

The agenda is read from the calendars every hour, the school news at 07:50 and 19:00. The bottom line says when each was last read.

This replaces the "school-sleep" plugin: both write the sleep screen, so keep only one.

Changing settings: edit config.json in this plugin's folder on a computer (take out the SD card, or use File Transfer).
- kid: which child this reader belongs to (lower case, as in the agenda and school jobs)
- key: the access code (the school read key; ask Hermes; keep it private)
- lat, lon: where to take the weather for
- depth: 2 for gray shades, 1 for plain black and white
- lang: nl for Dutch (the default), en for English
- dest_today, dest_tomorrow, dest_day_after: where the three pictures go

Needs firmware 1.6.5-anki.8 or newer (one plugin saving several pictures).
