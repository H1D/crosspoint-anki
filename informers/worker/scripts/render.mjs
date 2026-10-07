// Renders an informer locally without Wrangler, fetching live data:
//   node scripts/render.mjs weather out.bmp "lat=52.37&lon=4.90&place=Amsterdam&depth=2"
// Informers that read KV (school, dag) take their data from JSON files and
// their secrets from the environment:
//   SCHOOL_JSON=data.json SCHOOL_READ_KEY=k node scripts/render.mjs school out.bmp "kid=a&key=k"
//   SCHOOL_JSON=data.json AGENDA_JSON=agenda.json SCHOOL_READ_KEY=k node scripts/render.mjs dag out.bmp "kid=a&key=k&offset=1"
import { readFileSync, writeFileSync } from "node:fs";
import { renderBmp } from "../src/render.js";

const [name, out, query = ""] = process.argv.slice(2);
const { default: informer } = await import(`../src/informers/${name}.js`);
const env = { ...process.env };
// KV keys: "data" is the school push, "agenda" the calendar push.
const files = { data: process.env.SCHOOL_JSON, agenda: process.env.AGENDA_JSON };
if (files.data || files.agenda) {
  env.SCHOOL = {
    get: async (key, type) => {
      if (!files[key]) return null;
      const stored = readFileSync(files[key], "utf8");
      return type === "json" ? JSON.parse(stored) : stored;
    },
  };
}
writeFileSync(out, await renderBmp(informer, new URLSearchParams(query), env));
console.log(`wrote ${out}`);
