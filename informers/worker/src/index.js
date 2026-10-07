// Informer screens for CrossPoint readers: GET /<name>.bmp returns a 480x800
// BMP (1-bit, or 4-level gray with depth=2) that an SD plugin downloads on
// sleep. Register new informers here.
import dag, { pushAgenda } from "./informers/dag.js";
import school, { HttpError, push as pushSchool } from "./informers/school.js";
import vakantie from "./informers/vakantie.js";
import weather from "./informers/weather.js";
import { renderBmp } from "./render.js";

const INFORMERS = { weather, vakantie, school, dag };

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/") {
      const list = Object.keys(INFORMERS).map((n) => `/${n}.bmp`);
      return Response.json({ informers: list });
    }
    const pushes = { "/school/data": pushSchool, "/agenda/data": pushAgenda };
    if (pushes[url.pathname] && request.method === "PUT") {
      try {
        return await pushes[url.pathname](request, env);
      } catch (err) {
        if (err instanceof HttpError) return new Response(err.message, { status: err.status });
        throw err;
      }
    }
    const match = url.pathname.match(/^\/([a-z0-9-]+)\.bmp$/);
    const informer = match && INFORMERS[match[1]];
    if (!informer) return new Response("not found", { status: 404 });

    let bmp;
    try {
      bmp = await renderBmp(informer, url.searchParams, env);
    } catch (err) {
      if (err instanceof HttpError) return new Response(err.message, { status: err.status });
      console.error(match[1], err);
      // A non-2xx keeps the reader's previous image in place.
      return new Response("upstream data unavailable", { status: 502 });
    }
    // Drawn per request so the printed time is always now; data is cached in fetchJson.
    return new Response(bmp, { headers: { "Content-Type": "image/bmp", "Cache-Control": "no-store" } });
  },
};
