// Weather icons drawn from circles and lines, sized by `s` (about the icon width).
import { BLACK, WHITE } from "./canvas.js";

function cloudShape(c, cx, cy, s, grow, v) {
  const blobs = [
    [-0.27, 0.08, 0.2],
    [0.0, -0.08, 0.27],
    [0.26, 0.08, 0.19],
  ];
  for (const [dx, dy, r] of blobs) c.circle(cx + dx * s, cy + dy * s, Math.max(1, r * s + grow), v);
  c.rect(cx - 0.27 * s, cy + 0.08 * s - grow, 0.53 * s, 0.2 * s + 2 * grow, v);
}

function cloud(c, cx, cy, s, t) {
  cloudShape(c, cx, cy, s, 0, BLACK);
  cloudShape(c, cx, cy, s, -t, WHITE);
}

function sun(c, cx, cy, s, t) {
  const r = s * 0.22;
  c.ring(cx, cy, r, t);
  for (let i = 0; i < 8; i++) {
    const a = (i * Math.PI) / 4;
    c.line(cx + Math.cos(a) * r * 1.45, cy + Math.sin(a) * r * 1.45, cx + Math.cos(a) * r * 2, cy + Math.sin(a) * r * 2, t);
  }
}

function drops(c, cx, cy, s, t, count) {
  for (let i = 0; i < count; i++) {
    const x = cx + (i - (count - 1) / 2) * s * 0.2;
    c.line(x, cy, x - s * 0.06, cy + s * 0.18, t);
  }
}

function flakes(c, cx, cy, s, t) {
  for (let i = 0; i < 3; i++) {
    const x = cx + (i - 1) * s * 0.24;
    const y = cy + (i % 2) * s * 0.1;
    const r = s * 0.07;
    for (let k = 0; k < 3; k++) {
      const a = (k * Math.PI) / 3;
      c.line(x - Math.cos(a) * r, y - Math.sin(a) * r, x + Math.cos(a) * r, y + Math.sin(a) * r, Math.max(2, t - 1));
    }
  }
}

function bolt(c, cx, cy, s, t) {
  c.polyline(
    [
      [cx + s * 0.05, cy],
      [cx - s * 0.08, cy + s * 0.17],
      [cx + s * 0.05, cy + s * 0.17],
      [cx - s * 0.06, cy + s * 0.34],
    ],
    t,
  );
}

// kind: sun | partly | cloud | rain | showers | thunder | snow | fog
export function drawIcon(c, kind, cx, cy, s) {
  const t = Math.max(2, Math.round(s / 28));
  switch (kind) {
    case "sun":
      sun(c, cx, cy, s, t);
      break;
    case "partly":
      sun(c, cx + s * 0.16, cy - s * 0.16, s * 0.75, t);
      cloudShape(c, cx - s * 0.06, cy + s * 0.1, s * 0.8, t * 2, WHITE);
      cloud(c, cx - s * 0.06, cy + s * 0.1, s * 0.8, t);
      break;
    case "cloud":
      cloud(c, cx, cy, s, t);
      break;
    case "rain":
      cloud(c, cx, cy - s * 0.12, s, t);
      drops(c, cx, cy + s * 0.22, s, t, 3);
      break;
    case "showers":
      sun(c, cx + s * 0.18, cy - s * 0.26, s * 0.6, t);
      cloudShape(c, cx - s * 0.04, cy - s * 0.06, s * 0.85, t * 2, WHITE);
      cloud(c, cx - s * 0.04, cy - s * 0.06, s * 0.85, t);
      drops(c, cx - s * 0.04, cy + s * 0.24, s, t, 2);
      break;
    case "thunder":
      cloud(c, cx, cy - s * 0.12, s, t);
      bolt(c, cx, cy + s * 0.12, s, t);
      break;
    case "snow":
      cloud(c, cx, cy - s * 0.12, s, t);
      flakes(c, cx, cy + s * 0.3, s, t);
      break;
    case "fog":
      for (let i = 0; i < 4; i++) {
        const w = s * (i % 2 ? 0.6 : 0.8);
        c.line(cx - w / 2, cy - s * 0.2 + i * s * 0.14, cx + w / 2, cy - s * 0.2 + i * s * 0.14, t);
      }
      break;
  }
}

// Small raindrop marker, for "chance of rain" labels.
export function drop(c, cx, cy, r) {
  c.circle(cx, cy, r, BLACK);
  for (let y = 0; y < r * 1.6; y++) {
    const half = r * (1 - y / (r * 1.6));
    c.rect(cx - half, cy - y, 2 * half, 1, BLACK);
  }
}

// Maps a Dutch weather description to an icon kind.
export function iconFor(description = "") {
  const d = description.toLowerCase();
  if (d.includes("onweer")) return "thunder";
  if (d.includes("sneeuw") || d.includes("hagel") || d.includes("ijzel")) return "snow";
  if (d.includes("mist") && !d.includes("regen")) return "fog";
  if (d.includes("bui")) return "showers";
  if (d.includes("regen") || d.includes("motregen")) return d.includes("zon") || d.includes("opklaring") ? "showers" : "rain";
  if (d.includes("zwaar bewolkt") || d === "bewolkt" || d.includes("betrokken")) return "cloud";
  if (d.includes("bewolkt") || d.includes("wolk")) return "partly";
  if (d.includes("zon") || d.includes("helder") || d.includes("onbewolkt")) return "sun";
  return "cloud";
}
