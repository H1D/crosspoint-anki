// Weather for a child: simple Dutch (or English with lang=en), big icons,
// rain in the next two hours.
// Data: Buienradar (station measurements, 5-day forecast, sun times) and
// Buienalarm (5-minute rain nowcast).
import { DARK_GRAY, GRAY } from "../canvas.js";
import * as F from "../fonts.js";
import { drawIcon, drop, iconFor } from "../icons.js";
import { fetchJson } from "../fetch.js";
import { clock, dateLong, langOf, weekdayShort } from "../time.js";

const BUIENRADAR = "https://data.buienradar.nl/2.0/feed/json";
const BUIENALARM = "https://imn-rust-lb.infoplaza.io/v4/nowcast/ba/timeseries";
const RAIN_MM = 0.1; // below this the nowcast counts as dry

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

const T = {
  nl: {
    feel: ["Het vriest!", "Het is koud.", "Het is fris.", "Het is lekker weer.", "Het is warm.", "Het is heet!"],
    sky: {
      sun: ["De zon schijnt.", "De lucht is helder."],
      partly: ["Zon en wolken.", "Er zijn een paar wolken."],
      cloud: ["Er zijn veel wolken."],
      rain: ["Het regent."],
      showers: ["Soms valt er een bui."],
      thunder: ["Er is onweer!"],
      snow: ["Het sneeuwt!"],
      fog: ["Het is mistig."],
    },
    how: ["een beetje", "", "hard"],
    dry2h: "De komende 2 uur blijft het droog.",
    rainingStays: (how) => `Het regent ${how} en dat blijft zo.`,
    rainingUntil: (how, at) => `Het regent nu ${how}. Om ${at} is het weer droog.`,
    rainIn: (how, minutes) => `${minutes <= 5 ? "Zo meteen" : `Over ${minutes} minuten`} gaat het ${how} regenen.`,
    tips: {
      raincoat: "Neem je regenjas mee!",
      clothes: [
        "Muts, sjaal en wanten aan!",
        "Trek je winterjas aan.",
        "Trek een jas aan.",
        "Een trui of vest is genoeg.",
        "Een T-shirt is genoeg.",
        "Korte broek aan en drink veel water!",
      ],
      wind: "Het waait hard!",
    },
    chance: ["droog", "misschien", "regen"],
    title: (place) => (place ? `Het weer in ${place}` : "Het weer"),
    at: "om",
    rain: "Regen",
    now: "nu",
    noMap: "Er is nu geen regenkaart.",
  },
  en: {
    feel: ["It's freezing!", "It's cold.", "It's chilly.", "Nice weather.", "It's warm.", "It's hot!"],
    sky: {
      sun: ["The sun is shining.", "The sky is clear."],
      partly: ["Sun and clouds.", "A few clouds."],
      cloud: ["Lots of clouds."],
      rain: ["It's raining."],
      showers: ["Now and then a shower."],
      thunder: ["Thunderstorm!"],
      snow: ["It's snowing!"],
      fog: ["It's foggy."],
    },
    how: ["a little", "", "hard"],
    dry2h: "No rain for the next 2 hours.",
    rainingStays: (how) => `It's raining ${how} and it will keep raining.`,
    rainingUntil: (how, at) => `It's raining ${how} now. Dry again at ${at}.`,
    rainIn: (how, minutes) => `${minutes <= 5 ? "Any minute now" : `In ${minutes} minutes`} it will rain ${how}.`,
    tips: {
      raincoat: "Take your raincoat!",
      clothes: [
        "Hat, scarf and gloves on!",
        "Wear your winter coat.",
        "Wear a jacket.",
        "A sweater is enough.",
        "A T-shirt is enough.",
        "Shorts on, and drink lots of water!",
      ],
      wind: "It's very windy!",
    },
    chance: ["dry", "maybe", "rain"],
    title: (place) => (place ? `Weather in ${place}` : "Weather"),
    at: "at",
    rain: "Rain",
    now: "now",
    noMap: "No rain map right now.",
  },
};

const tidy = (s) => s.replace(/\s+/g, " ").replace(" .", ".");

function feelSentence(t, L) {
  const steps = [0, 8, 14, 20, 26];
  const i = steps.findIndex((limit) => t < limit);
  return L.feel[i === -1 ? steps.length : i];
}

function skySentence(kind, isNight, L) {
  const options = L.sky[kind];
  return isNight && options[1] ? options[1] : options[0];
}

function rainWord(mm, L) {
  if (mm < 0.5) return L.how[0];
  if (mm < 3) return L.how[1];
  return L.how[2];
}

// One sentence about rain in the next two hours, from the 5-minute nowcast.
function rainSentence(slots, L) {
  const wet = slots.map((s) => s.mm >= RAIN_MM);
  const first = wet.indexOf(true);
  if (first === -1) return L.dry2h;
  const peak = Math.max(...slots.map((s) => s.mm));
  const how = rainWord(peak, L);
  if (first === 0) {
    const dry = wet.indexOf(false);
    if (dry === -1) return tidy(L.rainingStays(how));
    return tidy(L.rainingUntil(how, clock(slots[dry].time)));
  }
  const minutes = Math.round((slots[first].time - Date.now()) / 60000 / 5) * 5;
  return tidy(L.rainIn(how, minutes));
}

function tips(temp, bft, rainSoon, L) {
  const out = [];
  if (rainSoon) out.push(L.tips.raincoat);
  const steps = [3, 10, 15, 20, 25];
  const i = steps.findIndex((limit) => temp < limit);
  out.push(L.tips.clothes[i === -1 ? steps.length : i]);
  if (bft >= 6) out.push(L.tips.wind);
  return out;
}

function rainChanceIndex(pct) {
  if (pct < 30) return 0;
  if (pct < 60) return 1;
  return 2;
}

function rainChart(c, slots, x, y, w, h, L) {
  const max = Math.max(4, ...slots.map((s) => s.mm));
  const step = w / slots.length;
  c.rect(x, y + h, w, 2);
  slots.forEach((s, i) => {
    if (s.mm < RAIN_MM) return;
    const bar = Math.max(4, (Math.sqrt(s.mm) / Math.sqrt(max)) * h);
    c.rect(x + i * step + 1, y + h - bar, step - 2, bar, c.gray ? GRAY : DARK_GRAY);
  });
  slots.forEach((s, i) => {
    if (i % 6) return;
    const label = i === 0 ? L.now : clock(s.time);
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
  // Query: lat, lon, place (label at the top), demo=rain (fake shower), lang=en.
  async render(c, params) {
    const lang = langOf(params);
    const L = T[lang];
    const lat = Number(params.get("lat") ?? 52.37);
    const lon = Number(params.get("lon") ?? 4.9);
    const place = params.get("place") || "";

    const [br, ba] = await Promise.all([
      fetchJson(BUIENRADAR),
      fetchJson(`${BUIENALARM}/${lat.toFixed(2)}/${lon.toFixed(2)}`, {
        headers: { Referer: "https://www.buienalarm.nl/", Origin: "https://www.buienalarm.nl" },
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

    const W = c.width;

    c.text(L.title(place), 24, 50, F.title);
    c.text(dateLong(Date.now(), lang), 24, 86, F.body);
    c.text(`${L.at} ${nowClock}`, W - 24, 86, F.small, { align: "right" });

    drawIcon(c, kind === "sun" && isNight ? "partly" : kind, 118, 196, 180);
    c.text(`${temp}°`, W - 20, 266, F.huge, { align: "right" });
    c.text(feelSentence(temp, L), 24, 344, F.big);
    c.text(skySentence(kind, isNight, L), 24, 384, F.body);

    c.rect(24, 404, W - 48, 2);
    c.text(L.rain, 24, 440, F.bodyBold);
    c.paragraph(slots.length ? rainSentence(slots, L) : L.noMap, 24, 474, W - 48, F.body, { lineHeight: 32 });
    // A chart with no bars reads as broken; on dry days the sentence says it all.
    if (slots.some((s) => s.mm >= RAIN_MM)) rainChart(c, slots, 24, 518, W - 48, 44, L);

    const tipLines = [];
    for (const tip of tips(temp, station.windspeedBft ?? 0, rainSoon, L)) {
      const lines = wrap(c, tip, W - 72, F.bodyBold);
      if (tipLines.length + lines.length > 2) break;
      tipLines.push(...lines);
    }
    if (c.gray) c.rect(20, 606, W - 40, 20 + tipLines.length * 34, GRAY);
    c.frame(20, 606, W - 40, 20 + tipLines.length * 34, 3);
    tipLines.forEach((t, i) => c.text(t, W / 2, 640 + i * 34, F.bodyBold, { align: "center" }));

    const days = br.forecast.fivedayforecast.slice(0, 4);
    const colW = (W - 40) / days.length;
    days.forEach((d, i) => {
      const cx = 20 + colW * i + colW / 2;
      c.text(weekdayShort(d.day, lang), cx, 718, F.bodyBold, { align: "center" });
      drawIcon(c, iconFor(d.weatherdescription), cx - 26, 748, 46);
      c.text(`${d.maxtemperatureMax}°`, cx + 2, 758, F.bodyBold);
      const chance = rainChanceIndex(d.rainChance);
      const word = L.chance[chance];
      if (chance > 0) drop(c, cx - c.textWidth(word, F.small) / 2 - 10, 786, 5);
      c.text(word, cx + (chance > 0 ? 6 : 0), 792, F.small, { align: "center" });
    });
    return c;
  },
};
