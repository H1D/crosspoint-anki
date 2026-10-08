// When it rains during one day, as a sentence and a row of hours with drops:
//
//   Regen van 9 tot 11 uur en vanaf 13 uur.
//         💧💧            💧    💧💧💧  💧
//   ┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴
//   7     9     11    13    15    17    19    21
//
// One drop is a little rain, two is rain, three is heavy rain (hourly
// amounts from Open-Meteo); black when the ensemble says rain is likely, gray
// when it is a maybe. Today starts at the current hour, and its first two
// hours carry the 5-minute radar nowcast instead of the model.
import { BLACK, DARK_GRAY, GRAY } from "./canvas.js";
import * as F from "./fonts.js";
import { clock } from "./time.js";

export const WET_MM = 0.1; // an hour (or nowcast slot) below this is dry
const LIKELY = 50; // % from the ensemble: at or above draws dark
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
  return `${how} ${spans.join(` ${L.and} `)}${ws.length > 2 ? " ..." : ""}.`;
}

export const dryText = (offset, lang) => T[lang].dry[offset === 0 ? 0 : 1];

// A raindrop with its tip up, filled with `v`.
function raindrop(c, cx, cy, r, v) {
  c.circle(cx, cy, r, v);
  const tip = r * 1.7;
  for (let y = 0; y < tip; y++) {
    const half = r * (1 - y / tip);
    c.rect(cx - half, cy - y, 2 * half, 1, v);
  }
}

// Drops per hour: 1 a little rain, 2 rain, 3 heavy rain.
const dropsFor = (mm) => (mm < WET_MM ? 0 : mm < 0.5 ? 1 : mm < 2.5 ? 2 : 3);

// Draws the day as a row of hours with raindrops stacked in the rainy ones
// (black: rain is likely, gray: a maybe) over an hour axis; returns the y
// below it. Hours the radar nowcast covers already carry its amount. With
// `showNow`, `startMs` is the first column's time and a marker shows now.
export function rainChart(c, hours, x, y, w, lang, { startMs = 0, showNow = false } = {}) {
  const L = T[lang];
  const colW = w / hours.length;
  const r = Math.max(4, Math.min(7, Math.floor(colW / 4.4)));
  const step = Math.round(r * 2.9); // one drop's height plus a gap
  const top = y + 4;
  const base = top + 3 * step + 2;

  hours.forEach((h, i) => {
    const n = dropsFor(h.mm);
    const v = h.p >= LIKELY ? BLACK : c.gray ? GRAY : DARK_GRAY;
    const cx = x + i * colW + colW / 2;
    for (let k = 0; k < n; k++) raindrop(c, cx, base - r - 3 - k * step, r, v);
  });

  c.rect(x, base, w, 2);
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
    c.rect(nx, top - 4, 2, base - top + 12, BLACK);
    c.text(L.now, Math.max(x, nx - c.textWidth(L.now, F.small) / 2), base + 28, F.small);
  }
  return base + 34;
}

export const startOfHour = (ms) => Math.floor(ms / HOUR) * HOUR;
export const hourOf = (ms) => Number(clock(ms).slice(0, 2));
