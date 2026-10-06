// Informer screens for CrossPoint readers: GET /<name>.bmp returns a 480x800
// 1-bit BMP that an SD plugin downloads on sleep. Register new informers here.
import weather from "./informers/weather.js";

const INFORMERS = { weather };
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

    let canvas;
    try {
      canvas = await informer.render(url.searchParams, env);
    } catch (err) {
      console.error(match[1], err);
      // A non-2xx keeps the reader's previous image in place.
      return new Response("upstream data unavailable", { status: 502 });
    }
    const res = new Response(canvas.toBmp(), {
      headers: { "Content-Type": "image/bmp", "Cache-Control": `public, max-age=${CACHE_SECONDS}` },
    });
    ctx.waitUntil(cache.put(request, res.clone()));
    return res;
  },
};
