// Weather for a child: simple Dutch, big icons, rain in the next two hours.
// Data: Buienradar (station measurements, 5-day forecast, sun times) and
// Buienalarm (5-minute rain nowcast).
import { BLACK, Canvas, GRAY } from "../canvas.js";
import * as F from "../fonts.js";
import { drawIcon, drop, iconFor } from "../icons.js";
import { clock, dateLong, weekdayShort } from "../time.js";

const BUIENRADAR = "https://data.buienradar.nl/2.0/feed/json";
const BUIENALARM = "https://imn-rust-lb.infoplaza.io/v4/nowcast/ba/timeseries";
const RAIN_MM = 0.1; // below this the nowcast counts as dry

async function getJson(url, headers = {}) {
  const res = await fetch(url, { headers: { Accept: "application/json", ...headers } });
  if (!res.ok) throw new Error(`${url} -> ${res.status}`);
  return res.json();
}

function nearestStation(stations, lat, lon) {
  let best = null;
  let bestD = Infinity;
  for (const s of stations) {
    if (s.temperature == null) continue;
    const d = (s.lat - lat) ** 2 + ((s.lon - lon) * Math.cos((lat * Math.PI) / 180)) ** 2;
    if (d < bestD) [best, bestD] = [s, d];
  }
  return best;
}

function feelSentence(t) {
  if (t < 0) return "Het vriest!";
  if (t < 8) return "Het is koud.";
  if (t < 14) return "Het is fris.";
  if (t < 20) return "Het is lekker weer.";
  if (t < 26) return "Het is warm.";
  return "Het is heet!";
}

function skySentence(kind, isNight) {
  return {
    sun: isNight ? "De lucht is helder." : "De zon schijnt.",
    partly: isNight ? "Er zijn een paar wolken." : "Zon en wolken.",
    cloud: "Er zijn veel wolken.",
    rain: "Het regent.",
    showers: "Soms valt er een bui.",
    thunder: "Er is onweer!",
    snow: "Het sneeuwt!",
    fog: "Het is mistig.",
  }[kind];
}

function rainWord(mm) {
  if (mm < 0.5) return "een beetje";
  if (mm < 3) return "";
  return "hard";
}

// One sentence about rain in the next two hours, from the 5-minute nowcast.
function rainSentence(slots) {
  const wet = slots.map((s) => s.mm >= RAIN_MM);
  const first = wet.indexOf(true);
  if (first === -1) return "De komende 2 uur blijft het droog.";
  const peak = Math.max(...slots.map((s) => s.mm));
  const how = rainWord(peak);
  if (first === 0) {
    const dry = wet.indexOf(false);
    if (dry === -1) return `Het regent ${how} en dat blijft zo.`.replace("  ", " ");
    return `Het regent nu ${how}. Om ${clock(slots[dry].time)} is het weer droog.`.replace("  ", " ");
  }
  const minutes = Math.round((slots[first].time - Date.now()) / 60000 / 5) * 5;
  const when = minutes <= 5 ? "Zo meteen" : `Over ${minutes} minuten`;
  return `${when} gaat het ${how} regenen.`.replace("  ", " ");
}

function tips(temp, bft, rainSoon, kind) {
  const out = [];
  if (rainSoon) out.push("Neem je regenjas mee!");
  if (temp < 3) out.push("Doe een muts en wanten aan.");
  else if (temp < 12) out.push("Trek een warme jas aan.");
  else if (temp < 18 && !rainSoon) out.push("Een trui of jasje is genoeg.");
  if (bft >= 6) out.push("Het waait hard. Hou je pet vast!");
  if (temp >= 25 && kind === "sun") out.push("Smeer je in en drink veel water.");
  if (!out.length) out.push("Lekker weer om buiten te spelen!");
  return out.slice(0, 2);
}

function rainChart(c, slots, x, y, w, h) {
  const max = Math.max(4, ...slots.map((s) => s.mm));
  const step = w / slots.length;
  c.rect(x, y + h, w, 2);
  slots.forEach((s, i) => {
    if (s.mm < RAIN_MM) return;
    const bar = Math.max(4, (Math.sqrt(s.mm) / Math.sqrt(max)) * h);
    c.rect(x + i * step + 1, y + h - bar, step - 2, bar, GRAY);
  });
  slots.forEach((s, i) => {
    if (i % 6) return;
    const label = i === 0 ? "nu" : clock(s.time);
    c.rect(x + i * step, y + h, 2, 8);
    c.text(label, x + i * step, y + h + 30, F.small, { align: i === 0 ? "left" : "center" });
  });
}

function wrap(c, text, maxWidth, font) {
  const lines = [];
  let line = "";
  for (const word of text.split(" ")) {
    const next = line ? `${line} ${word}` : word;
    if (line && c.textWidth(next, font) > maxWidth) {
      lines.push(line);
      line = word;
    } else line = next;
  }
  return line ? [...lines, line] : lines;
}

// ?demo=rain swaps in a made-up shower so the chart can be checked on dry days.
function demoRain() {
  const start = Math.floor(Date.now() / 300000) * 300000;
  const mm = [0, 0, 0, 0.3, 0.8, 1.5, 3, 4.5, 3.2, 2, 1, 0.4, 0.2, 0, 0, 0, 0, 0.2, 0.5, 0.3, 0, 0, 0, 0];
  return mm.map((v, i) => ({ time: start + i * 300000, mm: v }));
}

export default {
  title: "Weer",
  // Query: lat, lon, place (label at the top), demo=rain (fake shower).
  async render(params) {
    const lat = Number(params.get("lat") ?? 52.37);
    const lon = Number(params.get("lon") ?? 4.9);
    const place = params.get("place") || "";

    const [br, ba] = await Promise.all([
      getJson(BUIENRADAR),
      getJson(`${BUIENALARM}/${lat.toFixed(2)}/${lon.toFixed(2)}`, {
        Referer: "https://www.buienalarm.nl/",
        Origin: "https://www.buienalarm.nl",
      }).catch(() => null),
    ]);

    const station = nearestStation(br.actual.stationmeasurements, lat, lon);
    const temp = Math.round(station.temperature);
    const sunrise = br.actual.sunrise.slice(11, 16);
    const sunset = br.actual.sunset.slice(11, 16);
    const nowClock = clock(Date.now());
    const isNight = nowClock < sunrise || nowClock > sunset;
    const kind = iconFor(station.weatherdescription);
    const slots = (ba?.data ?? [])
      .map((d) => ({ time: d.timestamp * 1000, mm: d.precipitationrate }))
      .filter((s) => s.time > Date.now() - 5 * 60000)
      .slice(0, 24);
    if (params.get("demo") === "rain") slots.splice(0, slots.length, ...demoRain());
    const rainSoon = slots.slice(0, 12).some((s) => s.mm >= RAIN_MM);

    const c = new Canvas();
    const W = c.width;

    c.text(place ? `Het weer in ${place}` : "Het weer", 24, 50, F.title);
    c.text(dateLong(Date.now()), 24, 86, F.body);
    c.text(`om ${nowClock}`, W - 24, 86, F.small, { align: "right" });

    drawIcon(c, kind === "sun" && isNight ? "partly" : kind, 118, 196, 180);
    c.text(`${temp}°`, W - 20, 266, F.huge, { align: "right" });
    c.text(feelSentence(temp), 24, 344, F.big);
    c.text(skySentence(kind, isNight), 24, 384, F.body);

    c.rect(24, 404, W - 48, 2);
    c.text("Regen", 24, 440, F.bodyBold);
    c.paragraph(slots.length ? rainSentence(slots) : "Er is nu geen regenkaart.", 24, 474, W - 48, F.body, { lineHeight: 32 });
    if (slots.length) rainChart(c, slots, 24, 518, W - 48, 44);

    const tipLines = [];
    for (const tip of tips(temp, station.windspeedBft ?? 0, rainSoon, kind)) {
      const lines = wrap(c, tip, W - 72, F.bodyBold);
      if (tipLines.length + lines.length > 2) break;
      tipLines.push(...lines);
    }
    c.frame(20, 606, W - 40, 20 + tipLines.length * 34, 3);
    tipLines.forEach((t, i) => c.text(t, W / 2, 640 + i * 34, F.bodyBold, { align: "center" }));

    const days = br.forecast.fivedayforecast.slice(0, 4);
    const colW = (W - 40) / days.length;
    days.forEach((d, i) => {
      const cx = 20 + colW * i + colW / 2;
      c.text(weekdayShort(d.day), cx, 718, F.bodyBold, { align: "center" });
      drawIcon(c, iconFor(d.weatherdescription), cx - 26, 748, 46);
      c.text(`${d.maxtemperatureMax}°`, cx + 2, 758, F.bodyBold);
      drop(c, cx - 26, 786, 5);
      c.text(`${d.rainChance}%`, cx - 16, 792, F.small);
    });
    return c;
  },
};
