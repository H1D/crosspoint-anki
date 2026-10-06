// Renders an informer locally without Wrangler, fetching live data:
//   node scripts/render.mjs weather out.bmp "lat=52.37&lon=4.90&place=Amsterdam&depth=2"
// Informers that read KV (school) take their data from a JSON file and their
// secrets from the environment:
//   SCHOOL_JSON=data.json SCHOOL_READ_KEY=k node scripts/render.mjs school out.bmp "kid=a&key=k"
import { readFileSync, writeFileSync } from "node:fs";
import { renderBmp } from "../src/render.js";

const [name, out, query = ""] = process.argv.slice(2);
const { default: informer } = await import(`../src/informers/${name}.js`);
const env = { ...process.env };
if (process.env.SCHOOL_JSON) {
  const stored = readFileSync(process.env.SCHOOL_JSON, "utf8");
  env.SCHOOL = { get: async (_key, type) => (type === "json" ? JSON.parse(stored) : stored) };
}
writeFileSync(out, await renderBmp(informer, new URLSearchParams(query), env));
console.log(`wrote ${out}`);
