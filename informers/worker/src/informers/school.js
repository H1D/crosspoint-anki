// School news from Parro for one child, rewritten into short simple Dutch.
// The Worker does not talk to Parro: a job on Hermes's host (informers/school/)
// pulls the news twice a day, sorts it per child with Jev, has an LLM shorten
// it, and pushes the result here (PUT /school/data). This module stores that
// push in KV and draws one child's screen from it.
//
// A "no school / stay home" message for today or tomorrow takes over the top
// half of the screen in white on black, so it cannot be missed.
import { BLACK, DARK_GRAY, GRAY, WHITE } from "../canvas.js";
import * as F from "../fonts.js";
import { clock, dateLong, langOf } from "../time.js";

const KEY = "data";
const DAY = 86400000;
const STALE_HOURS = 30; // two pulls a day; older than this means the job is not running
const KINDS = ["alarm", "todo", "event", "info"];
// Screen text; the news itself arrives already rewritten (Dutch from the job).
const T = {
  nl: {
    label: { alarm: "LET OP", todo: "MEENEMEN / DOEN", event: "ACTIVITEIT", info: "INFO" },
    rel: ["vandaag", "morgen", "overmorgen"],
    weekday: ["zondag", "maandag", "dinsdag", "woensdag", "donderdag", "vrijdag", "zaterdag"],
    month: ["jan", "feb", "mrt", "apr", "mei", "jun", "jul", "aug", "sep", "okt", "nov", "dec"],
    forKid: (n) => `voor ${n}`,
    at: "om",
    empty: "Geen nieuws van school. Fijne dag!",
    pulled: (t) => `nieuws van ${t}`,
    stale: (d) => `Let op: oud nieuws (${d})`,
    more: (n) => `+${n} meer in Parro`,
  },
  en: {
    label: { alarm: "HEADS UP", todo: "BRING / DO", event: "ACTIVITY", info: "INFO" },
    rel: ["today", "tomorrow", "day after tomorrow"],
    weekday: ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"],
    month: ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"],
    forKid: (n) => `for ${n}`,
    at: "at",
    empty: "No news from school. Have a nice day!",
    pulled: (t) => `news from ${t}`,
    stale: (d) => `Old news! (${d})`,
    more: (n) => `+${n} more in Parro`,
  },
};

export class HttpError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

const amsDate = new Intl.DateTimeFormat("en-CA", { timeZone: "Europe/Amsterdam", year: "numeric", month: "2-digit", day: "2-digit" });
const dayNr = (iso) => {
  const [y, m, d] = iso.slice(0, 10).split("-").map(Number);
  return Math.floor(Date.UTC(y, m - 1, d) / DAY);
};
// "vandaag", "morgen", "overmorgen", "vrijdag" (this week), or "wo 14 okt".
function relDay(day, today, L) {
  const n = dayNr(day) - today;
  if (n >= 0 && n < 3) return L.rel[n];
  const d = new Date(dayNr(day) * DAY);
  if (n > 2 && n < 7) return L.weekday[d.getUTCDay()];
  return `${L.weekday[d.getUTCDay()].slice(0, L === T.en ? 3 : 2)} ${d.getUTCDate()} ${L.month[d.getUTCMonth()]}`;
}

// The fonts hold ASCII plus a few accented letters. Fold everything else
// (curly quotes, other accents, emoji) so nothing renders as "?".
const SAME = { "‘": "'", "’": "'", "“": '"', "”": '"', "…": "...", "€": "EUR", "—": "–", " ": " " };
function fold(str) {
  let out = "";
  for (const ch of String(str ?? "")) {
    if (F.body.glyphs[ch]) out += ch;
    else if (SAME[ch]) out += SAME[ch];
    else {
      const base = ch.normalize("NFKD").replace(/[̀-ͯ]/g, "");
      if (base && [...base].every((b) => F.body.glyphs[b])) out += base;
    }
  }
  return out.replace(/\s+/g, " ").trim();
}

// Word-wraps into at most maxLines, ending the last one with "..." if cut.
function wrap(c, str, font, maxWidth, maxLines) {
  const words = fold(str).split(" ").filter(Boolean);
  const lines = [];
  let line = "";
  for (let i = 0; i < words.length; i++) {
    const next = line ? `${line} ${words[i]}` : words[i];
    if (!line || c.textWidth(next, font) <= maxWidth) {
      line = next;
      continue;
    }
    lines.push(line);
    line = words[i];
    if (lines.length === maxLines) {
      let last = lines[maxLines - 1];
      while (last && c.textWidth(`${last}...`, font) > maxWidth) last = last.replace(/\s*\S+$/, "");
      lines[maxLines - 1] = `${last}...`;
      return lines;
    }
  }
  if (line) lines.push(line);
  return lines;
}

// Accepts the job's push, after a light shape check so a bad push can never
// break rendering.
export async function push(request, env) {
  if (!env.SCHOOL || !env.SCHOOL_PUSH_KEY) throw new HttpError(500, "school store not configured");
  if (request.headers.get("Authorization") !== `Bearer ${env.SCHOOL_PUSH_KEY}`) throw new HttpError(403, "forbidden");
  let data;
  try {
    data = await request.json();
  } catch {
    throw new HttpError(400, "body is not JSON");
  }
  if (!data || typeof data.updated !== "string" || typeof data.kids !== "object") throw new HttpError(400, "need updated and kids");
  for (const [id, kid] of Object.entries(data.kids)) {
    if (!/^[a-z0-9-]+$/.test(id) || !Array.isArray(kid.items)) throw new HttpError(400, `bad kid ${id}`);
    for (const it of kid.items) {
      if (!KINDS.includes(it.kind) || typeof it.title !== "string") throw new HttpError(400, `bad item in ${id}`);
    }
  }
  await env.SCHOOL.put(KEY, JSON.stringify(data));
  return Response.json({ ok: true, kids: Object.keys(data.kids), items: Object.values(data.kids).map((k) => k.items.length) });
}

// What one child should see on `today`: alarms that are not over yet, then
// dated things still ahead, then recent news.
export function pick(items, today) {
  const recent = (it) => dayNr(it.posted) >= today - 10;
  const alarms = items.filter((it) => it.alarm?.day && dayNr(it.alarm.day) >= today).sort((a, b) => dayNr(a.alarm.day) - dayNr(b.alarm.day));
  const rest = items.filter((it) => !alarms.includes(it) && (it.day ? dayNr(it.day) >= today : recent(it)));
  const dated = rest.filter((it) => it.day).sort((a, b) => dayNr(a.day) - dayNr(b.day) || (a.posted < b.posted ? 1 : -1));
  const undated = rest.filter((it) => !it.day).sort((a, b) => (a.posted < b.posted ? 1 : -1));
  // Schools often post a reminder about the same thing; keep the newest one per day and subject.
  const firstWord = (it) => fold(it.title).toLowerCase().split(" ")[0];
  const list = [];
  for (const it of [...dated, ...undated]) {
    if (it.day && list.some((o) => o.day === it.day && firstWord(o) === firstWord(it))) continue;
    list.push(it);
  }
  return { alarms, list };
}

// White on black across the top: "MORGEN" / "Geen school", plus one line of detail.
function alarmBlock(c, alarm, item, today, y, L) {
  const W = c.width;
  const when = relDay(alarm.day, today, L).toUpperCase();
  // The big font when it fits on one line, else the title font over up to two.
  let font = F.big;
  let what = wrap(c, alarm.what || item.title, font, W - 72, 2);
  if (what.length > 1) {
    font = F.title;
    what = wrap(c, alarm.what || item.title, font, W - 72, 2);
  }
  const step = font === F.big ? 52 : 42;
  const detail = wrap(c, item.text, F.body, W - 72, 4);
  const h = 30 + 46 + what.length * step + detail.length * 32 + 24;
  c.rect(16, y, W - 32, h, BLACK);
  c.text(`! ${when} !`, W / 2, y + 62, F.big, { align: "center", color: WHITE });
  let by = y + 62 + step + 2;
  for (const l of what) {
    c.text(l, W / 2, by, font, { align: "center", color: WHITE });
    by += step;
  }
  by += 4;
  for (const l of detail) {
    c.text(l, 36, by, F.body, { color: WHITE });
    by += 32;
  }
  return y + h + 16;
}

// One news item: label and day on top, bold title, up to `lines` lines of text.
function itemBlock(c, it, today, y, lines, bottom, L) {
  const W = c.width;
  const title = wrap(c, it.title, F.bodyBold, W - 48, 1);
  let text = wrap(c, it.text, F.body, W - 48, lines);
  const height = () => 26 + 32 + text.length * 30 + 12;
  // Shorten to fit, but never below two lines of text.
  while (y + height() > bottom && text.length > 2) text = wrap(c, it.text, F.body, W - 48, text.length - 1);
  const h = height();
  if (y + h > bottom) return null;
  const isAlarm = it.kind === "alarm";
  const label = L.label[it.kind] ?? L.label.info;
  const lw = c.textWidth(label, F.small) + 16;
  if (isAlarm) c.rect(24, y, lw, 26, BLACK);
  else if (c.gray) c.rect(24, y, lw, 26, GRAY);
  else c.frame(24, y, lw, 26, 1);
  c.text(label, 32, y + 20, F.small, { color: isAlarm ? WHITE : BLACK });
  const when = it.day ? relDay(it.day, today, L) : it.from ? fold(it.from) : "";
  if (when) c.text(when, W - 24, y + 20, F.small, { align: "right" });
  let by = y + 26 + 29;
  c.text(title[0] ?? "", 24, by, F.bodyBold);
  by += 31;
  for (const l of text) {
    c.text(l, 24, by, F.body);
    by += 30;
  }
  c.rect(24, y + h - 6, W - 48, 1, c.gray ? DARK_GRAY : BLACK);
  return y + h + 6;
}

export default {
  title: "School",
  // Query: kid=<id> (as pushed by the job), key=<read key>, depth,
  // vandaag=YYYY-MM-DD to pretend it is another day, lang=en.
  async render(c, params, env) {
    const lang = langOf(params);
    const L = T[lang];
    if (!env.SCHOOL_READ_KEY || params.get("key") !== env.SCHOOL_READ_KEY) throw new HttpError(403, "forbidden");
    const data = await env.SCHOOL.get(KEY, "json");
    if (!data) throw new Error("no school data pushed yet");
    const kidId = (params.get("kid") || "").toLowerCase();
    const kid = data.kids[kidId];
    if (!kid) throw new HttpError(404, "unknown kid");

    const fake = params.get("vandaag");
    const now = fake ? Date.parse(`${fake}T12:00:00Z`) : Date.now();
    const today = dayNr(fake || amsDate.format(now));
    const W = c.width;
    const H = c.height;

    c.text("School", 24, 50, F.title);
    c.text(L.forKid(fold(kid.name)), W - 24, 48, F.bodyBold, { align: "right" });
    c.text(dateLong(now, lang), 24, 86, F.body);
    c.text(`${L.at} ${clock(Date.now())}`, W - 24, 86, F.small, { align: "right" });
    c.rect(24, 102, W - 48, 2);

    const { alarms, list } = pick(kid.items, today);
    let y = 118;

    // Today and tomorrow get the black block; later alarms sit at the top of the list.
    const urgent = alarms.filter((it) => dayNr(it.alarm.day) - today <= 1);
    for (const it of urgent.slice(0, 2)) y = alarmBlock(c, it.alarm, it, today, y, L);
    const later = alarms.filter((it) => !urgent.includes(it)).map((it) => ({ ...it, kind: "alarm", day: it.alarm.day }));
    const queue = [...urgent.slice(2), ...later, ...list];

    const bottom = H - 46;
    if (!queue.length && !urgent.length) {
      c.paragraph(L.empty, 24, y + 60, W - 48, F.body);
    }
    let shown = 0;
    for (const it of queue) {
      const next = itemBlock(c, it, today, y, 4, bottom, L);
      if (next === null) break;
      y = next;
      shown++;
    }
    const left = queue.length - shown;

    // Footer: how fresh the news is, and what did not fit.
    c.rect(24, H - 40, W - 48, 1);
    const age = (now - Date.parse(data.updated)) / 3600000;
    const pulled = L.pulled(clock(Date.parse(data.updated)));
    const footer = age > STALE_HOURS ? L.stale(relDay(amsDate.format(Date.parse(data.updated)), today, L)) : pulled;
    c.text(footer, 24, H - 12, age > STALE_HOURS ? F.bodyBold : F.small);
    if (left > 0 && age <= STALE_HOURS) c.text(L.more(left), W - 24, H - 12, F.small, { align: "right" });
    return c;
  },
};
