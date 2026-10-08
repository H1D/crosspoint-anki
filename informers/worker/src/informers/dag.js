// One child's day on one screen: the weather, the day's agenda, and school
// news. `offset=1|2` draws tomorrow or the day after, so a plugin can save
// three days at once (today on the sleep screen, the next two in /informers).
//
// The agenda comes from a job on Hermes's host (informers/agenda/) that reads
// the family calendars and pushes each child's events for the next days here
// (PUT /agenda/data). An event that is not part of a series is drawn white on
// black, so a one-off appointment stands out from the weekly routine.
import { BLACK, DARK_GRAY, GRAY, WHITE } from "../canvas.js";
import * as F from "../fonts.js";
import { drawIcon, iconFor } from "../icons.js";
import { clock, dateLong, langOf } from "../time.js";
import { HttpError, KEY as SCHOOL_KEY, STALE_HOURS, amsDate, dayNr, fold, itemBlock, pick, relDay, T as SCHOOL_T, wrap } from "./school.js";
import { T as WEATHER_T, feelSentence, loadWeather, rainChanceIndex, rainSentence, skySentence, tips } from "./weather.js";
import { WET_MM, dryText, hourOf, hourRange, hoursOf, rainChart, rainWhen, startOfHour } from "../rain.js";

const AGENDA_KEY = "agenda";
const DAY = 86400000;
const AGENDA_STALE_HOURS = 12;

const T = {
  nl: {
    rel: ["Vandaag", "Morgen", "Overmorgen"],
    agenda: "AGENDA",
    school: "SCHOOL",
    allDay: "hele dag",
    noEvents: "Niets in de agenda.",
    noAgenda: "Agenda is nog niet gekoppeld.",
    unknown: "Agenda nog niet bekend.",
    noNews: "Geen schoolnieuws.",
    more: (n) => `+${n} meer`,
    fresh: (a, s) => [a && `agenda ${a}`, s && `school ${s}`].filter(Boolean).join(" · "),
    staleAgenda: "Let op: agenda is oud",
    at: "om",
  },
  en: {
    rel: ["Today", "Tomorrow", "Day after tomorrow"],
    agenda: "AGENDA",
    school: "SCHOOL",
    allDay: "all day",
    noEvents: "Nothing in the calendar.",
    noAgenda: "Calendar not connected yet.",
    unknown: "Calendar not read yet.",
    noNews: "No school news.",
    more: (n) => `+${n} more`,
    fresh: (a, s) => [a && `calendar ${a}`, s && `school ${s}`].filter(Boolean).join(" · "),
    staleAgenda: "Old calendar!",
    at: "at",
  },
};

// Accepts the calendar job's push: { updated, first, last (the days it covers),
// kids: { <id>: { events: [...] } } },
// each event { day: "YYYY-MM-DD", start, end ("HH:MM", absent for all-day),
// title, where?, recurring }. Checked lightly so a bad push can never break rendering.
export async function pushAgenda(request, env) {
  if (!env.SCHOOL || !env.SCHOOL_PUSH_KEY) throw new HttpError(500, "store not configured");
  if (request.headers.get("Authorization") !== `Bearer ${env.SCHOOL_PUSH_KEY}`) throw new HttpError(403, "forbidden");
  let data;
  try {
    data = await request.json();
  } catch {
    throw new HttpError(400, "body is not JSON");
  }
  if (!data || Number.isNaN(Date.parse(data.updated)) || !data.kids || typeof data.kids !== "object" || Array.isArray(data.kids)) {
    throw new HttpError(400, "need updated (a date) and kids");
  }
  const clockRe = /^\d\d:\d\d$/;
  const dayRe = /^\d{4}-\d\d-\d\d$/;
  if ((data.first != null && !dayRe.test(data.first)) || (data.last != null && !dayRe.test(data.last))) {
    throw new HttpError(400, "first/last must be YYYY-MM-DD");
  }
  for (const [id, kid] of Object.entries(data.kids)) {
    if (!/^[a-z0-9-]+$/.test(id) || !Array.isArray(kid.events)) throw new HttpError(400, `bad kid ${id}`);
    for (const ev of kid.events) {
      const ok =
        dayRe.test(ev.day ?? "") &&
        typeof ev.title === "string" &&
        typeof ev.recurring === "boolean" &&
        (ev.start == null || clockRe.test(ev.start)) &&
        (ev.end == null || clockRe.test(ev.end));
      if (!ok) throw new HttpError(400, `bad event in ${id}`);
    }
  }
  await env.SCHOOL.put(AGENDA_KEY, JSON.stringify(data));
  return Response.json({ ok: true, kids: Object.keys(data.kids), events: Object.values(data.kids).map((k) => k.events.length) });
}

// Today: the station now and rain from now on. Later days: the forecast. Rain
// gets a sentence saying when, and a timeline when there is any.
function weatherBlock(c, w, offset, targetIso, lang, y) {
  const L = WEATHER_T[lang];
  const W = c.width;
  const now = Date.now();
  const range = hourRange(offset, hourOf(now));
  // Today's chart starts at this hour, or at 7 when it is still night.
  const showNow = offset === 0 && range.from === hourOf(now);
  const startMs = startOfHour(now) + (range.from - hourOf(now)) * 3600000;
  // The radar nowcast is better than the model for the next two hours; without
  // the hourly forecast, today's timeline still shows the nowcast alone.
  const nowcast = offset === 0 && w.slots.length ? w.slots : null;
  let hours = hoursOf(w.hourly, targetIso, range);
  if (!hours && nowcast) hours = Array.from({ length: range.to - range.from }, (_, i) => ({ hour: range.from + i, mm: 0, p: 0 }));
  if (hours && nowcast) {
    for (const s of nowcast) {
      const col = Math.floor((s.time - startMs) / 3600000);
      if (col < 0 || col >= hours.length) continue;
      if (!hours[col].fromNowcast) Object.assign(hours[col], { mm: 0, p: 100, fromNowcast: true });
      hours[col].mm = Math.max(hours[col].mm, s.mm);
    }
  }
  const wet = hours?.some((h) => h.mm >= WET_MM);
  const likelyWet = hours?.some((h) => h.mm >= WET_MM && h.p >= 50);

  let temp;
  let tipList;
  if (offset === 0) {
    temp = w.temp;
    drawIcon(c, w.kind === "sun" && w.isNight ? "partly" : w.kind, 72, y + 62, 108);
    c.text(`${temp}°`, 150, y + 74, F.big);
    tipList = tips(temp, w.station.windspeedBft ?? 0, w.rainSoon || likelyWet, L);
  } else {
    const d = w.days.find((day) => day.day.slice(0, 10) === targetIso);
    if (!d) return y;
    temp = d.maxtemperatureMax;
    drawIcon(c, iconFor(d.weatherdescription), 72, y + 62, 108);
    c.text(`${temp}° / ${d.mintemperatureMin}°`, 150, y + 74, F.big);
    tipList = tips(temp, d.wind ?? 0, hours ? likelyWet : rainChanceIndex(d.rainChance) === 2, L);
  }
  c.text(feelSentence(temp, L), 150, y + 110, F.bodyBold);

  // When: the nowcast's minute-precise sentence if rain is under two hours
  // away, else the windows over the day.
  let when;
  if (nowcast && w.rainSoon && showNow) when = rainSentence(nowcast, L);
  else if (hours) when = rainWhen(hours, lang) ?? dryText(offset, lang);
  else when = offset === 0 ? skySentence(w.kind, w.isNight, L) : null;
  let by = y + 140;
  if (when) {
    const lines = wrap(c, when, F.small, W - 174, 2);
    lines.forEach((l, i) => c.text(l, 150, by + i * 26, F.small));
    by += (lines.length - 1) * 26;
  }
  by += 14;
  if (wet) by = rainChart(c, hours, 24, by, W - 48, lang, { nowcast, startMs, showNow }) + 4;
  return tipBox(c, tipList, by);
}

// The most useful tip only (raincoat first): one line that always fits.
function tipBox(c, list, y) {
  const W = c.width;
  const line = wrap(c, list[0] ?? "", F.bodyBold, W - 72, 1)[0] ?? "";
  if (c.gray) c.rect(20, y, W - 40, 44, GRAY);
  c.frame(20, y, W - 40, 44, 2);
  c.text(line, W / 2, y + 31, F.bodyBold, { align: "center" });
  return y + 56;
}

function sectionBar(c, label, y) {
  const w = c.textWidth(label, F.small);
  c.text(label, 24, y + 20, F.small);
  c.rect(34 + w, y + 12, c.width - 58 - w, 2);
  return y + 30;
}

// School off (or a late start) on this day: one white-on-black bar.
function schoolAlarm(c, alarm, item, L, y) {
  const W = c.width;
  const lines = wrap(c, alarm.what || item.title, F.title, W - 104, 2);
  const h = 20 + lines.length * 46;
  c.rect(16, y, W - 32, h, BLACK);
  c.text("!", 44, y + h / 2 + 18, F.big, { align: "center", color: WHITE });
  lines.forEach((l, i) => c.text(l, 76, y + 52 + i * 46, F.title, { color: WHITE }));
  return y + h + 10;
}

// "15:30–16:30", "15:30", "–02:00" (the tail of last night's event), or "all day".
const when = (ev, L) => (ev.start ? (ev.end ? `${ev.start}–${ev.end}` : ev.start) : ev.end ? `–${ev.end}` : L.allDay);

// One-off event: white on black with a "!" so it cannot be missed. Returns the
// height it needs; draws only when `draw` is set.
function oneOff(c, ev, y, L, draw) {
  const W = c.width;
  const title = wrap(c, ev.title, F.bodyBold, W - 112, 2);
  const where = ev.where ? wrap(c, ev.where, F.small, W - 112, 1) : [];
  const h = 38 + title.length * 32 + where.length * 26 + 8;
  if (!draw) return h + 8;
  c.rect(16, y, W - 32, h, BLACK);
  c.text("!", 44, y + h / 2 + 18, F.big, { align: "center", color: WHITE });
  let by = y + 32;
  c.text(when(ev, L), 76, by, F.small, { color: WHITE });
  for (const l of title) c.text(l, 76, (by += 32), F.bodyBold, { color: WHITE });
  for (const l of where) c.text(l, 76, (by += 26), F.small, { color: WHITE });
  return h + 8;
}

// Part of a series: one quiet line, time then title.
function routine(c, ev, y, L, draw) {
  if (!draw) return 36;
  const label = ev.start ?? (ev.end ? `–${ev.end}` : L.allDay);
  c.text(label, 24, y + 26, F.bodyBold);
  const title = wrap(c, ev.where ? `${ev.title} (${ev.where})` : ev.title, F.body, c.width - 140, 1)[0] ?? "";
  c.text(title, 116, y + 26, F.body);
  return 36;
}

const eventBlock = (c, ev, y, L, draw) => (ev.recurring ? routine : oneOff)(c, ev, y, L, draw);

export default {
  title: "Dag",
  // Query: kid, key (school read key), offset=0|1|2, lat, lon, depth,
  // vandaag=YYYY-MM-DD to pretend it is another day, lang=en.
  async render(c, params, env) {
    const lang = langOf(params);
    const L = T[lang];
    const SL = SCHOOL_T[lang];
    if (!env.SCHOOL_READ_KEY || params.get("key") !== env.SCHOOL_READ_KEY) throw new HttpError(403, "forbidden");
    const kidId = (params.get("kid") || "").toLowerCase();
    const offset = Math.min(2, Math.max(0, Math.trunc(Number(params.get("offset"))) || 0));

    const [school, agenda, weather] = await Promise.all([
      env.SCHOOL.get(SCHOOL_KEY, "json"),
      env.SCHOOL.get(AGENDA_KEY, "json"),
      loadWeather(params).catch((err) => {
        console.error("dag weather", err);
        return null;
      }),
    ]);
    const kid = school?.kids?.[kidId];
    const kidAgenda = agenda?.kids?.[kidId];
    if ((school || agenda) && !kid && !kidAgenda) throw new HttpError(404, "unknown kid");

    const fake = params.get("vandaag");
    const now = fake ? Date.parse(`${fake}T12:00:00Z`) : Date.now();
    const today = dayNr(fake || amsDate.format(now));
    const target = today + offset;
    const targetIso = new Date(target * DAY).toISOString().slice(0, 10);
    const targetMs = target * DAY + DAY / 2;
    const W = c.width;
    const H = c.height;
    const bottom = H - 40;

    // Header: the day in big letters, so "Morgen" never reads as today.
    c.text(L.rel[offset], 24, 50, F.title);
    const name = kid?.name ?? kidAgenda?.name ?? kidId;
    c.text(fold(name), W - 24, 48, F.bodyBold, { align: "right" });
    c.text(dateLong(targetMs, lang), 24, 86, F.body);
    c.text(`${L.at} ${clock(Date.now())}`, W - 24, 86, F.small, { align: "right" });
    c.rect(24, 100, W - 48, 3);

    let y = 108;
    if (weather) y = weatherBlock(c, weather, offset, targetIso, lang, y);

    // School off on this day changes the whole day: it goes first.
    const picked = kid ? pick(kid.items, today) : { alarms: [], list: [] };
    const dayAlarm = picked.alarms.find((it) => dayNr(it.alarm.day) === target);
    if (dayAlarm) y = schoolAlarm(c, dayAlarm.alarm, dayAlarm, L, y);

    const news = [
      ...picked.alarms.filter((it) => dayNr(it.alarm.day) > target).map((it) => ({ ...it, kind: "alarm", day: it.alarm.day })),
      ...picked.list.filter((it) => (it.day ? dayNr(it.day) === target : offset === 0)),
    ];

    y = sectionBar(c, L.agenda, y);
    const events = (kidAgenda?.events ?? [])
      .filter((ev) => ev.day === targetIso)
      .sort((a, b) => ((a.start ?? "") < (b.start ?? "") ? -1 : (a.start ?? "") > (b.start ?? "") ? 1 : 0));
    // A day the last push did not cover (job not run yet today, or failing) is
    // unknown, not empty.
    const covered = kidAgenda && (!agenda.last || (targetIso >= agenda.first && targetIso <= agenda.last));
    if (!covered || !events.length) {
      c.text(!kidAgenda ? L.noAgenda : !covered ? L.unknown : L.noEvents, 24, y + 26, F.body);
      y += 40;
    } else {
      // Leave room for the "+N more" line, the school bar, and one school item
      // (or its "no news" line); one-offs claim space before the routine, then
      // everything is drawn in time order.
      const agendaBottom = bottom - 24 - 34 - (news.length ? 130 : 40);
      const fits = new Set();
      let used = 0;
      for (const ev of [...events.filter((e) => !e.recurring), ...events.filter((e) => e.recurring)]) {
        const h = eventBlock(c, ev, 0, L, false);
        if (y + used + h > agendaBottom) continue;
        fits.add(ev);
        used += h;
      }
      for (const ev of events) if (fits.has(ev)) y += eventBlock(c, ev, y, L, true);
      const hidden = events.length - fits.size;
      if (hidden) {
        c.text(L.more(hidden), c.width - 24, y + 18, F.small, { align: "right" });
        y += 24;
      }
    }

    // School news about this day, then (today only) the latest news.
    y = sectionBar(c, L.school, y + 4);
    const queue = news;
    let shown = 0;
    for (const it of queue) {
      const next = itemBlock(c, it, today, y, 2, bottom, SL);
      if (next === null) break;
      y = next;
      shown++;
    }
    if (!queue.length) c.text(kid ? L.noNews : SL.empty, 24, y + 26, F.body);

    // Footer: how fresh each source is.
    c.rect(24, H - 34, W - 48, 1);
    const ageH = (iso) => (now - Date.parse(iso)) / 3600000;
    if (kidAgenda && ageH(agenda.updated) > AGENDA_STALE_HOURS) {
      c.text(L.staleAgenda, 24, H - 10, F.bodyBold);
    } else if (school && ageH(school.updated) > STALE_HOURS) {
      c.text(SL.stale(relDay(amsDate.format(Date.parse(school.updated)), today, SL)), 24, H - 10, F.bodyBold);
    } else {
      c.text(L.fresh(agenda && clock(Date.parse(agenda.updated)), school && clock(Date.parse(school.updated))), 24, H - 10, F.small);
    }
    if (queue.length > shown) c.text(SL.more(queue.length - shown), W - 24, H - 10, F.small, { align: "right" });
    return c;
  },
};
