// When it rains during one day, as a sentence and a small timeline:
//
//   Regen van 8 tot 14 uur en om 17 uur.
//   ▁▃▇▇▅▂       ▃
//   ┴──┴──┴──┴──┴──┴──┴──┴
//   7  9  11 13 15 17 19 21
//
// Bars are the hourly amount (Open-Meteo, square-root scale so a drizzle still
// shows); dark when the ensemble says rain is likely, light when it is a
// maybe. Today starts at the current hour and draws the next two hours from
// the 5-minute radar nowcast instead, so "in 20 minutes" is visible too.
import { BLACK, DARK_GRAY, GRAY } from "./canvas.js";
import * as F from "./fonts.js";
import { clock } from "./time.js";

export const WET_MM = 0.1; // an hour (or nowcast slot) below this is dry
const LIKELY = 50; // % from the ensemble: at or above draws dark
const MAYBE = 35; // a window whose hours all stay below this is "maybe"
const FULL_MM = 5; // mm/h that fills the bar height
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
    now: "now",
  },
};

// The hours to show: a day view from 7 to 21, or for today from the current
// hour to 21 (later in the evening: up to six hours, never past midnight).
export function hourRange(offset, nowHour) {
  if (offset > 0) return { from: DAY_FROM, to: DAY_TO };
  const from = Math.max(0, Math.min(23, nowHour));
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
export function rainWhen(hours, lang, { fromNow = false } = {}) {
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
    const first = a === 0 && !fromNow;
    const last = b === hours.length;
    return L.span(hours[a].hour % 24, hours[b - 1].hour % 24 + 1, first, last && !first);
  });
  return `${how} ${spans.join(` ${L.and} `)}${ws.length > 2 ? " ..." : ""}.`;
}

export const dryText = (offset, lang) => T[lang].dry[offset === 0 ? 0 : 1];

// Draws the timeline at (x, y), width w; returns the y below it.
// `nowcast` (today only) is [{ time: ms, mm }] in 5-minute steps; `startMs` is
// the absolute time of the first hour column.
export function rainChart(c, hours, x, y, w, lang, { nowcast = null, startMs = 0 } = {}) {
  const L = T[lang];
  const barH = 34;
  const colW = w / hours.length;
  const base = y + barH;
  const height = (mm) => Math.max(4, Math.round((Math.sqrt(Math.min(mm, FULL_MM)) / Math.sqrt(FULL_MM)) * barH));
  const shade = (p) => (p >= LIKELY ? (c.gray ? DARK_GRAY : BLACK) : GRAY);

  // Hours the nowcast covers get its 5-minute bars instead of the hourly one.
  const covered = new Set();
  if (nowcast?.length) {
    const slotW = colW / 12;
    for (const s of nowcast) {
      const col = Math.floor((s.time - startMs) / HOUR);
      if (col < 0 || col >= hours.length) continue;
      covered.add(col);
      if (s.mm < WET_MM) continue;
      const sx = x + ((s.time - startMs) / HOUR) * colW;
      const bh = height(s.mm);
      c.rect(sx, base - bh, Math.max(2, slotW - 0.5), bh, c.gray ? DARK_GRAY : BLACK);
    }
  }
  hours.forEach((h, i) => {
    if (covered.has(i) || h.mm < WET_MM) return;
    const bh = height(h.mm);
    c.rect(x + i * colW + 2, base - bh, colW - 4, bh, shade(h.p));
  });

  c.rect(x, base, w, 2);
  // A tick and label every two hours from the start. Today the first hour is
  // already under way: "nu" marks where now is instead of its hour.
  for (let i = 0; i <= hours.length; i += 2) {
    const tx = x + i * colW;
    c.rect(Math.min(tx, x + w - 2), base, 2, 6);
    if (i === 0 && startMs) continue;
    const align = i === 0 ? "left" : i === hours.length ? "right" : "center";
    c.text(String((hours[0].hour + i) % 24), tx, base + 26, F.small, { align });
  }
  if (startMs) {
    const nx = Math.round(x + ((Date.now() - startMs) / HOUR) * colW);
    c.rect(nx, base - barH - 2, 2, barH + 10, BLACK);
    c.text(L.now, Math.max(x, nx - c.textWidth(L.now, F.small) / 2), base + 26, F.small);
  }
  return base + 32;
}

export const startOfHour = (ms) => Math.floor(ms / HOUR) * HOUR;
export const hourOf = (ms) => Number(clock(ms).slice(0, 2));
