// When it rains during one day, as a sentence and a bar chart of hours:
//
//   Regen van 9 tot 11 uur en vanaf 13 uur.
//   ┌──────────────────────────────────────────┐
//   │      ▃▅          ▂     ▅▇█▅▃             │
//   └┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘
//    7     9     11    13    15    17    19    21
//
// Bar height is how heavy (hourly amounts from Open-Meteo, square-root
// scale); darkness is how sure the ensemble is: black likely, dark gray
// probably, light gray maybe. Today starts at the current hour and draws its
// first two hours from the 5-minute radar nowcast as thin black bars.
import { BLACK, DARK_GRAY, GRAY } from "./canvas.js";
import * as F from "./fonts.js";
import { clock } from "./time.js";

export const WET_MM = 0.1; // an hour (or nowcast slot) below this is dry
const MAYBE = 35; // a window whose hours all stay below this is "maybe"
const DAY_FROM = 7;
const DAY_TO = 21;
const HOUR = 3600000;

const T = {
  nl: {
    dry: ["Vandaag blijft het droog.", "Het blijft de hele dag droog."],
    how: { light: "Een beetje regen", rain: "Regen", heavy: "Harde regen" },
    maybe: "Misschien regen",
    span: (a, b, first, last) => {
      const to = b % 24 === 0 ? "middernacht" : `${b} uur`;
      if (first) return `tot ${to}`;
      if (last) return `vanaf ${a} uur`;
      return a === b - 1 ? `om ${a} uur` : `van ${a} tot ${to}`;
    },
    and: "en",
    showers: "daarna soms een bui",
    now: "nu",
  },
  en: {
    dry: ["No rain today.", "Dry all day."],
    how: { light: "A little rain", rain: "Rain", heavy: "Heavy rain" },
    maybe: "Maybe rain",
    span: (a, b, first, last) => {
      const to = b % 24 === 0 ? "midnight" : `${b}:00`;
      if (first) return `until ${to}`;
      if (last) return `from ${a}:00`;
      return a === b - 1 ? `at ${a}:00` : `${a}:00–${to}`;
    },
    and: "and",
    showers: "then now and then a shower",
    now: "now",
  },
};

// The hours to show: a day view from 7 to 21, or for today from the current
// hour to 21 (later in the evening: up to six hours, never past midnight).
// Before 7 today is the plain day view: the night hours are not worth a bar.
export function hourRange(offset, nowHour) {
  if (offset > 0) return { from: DAY_FROM, to: DAY_TO };
  const from = Math.min(23, Math.max(DAY_FROM, nowHour));
  return { from, to: Math.max(DAY_TO, Math.min(from + 6, 24)) };
}

// Per hour of the range: { hour, mm, p } from the Open-Meteo hourly arrays
// (local time strings), with hours past midnight taken from the next day.
export function hoursOf(hourly, dayIso, range) {
  if (!hourly?.time) return null;
  const start = hourly.time.indexOf(`${dayIso}T00:00`);
  if (start < 0) return null;
  const out = [];
  for (let h = range.from; h < range.to; h++) {
    const i = start + h;
    if (i >= hourly.time.length) return null;
    out.push({ hour: h, mm: hourly.precipitation[i] ?? 0, p: hourly.precipitation_probability?.[i] ?? 100 });
  }
  return out;
}

// Wet stretches as [startIndex, endIndex) over the hours, a single dry hour
// between two wet ones bridged so a passing gap does not split the sentence.
function windows(hours) {
  const wet = hours.map((h) => h.mm >= WET_MM);
  const out = [];
  for (let i = 0; i < wet.length; i++) {
    if (!wet[i]) continue;
    const last = out[out.length - 1];
    if (last && i - last[1] <= 1) last[1] = i + 1;
    else out.push([i, i + 1]);
  }
  return out;
}

// One sentence; null when the hours are dry.
export function rainWhen(hours, lang) {
  const L = T[lang];
  const ws = windows(hours);
  if (!ws.length) return null;
  const inWs = hours.filter((_, i) => ws.some(([a, b]) => i >= a && i < b));
  // The typical wet hour, not the worst one: one heavy hour in a drizzly
  // afternoon is still "a little rain"; the bars show the burst.
  const mean = inWs.reduce((sum, h) => sum + h.mm, 0) / inWs.length;
  const maybe = inWs.every((h) => h.p < MAYBE);
  const how = maybe ? L.maybe : L.how[mean < 0.5 ? "light" : mean < 2.5 ? "rain" : "heavy"];
  const spans = ws.slice(0, 2).map(([a, b]) => {
    // Rain from the first hour on reads "until": raining now, or from morning.
    const first = a === 0;
    const last = b === hours.length;
    return L.span(hours[a].hour % 24, hours[b - 1].hour % 24 + 1, first, last && !first);
  });
  // More than two spells: the first one, then "showers" for the rest.
  if (ws.length > 2) return `${how} ${spans[0]}, ${L.showers}.`;
  return `${how} ${spans.join(` ${L.and} `)}.`;
}

export const dryText = (offset, lang) => T[lang].dry[offset === 0 ? 0 : 1];

// Bar height: the amount on a square-root scale, so a drizzle still shows.
const FULL_MM = 5;
// Bar darkness: how sure the ensemble is.
const shadeFor = (p) => (p >= 70 ? BLACK : p >= 40 ? DARK_GRAY : GRAY);

// Draws the day as framed bars over an hour axis; returns the y below it.
// Height is how heavy, darkness how certain. `nowcast` (today) is
// [{ time: ms, mm }] in 5-minute steps, drawn as thin black bars over the
// hours it covers. With `showNow`, `startMs` is the first column's time and a
// marker shows now.
export function rainChart(c, hours, x, y, w, lang, { nowcast = null, startMs = 0, showNow = false } = {}) {
  const L = T[lang];
  const colW = w / hours.length;
  const barH = 48;
  const top = y + 4;
  const base = top + barH;
  const height = (mm) => Math.max(4, Math.round((Math.sqrt(Math.min(mm, FULL_MM)) / Math.sqrt(FULL_MM)) * (barH - 4)));

  const covered = new Set();
  if (nowcast?.length && startMs) {
    for (const s of nowcast) {
      const col = Math.floor((s.time - startMs) / HOUR);
      if (col < 0 || col >= hours.length) continue;
      covered.add(col);
      if (s.mm < WET_MM) continue;
      const sx = x + ((s.time - startMs) / HOUR) * colW;
      const bh = height(s.mm);
      c.rect(sx, base - bh, Math.max(2, colW / 12 - 0.5), bh, BLACK);
    }
  }
  hours.forEach((h, i) => {
    if (covered.has(i) || h.mm < WET_MM) return;
    const bh = height(h.mm);
    c.rect(x + i * colW + 2, base - bh, colW - 4, bh, shadeFor(h.p));
  });
  c.frame(x, top, w, barH + 2, 2);

  // A tick every hour, a label every two. Today "nu" stands where now is.
  for (let i = 0; i <= hours.length; i++) {
    const tx = Math.min(x + i * colW, x + w - 2);
    c.rect(tx, base, 2, i % 2 ? 4 : 8);
    if (i % 2 || (i === 0 && showNow)) continue;
    const align = i === 0 ? "left" : i === hours.length ? "right" : "center";
    c.text(String((hours[0].hour + i) % 24), x + i * colW, base + 28, F.small, { align });
  }
  if (showNow) {
    const nx = Math.round(x + ((Date.now() - startMs) / HOUR) * colW);
    c.rect(nx - 1, top - 6, 3, barH + 14, BLACK);
    c.text(L.now, Math.max(x, nx - c.textWidth(L.now, F.small) / 2), base + 28, F.small);
  }
  return base + 34;
}

export const startOfHour = (ms) => Math.floor(ms / HOUR) * HOUR;
export const hourOf = (ms) => Number(clock(ms).slice(0, 2));
