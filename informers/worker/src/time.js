// Date and time labels in the Netherlands' time zone, in Dutch or English.
const TZ = "Europe/Amsterdam";
const LOCALE = { nl: "nl-NL", en: "en-GB" };
const clockFmt = new Intl.DateTimeFormat("nl-NL", { timeZone: TZ, hour: "2-digit", minute: "2-digit", hour12: false });
const dateFmt = {};
const dayFmt = {};
for (const [lang, locale] of Object.entries(LOCALE)) {
  dateFmt[lang] = new Intl.DateTimeFormat(locale, { timeZone: TZ, weekday: "long", day: "numeric", month: "long" });
  // Buienradar day stamps are local dates without a zone; read them as UTC.
  dayFmt[lang] = new Intl.DateTimeFormat(locale, { timeZone: "UTC", weekday: "short" });
}

// Informers take ?lang=en; everything else is Dutch.
export const langOf = (params) => (params.get("lang") === "en" ? "en" : "nl");
export const clock = (ms) => clockFmt.format(ms);
export const dateLong = (ms, lang = "nl") => dateFmt[lang].format(ms).replace(",", "");
export const weekdayShort = (localDate, lang = "nl") =>
  dayFmt[lang].format(new Date(`${localDate.slice(0, 10)}T12:00:00Z`)).replace(".", "");
