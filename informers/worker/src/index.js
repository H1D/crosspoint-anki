// Informer screens for CrossPoint readers: GET /<name>.bmp returns a 480x800
// BMP (1-bit, or 4-level gray with depth=2) that an SD plugin downloads on
// sleep. Register new informers here.
import vakantie from "./informers/vakantie.js";
import weather from "./informers/weather.js";
import { renderBmp } from "./render.js";

const INFORMERS = { weather, vakantie };
const CACHE_SECONDS = 300;

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    if (url.pathname === "/") {
      const list = Object.keys(INFORMERS).map((n) => `/${n}.bmp`);
      return Response.json({ informers: list });
    }
    const match = url.pathname.match(/^\/([a-z0-9-]+)\.bmp$/);
    const informer = match && INFORMERS[match[1]];
    if (!informer) return new Response("not found", { status: 404 });

    const cache = caches.default;
    const cached = await cache.match(request);
    if (cached) return cached;

    let bmp;
    try {
      bmp = await renderBmp(informer, url.searchParams, env);
    } catch (err) {
      console.error(match[1], err);
      // A non-2xx keeps the reader's previous image in place.
      return new Response("upstream data unavailable", { status: 502 });
    }
    const res = new Response(bmp, {
      headers: { "Content-Type": "image/bmp", "Cache-Control": `public, max-age=${CACHE_SECONDS}` },
    });
    ctx.waitUntil(cache.put(request, res.clone()));
    return res;
  },
};
