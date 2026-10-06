// Countdown to the next Dutch school holiday, in simple Dutch for a child.
// Data: Rijksoverheid open data (schoolholidays), keyless, all school years at
// once. Regions noord/midden/zuid; Kerst- and Meivakantie are national and
// listed as "heel Nederland".
import { fetchJson } from "../fetch.js";
import { GRAY } from "../canvas.js";
import * as F from "../fonts.js";
import { drawIcon } from "../icons.js";
import { clock, dateLong } from "../time.js";

const SOURCE = "https://opendata.rijksoverheid.nl/v1/infotypes/schoolholidays?output=json";
const REGIONS = ["noord", "midden", "zuid"];
const DAY = 86400000;
const STRIP_MAX = 21; // nights that still fit in the three-row sleep strip

const amsDate = new Intl.DateTimeFormat("en-CA", {
  timeZone: "Europe/Amsterdam",
  year: "numeric",
  month: "2-digit",
  day: "2-digit",
});

// Calendar days, so a DST change never shifts the countdown by one.
const dayNr = (iso) => {
  const [y, m, d] = iso.slice(0, 10).split("-").map(Number);
  return Math.floor(Date.UTC(y, m - 1, d) / DAY);
};
const dayLabel = (iso) => dateLong(dayNr(iso) * DAY + 12 * 3600000);
const clean = (s) => s.replace(/\s+/g, " ").trim();

// Every holiday of every school year that touches the chosen region, by date.
// The stamps carry a bogus time (00:00Z / 22:59Z), so only the date is used.
function holidays(json, region) {
  const out = [];
  for (const entry of json) {
    for (const year of entry.content ?? []) {
      for (const vacation of year.vacations ?? []) {
        const name = clean(vacation.type);
        for (const r of vacation.regions ?? []) {
          const where = clean(r.region).toLowerCase();
          if (where !== region && !where.startsWith("heel")) continue;
          out.push({ name, start: dayNr(r.startdate), end: dayNr(r.enddate), startIso: r.startdate, endIso: r.enddate });
          break;
        }
      }
    }
  }
  return out.sort((a, b) => a.start - b.start);
}

// A weather icon that matches the season of the holiday, as a hint of what it
// will be like outside.
function iconKind(name) {
  const n = name.toLowerCase();
  if (n.startsWith("kerst")) return "snow";
  if (n.startsWith("herfst")) return "cloud";
  if (n.startsWith("voorjaar")) return "partly";
  return "sun";
}

// Three rows of seven boxes: one box per night still to sleep.
function sleepStrip(c, count, x, y) {
  const box = 28;
  const gap = 8;
  const perRow = 7;
  for (let i = 0; i < count; i++) {
    const bx = x + (i % perRow) * (box + gap);
    const by = y + Math.floor(i / perRow) * (box + gap);
    c.rect(bx, by, box, box, GRAY);
    c.frame(bx, by, box, box, 2);
  }
}

export default {
  title: "Vakantie",
  // Query: regio=noord|midden|zuid (default midden), vandaag=YYYY-MM-DD to
  // pretend it is another day (for checking the layout), depth.
  async render(c, params) {
    const asked = (params.get("regio") || params.get("region") || "midden").toLowerCase();
    const region = REGIONS.includes(asked) ? asked : "midden";
    const fake = params.get("vandaag");

    const all = holidays(await fetchJson(SOURCE, { ttl: 86400 }), region);

    const today = dayNr(fake || amsDate.format(Date.now()));
    const upcoming = all.filter((h) => h.end >= today);
    if (!upcoming.length) throw new Error("no school holidays left in the data");

    const next = upcoming[0];
    const running = next.start <= today; // we are inside this holiday
    const nights = running ? next.end - today + 1 : next.start - today;
    const length = next.end - next.start + 1;

    const W = c.width;

    c.text("Vakantie", 24, 50, F.title);
    c.text(`regio ${region}`, W - 24, 48, F.small, { align: "right" });
    c.text(dayLabel(fake || amsDate.format(Date.now())), 24, 90, F.body);
    c.text(`om ${clock(Date.now())}`, W - 24, 90, F.small, { align: "right" });
    c.rect(24, 108, W - 48, 2);

    // Hero: the number, what it counts, and which holiday it is.
    c.frame(20, 124, W - 40, 246, 3);
    c.text(String(nights), W / 2, 290, F.huge, { align: "center" });
    const unit = nights === 1 ? "dag" : "dagen";
    c.text(running ? `${unit} nog vrij` : `${unit} tot de`, W / 2, 326, F.body, { align: "center" });
    c.text(next.name, W / 2, 362, F.title, { align: "center" });

    c.text(`van ${dayLabel(next.startIso)}`, 24, 414, F.body);
    c.text(`t/m ${dayLabel(next.endIso)}`, 24, 450, F.body);
    c.text(running ? "Fijne vakantie!" : `Dat zijn ${length} dagen vrij!`, 24, 494, F.bodyBold);
    c.rect(24, 514, W - 48, 2);

    // nights is at least 1: either the holiday is still ahead or it is running.
    if (nights <= STRIP_MAX) {
      const label = running ? "Zoveel dagen nog vakantie:" : nights === 1 ? "Nog 1x slapen!" : `Nog ${nights}x slapen:`;
      c.text(label, 24, 548, F.small);
      sleepStrip(c, nights, 24, 560);
    } else {
      c.text(`Dat is ongeveer ${Math.round(nights / 7)} weken.`, 24, 556, F.bodyBold);
      drawIcon(c, iconKind(next.name), W / 2, 616, 90);
    }

    c.rect(24, 676, W - 48, 2);
    c.text("Daarna", 24, 710, F.bodyBold);
    upcoming.slice(1, 3).forEach((h, i) => {
      const y = 750 + i * 36;
      c.text(h.name, 24, y, F.body);
      c.text(`over ${h.start - today} dagen`, W - 24, y, F.small, { align: "right" });
    });
    return c;
  },
};
