// Dutch date and time labels in the Netherlands' time zone.
const TZ = "Europe/Amsterdam";
const clockFmt = new Intl.DateTimeFormat("nl-NL", { timeZone: TZ, hour: "2-digit", minute: "2-digit", hour12: false });
const dateFmt = new Intl.DateTimeFormat("nl-NL", { timeZone: TZ, weekday: "long", day: "numeric", month: "long" });
// Buienradar day stamps are local dates without a zone; read them as UTC.
const dayFmt = new Intl.DateTimeFormat("nl-NL", { timeZone: "UTC", weekday: "short" });

export const clock = (ms) => clockFmt.format(ms);
export const dateLong = (ms) => dateFmt.format(ms);
export const weekdayShort = (localDate) => dayFmt.format(new Date(`${localDate.slice(0, 10)}T12:00:00Z`)).replace(".", "");
