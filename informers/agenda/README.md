# Agenda for the day informer

The `dag` informer shows one child's day: weather, agenda, school news. The
agenda comes from the family's Google calendars. A job on Hermes's host
(clawd) reads them and pushes the next three days to the Worker; the Worker
never sees a calendar address.

1. `agenda.py` downloads each calendar from its secret iCal address, expands
   repeating events, and keeps the events of today and the next two days.
2. An event is `recurring` when it belongs to a series (RRULE/RDATE, or a moved
   occurrence of one). Everything else is a one-off, drawn white on black.
3. A calendar entry can carry `match`: then only events whose title or
   description names the child are kept (for a shared family calendar).
4. The result is `PUT` to `https://crosspoint-informers.doorcomp.workers.dev/agenda/data`
   (Bearer `SCHOOL_PUSH_KEY`, the same key as the school job) and stored in KV.
   `GET /dag.bmp?kid=<id>&key=<SCHOOL_READ_KEY>&offset=0|1|2` draws a day.

## Giving read access

A secret iCal address is read-only and can be revoked (reset) at any time.
For each calendar, in Google Calendar on the web (signed in as the calendar's
owner): Settings → the calendar under "Settings for my calendars" →
"Integrate calendar" → "Secret address in iCal format" → copy.

Put the addresses on clawd, never in this repo:

```sh
ssh clawd
umask 077; nano ~/.hermes/secrets/crosspoint-agenda.env
```

```sh
AGENDA_ICS_LISA=https://calendar.google.com/calendar/ical/.../private-.../basic.ics
AGENDA_ICS_TIMA=https://calendar.google.com/calendar/ical/.../private-.../basic.ics
AGENDA_ICS_FAMILY=https://calendar.google.com/calendar/ical/.../private-.../basic.ics
```

To revoke: "Reset" next to the secret address in the same settings page.

## Setup on clawd

- Script: `~/.hermes/scripts/agenda_informer.py` (this folder's `agenda.py`).
- Config: `~/.config/crosspoint/agenda.json`, shaped like `agenda.example.json`.
  The kid ids must match the school job's (`lisa`, `tima`).
- Cron: Hermes job `agenda-informer` running `cron.sh` (installed as
  `~/.hermes/scripts/agenda_informer.sh`) every hour at :40, so the readers'
  06:45 wake finds a fresh agenda.
- Try it: `uv run ~/.hermes/scripts/agenda_informer.py run --dry-run -v` lists
  the events per child, one-offs marked `!`.
