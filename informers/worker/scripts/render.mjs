// Renders an informer locally without Wrangler: bun scripts/render.mjs weather out.bmp "lat=52.37&lon=4.90&place=Amsterdam"
import { writeFileSync } from "node:fs";

const [name, out, query = ""] = process.argv.slice(2);
const { default: informer } = await import(`../src/informers/${name}.js`);
const canvas = await informer.render(new URLSearchParams(query));
writeFileSync(out, canvas.toBmp());
console.log(`wrote ${out}`);
