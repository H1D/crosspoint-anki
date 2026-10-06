# School news informer

Each child's reader shows that child's own Parro news, rewritten into short
simple Dutch. A "no school / stay home / later start / earlier out" message for
today or tomorrow becomes a white-on-black block at the top of the screen.

Parro needs a login, so the Worker never talks to it. A job on Hermes's host
(clawd) does the work and pushes the result:

1. `school_news.py` pulls the announcements through the `parro-news` skill's
   helpers (its token cache, `~/.config/parro/tokens.json`).
2. Jev (OpenRouter decisions API, `~typesafe/jev-latest`) answers three things
   per message in one call: its kind (info, bring/do, activity, school off,
   parents only), whether it means school is off on some day, and, per class
   (plein), whether it is only about that class.
3. A message goes to a child when it was posted in one of the child's groups
   and is not only about another child's class. Parents-only messages are
   dropped.
4. One tool-less `hermes chat` call rewrites all new messages at once: a title
   of at most 28 characters, at most 110 characters of text addressed to the
   child, the day it is about, and the alarm (day and a short phrase).
   Relative words ("morgen") are resolved against the posting date and
   avoided; the Worker turns days back into "vandaag", "morgen", "vrijdag" at
   render time. If the LLM fails, the original title and first sentence are
   used and a likely school-off message (Jev) still raises the alarm, reading
   "Kijk in Parro!".
5. The result is `PUT` to `https://crosspoint-informers.doorcomp.workers.dev/school/data`
   (Bearer `SCHOOL_PUSH_KEY`) and stored in KV. `GET /school.bmp?kid=<id>&key=<SCHOOL_READ_KEY>&depth=2`
   draws one child's screen.

Jev answers and rewrites are cached per message (`~/.config/parro/school-informer-state.json`),
so a run with no new messages takes a few seconds; a run with new ones about two minutes.

## Schedule, alarms, manual refresh

- Hermes cron jobs `school-informer:morning` (`50 5,6 * * *`) and
  `school-informer:evening` (`0 17,18 * * *`) run `cron.sh` (installed as
  `~/.hermes/scripts/school_informer_{morning,evening}.sh`). Hermes cron runs
  in UTC, so both DST candidates are scheduled and the script only acts at
  07:50 and 19:00 Amsterdam time.
- With `--notify` (the cron jobs) the script prints a loud Russian message for
  every school-off item for today or tomorrow, once per item and day, and Hermes
  delivers it to Telegram. So the 19:00 run warns about tomorrow and the 07:50
  run warns again on the day.
- Manual refresh: ask Hermes ("обнови школьные новости на ридерах"), or on
  clawd run `uv run ~/.hermes/scripts/school_informer.py run -v`.
- Readers download when they go to sleep, and wake themselves at 07:55 and
  19:05 (the plugins' `wake` times) to download and redraw the sleep screen.
  The `school-sleep` plugin puts the picture on the sleep screen, so an alarm
  is the first thing a child sees even if nobody touched the reader.

## Setup on clawd

- Script: `~/.hermes/scripts/school_informer.py` (this folder's `school_news.py`).
- Config: `~/.config/parro/school-informer.json`, shaped like
  `school-informer.example.json`: `kids.<id>.groups` lists the Parro group names
  the child belongs to; `subgroups` describes each class for Jev's "only about"
  question. The kid id is what the reader's plugin config uses (`kid`).
- Secrets: `~/.hermes/secrets/crosspoint-school.env` (`SCHOOL_PUSH_KEY`,
  `SCHOOL_READ_KEY`), `OPENROUTER_API_KEY` from `~/.hermes/.env`. On brick the
  same keys are in `~/.config/agent-secrets/crosspoint/school-keys.cred`. The
  Worker has them as secrets of the same name.
- Log: `~/logs/school-informer.log`.

## Testing

```sh
# on clawd: what would be pushed if it were 7 September, 19:00
uv run school_news.py run --dry-run --now 2026-09-07T19:00 --out /tmp/sep7.json -v
# on brick: draw it as that day
SCHOOL_JSON=/tmp/sep7.json SCHOOL_READ_KEY=x node ../worker/scripts/render.mjs school /tmp/s.bmp "kid=kid1&key=x&vandaag=2026-09-07&depth=2"
python3 ../worker/scripts/preview.py /tmp/s.bmp /tmp/s.png
```

`HERMES_BIN=/bin/false` simulates the LLM being down; `SCHOOL_INFORMER_STATE`
points the cache elsewhere so a test does not touch the real one.
