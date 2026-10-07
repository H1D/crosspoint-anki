#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["httpx>=0.27", "icalendar>=6", "recurring-ical-events>=3"]
# ///
"""Calendar agenda for the CrossPoint `dag` informer.

Reads each child's calendars (Google Calendar secret iCal addresses), expands
repeating events, and pushes the events of the next few days to the informers
Worker (PUT /agenda/data). Every event carries `recurring`: false for a
one-off, which the reader draws white on black.

Runs on Hermes's host:

  agenda.py run [--dry-run] [--out FILE] [--now 2026-10-07T07:00] [-v]

--dry-run  do not push; with --out, write the JSON that would be pushed.
--now      pretend it is that moment (Amsterdam time).

Config: ~/.config/crosspoint/agenda.json (see agenda.example.json). A calendar
is named by the environment variable holding its iCal address; `match` keeps
only events whose title or description has a word starting with one of the
listed names or stems (for a shared family calendar).
Secrets: the iCal addresses in ~/.hermes/secrets/crosspoint-agenda.env,
SCHOOL_PUSH_KEY (the Worker's push key) in ~/.hermes/secrets/crosspoint-school.env.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from datetime import date, datetime, time, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

import httpx
import icalendar
import recurring_ical_events

AMS = ZoneInfo("Europe/Amsterdam")
HOME = Path.home()
CONFIG = HOME / ".config/crosspoint/agenda.json"
SECRET_FILES = [HOME / ".hermes/secrets/crosspoint-agenda.env", HOME / ".hermes/secrets/crosspoint-school.env"]
LOG = HOME / ".hermes/logs/agenda-informer.log"
MAX_EVENTS_PER_DAY = 12


def log(msg: str) -> None:
    LOG.parent.mkdir(parents=True, exist_ok=True)
    with LOG.open("a") as f:
        f.write(f"[{datetime.now(AMS):%Y-%m-%d %H:%M:%S}] {msg}\n")


def load_secrets() -> None:
    """Reads KEY=value lines into the environment without overriding it."""
    for path in SECRET_FILES:
        if not path.exists():
            continue
        for line in path.read_text().splitlines():
            m = re.match(r"\s*(?:export\s+)?([A-Z_][A-Z0-9_]*)=(.*)", line)
            if m and m.group(1) not in os.environ:
                os.environ[m.group(1)] = m.group(2).strip().strip("'\"")


def fetch_calendar(url: str) -> icalendar.Calendar:
    r = httpx.get(url, timeout=30, follow_redirects=True)
    r.raise_for_status()
    return icalendar.Calendar.from_ical(r.content)


def series_uids(cal: icalendar.Calendar) -> set[str]:
    """UIDs of repeating events. Expanded occurrences lose RRULE and all get a
    RECURRENCE-ID, so the answer comes from the calendar as stored: a series
    master has RRULE/RDATE, a moved occurrence RECURRENCE-ID."""
    return {
        str(ev.get("UID"))
        for ev in cal.walk("VEVENT")
        if any(k in ev for k in ("RRULE", "RDATE", "RECURRENCE-ID"))
    }


def local(dt) -> datetime:
    if dt.tzinfo is None:
        return dt.replace(tzinfo=AMS)
    return dt.astimezone(AMS)


def day_events(ev, recurring: bool, first: date, last: date) -> list[dict]:
    """The event's entries per day within [first, last]: an all-day or multi-day
    event appears on every day it covers. The first day carries the start, the
    last day the end; a short event that ends early the next morning (a late
    party) stays on its first day only."""
    start = ev.decoded("DTSTART")
    end = ev.decoded("DTEND") if "DTEND" in ev else None
    title = str(ev.get("SUMMARY", "")).strip()
    where = str(ev.get("LOCATION", "")).strip()
    base = {"title": title, "recurring": recurring}
    if where:
        base["where"] = where[:80]
    out = []
    if not isinstance(start, datetime):  # all-day: DTEND is exclusive
        stop = end if isinstance(end, date) and end > start else start + timedelta(days=1)
        d = max(start, first)
        while d < stop and d <= last:
            out.append({**base, "day": d.isoformat()})
            d += timedelta(days=1)
        return out
    s = local(start)
    e = local(end) if isinstance(end, datetime) else s
    d = max(s.date(), first)
    while d <= min(e.date(), last):
        entry = {**base, "day": d.isoformat()}
        if d == s.date():
            entry["start"] = f"{s:%H:%M}"
            if e.date() == d and e > s:
                entry["end"] = f"{e:%H:%M}"
        elif d == e.date():
            if e.time() <= time(4) and e - s < timedelta(hours=24):
                break
            entry["end"] = f"{e:%H:%M}"
        out.append(entry)
        d += timedelta(days=1)
    return out


def names_kid(ev, words: list[str]) -> bool:
    """True when a word in the title or description starts with one of `words`,
    so a stem covers declined forms ("Тим" matches Тима, Тимы, Тимофей)."""
    text = f"{ev.get('SUMMARY', '')} {ev.get('DESCRIPTION', '')}".lower()
    return any(re.search(rf"(?<!\w){re.escape(w.lower())}", text) for w in words)


def kid_events(kid: dict, first: date, last: date, cache: dict) -> list[dict]:
    events = []
    for cal in kid["calendars"]:
        env = cal["env"]
        url = os.environ.get(env)
        if not url:
            raise RuntimeError(f"{env} is not set (see {SECRET_FILES[0]})")
        if env not in cache:
            parsed = fetch_calendar(url)
            cache[env] = (parsed, series_uids(parsed))
        parsed, series = cache[env]
        window = (datetime.combine(first, time(0), AMS), datetime.combine(last + timedelta(days=1), time(0), AMS))
        for ev in recurring_ical_events.of(parsed).between(*window):
            if str(ev.get("STATUS", "")).upper() == "CANCELLED":
                continue
            if cal.get("match") and not names_kid(ev, cal["match"]):
                continue
            events.extend(day_events(ev, str(ev.get("UID")) in series, first, last))
    # Same event in two calendars (a shared invite): keep one, a one-off wins.
    seen: dict[tuple, dict] = {}
    for e in events:
        key = (e["day"], e.get("start"), e["title"].lower())
        if key not in seen or (seen[key]["recurring"] and not e["recurring"]):
            seen[key] = e
    out = sorted(seen.values(), key=lambda e: (e["day"], e.get("start") or "", e["title"]))
    per_day: dict[str, int] = {}
    kept = []
    for e in out:
        per_day[e["day"]] = per_day.get(e["day"], 0) + 1
        if per_day[e["day"]] <= MAX_EVENTS_PER_DAY:
            kept.append(e)
    return kept


def run(args) -> int:
    now = datetime.fromisoformat(args.now).replace(tzinfo=AMS) if args.now else datetime.now(AMS)
    load_secrets()
    try:
        cfg = json.loads(CONFIG.read_text())
    except (OSError, ValueError) as err:
        print(f"agenda informer: no config at {CONFIG} ({err})", file=sys.stderr)
        return 2
    first = now.date()
    last = first + timedelta(days=cfg.get("days", 3) - 1)
    cache: dict = {}
    try:
        kids = {kid_id: {"name": k["name"], "events": kid_events(k, first, last, cache)} for kid_id, k in cfg["kids"].items()}
    except (httpx.HTTPError, RuntimeError, ValueError) as err:
        # Calendar addresses are secrets: never echo a URL.
        msg = re.sub(r"https?://\S+", "<calendar url>", str(err))
        print(f"agenda informer: {msg}")
        log(f"failed: {msg}")
        return 1
    data = {"updated": now.isoformat(timespec="seconds"), "first": first.isoformat(), "last": last.isoformat(), "kids": kids}
    if args.out:
        Path(args.out).write_text(json.dumps(data, ensure_ascii=False, indent=1))
    if args.verbose:
        for kid_id, kid in kids.items():
            print(f"[{kid_id}] {len(kid['events'])} events", file=sys.stderr)
            for e in kid["events"]:
                mark = "  " if e["recurring"] else "! "
                print(f"  {mark}{e['day']} {e.get('start', 'all-day'):>7} {e['title']}", file=sys.stderr)
    if args.dry_run:
        return 0
    r = httpx.put(
        f"{cfg['worker'].rstrip('/')}/agenda/data",
        json=data,
        headers={"Authorization": f"Bearer {os.environ['SCHOOL_PUSH_KEY']}"},
        timeout=30,
    )
    if r.status_code != 200:
        print(f"agenda informer: push failed {r.status_code}: {r.text[:200]}")
        log(f"push failed {r.status_code}: {r.text[:200]}")
        return 1
    log(f"pushed {r.text}")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="read calendars, push the next days")
    r.add_argument("--dry-run", action="store_true")
    r.add_argument("--out")
    r.add_argument("--now")
    r.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
