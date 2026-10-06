// Renders an informer locally without Wrangler, fetching live data:
//   node scripts/render.mjs weather out.bmp "lat=52.37&lon=4.90&place=Amsterdam&depth=2"
import { writeFileSync } from "node:fs";
import { renderBmp } from "../src/render.js";

const [name, out, query = ""] = process.argv.slice(2);
const { default: informer } = await import(`../src/informers/${name}.js`);
writeFileSync(out, await renderBmp(informer, new URLSearchParams(query)));
console.log(`wrote ${out}`);
