// Fetches upstream JSON through Cloudflare's edge cache, so frequent sleeps
// don't hammer the data sources while every image is still drawn fresh (its
// printed time is the time of the request). `cf` is ignored outside Workers.
export async function fetchJson(url, { headers = {}, ttl = 300 } = {}) {
  const res = await fetch(url, {
    headers: { Accept: "application/json", ...headers },
    cf: { cacheTtl: ttl, cacheEverything: true },
  });
  if (!res.ok) throw new Error(`${url} -> ${res.status}`);
  return res.json();
}
