// A tiny raster for e-ink screens: 8-bit gray pixels in, 1-bit BMP out.
// Workers have no canvas or font rasterizer; text comes from pre-baked glyph
// bitmaps (fonts.js) and shapes are filled pixel by pixel.

export const BLACK = 0;
export const GRAY = 160; // dithered to a 50% checkerboard
export const WHITE = 255;

const glyphCache = new WeakMap();

function glyphBits(font, ch) {
  let cache = glyphCache.get(font);
  if (!cache) glyphCache.set(font, (cache = new Map()));
  let entry = cache.get(ch);
  if (!entry) {
    const g = font.glyphs[ch] ?? font.glyphs["?"];
    const bits = g[5] ? Uint8Array.from(atob(g[5]), (c) => c.charCodeAt(0)) : new Uint8Array(0);
    entry = { advance: g[0], x: g[1], y: g[2], w: g[3], h: g[4], bits };
    cache.set(ch, entry);
  }
  return entry;
}

export class Canvas {
  constructor(width = 480, height = 800) {
    this.width = width;
    this.height = height;
    this.px = new Uint8Array(width * height).fill(WHITE);
  }

  set(x, y, v) {
    x |= 0;
    y |= 0;
    if (x >= 0 && y >= 0 && x < this.width && y < this.height) this.px[y * this.width + x] = v;
  }

  rect(x, y, w, h, v = BLACK) {
    for (let j = Math.max(0, y | 0); j < Math.min(this.height, y + h); j++) {
      this.px.fill(v, j * this.width + Math.max(0, x | 0), j * this.width + Math.min(this.width, x + w));
    }
  }

  frame(x, y, w, h, t = 2, v = BLACK) {
    this.rect(x, y, w, t, v);
    this.rect(x, y + h - t, w, t, v);
    this.rect(x, y, t, h, v);
    this.rect(x + w - t, y, t, h, v);
  }

  circle(cx, cy, r, v = BLACK) {
    for (let y = -r; y <= r; y++) {
      const dx = Math.sqrt(r * r - y * y);
      this.rect(Math.round(cx - dx), Math.round(cy + y), Math.round(2 * dx) + 1, 1, v);
    }
  }

  ring(cx, cy, r, t = 3, v = BLACK) {
    for (let y = -r; y <= r; y++) {
      for (let x = -r; x <= r; x++) {
        const d = Math.sqrt(x * x + y * y);
        if (d <= r && d > r - t) this.set(cx + x, cy + y, v);
      }
    }
  }

  // Thick line drawn as a run of discs.
  line(x0, y0, x1, y1, t = 3, v = BLACK) {
    const steps = Math.max(1, Math.ceil(Math.hypot(x1 - x0, y1 - y0)));
    const r = t / 2;
    for (let i = 0; i <= steps; i++) {
      const x = x0 + ((x1 - x0) * i) / steps;
      const y = y0 + ((y1 - y0) * i) / steps;
      if (r <= 1) this.set(x, y, v);
      else this.circle(Math.round(x), Math.round(y), Math.round(r), v);
    }
  }

  polyline(points, t = 3, v = BLACK) {
    for (let i = 1; i < points.length; i++) this.line(...points[i - 1], ...points[i], t, v);
  }

  textWidth(str, font) {
    let w = 0;
    for (const ch of str) w += glyphBits(font, ch).advance;
    return w;
  }

  // Draws `str` with its baseline at y. align: "left" | "center" | "right".
  text(str, x, y, font, { align = "left", color = BLACK } = {}) {
    if (align !== "left") x -= align === "center" ? this.textWidth(str, font) / 2 : this.textWidth(str, font);
    x = Math.round(x);
    for (const ch of str) {
      const g = glyphBits(font, ch);
      for (let j = 0; j < g.h; j++) {
        for (let i = 0; i < g.w; i++) {
          const bit = j * g.w + i;
          if (g.bits[bit >> 3] & (0x80 >> (bit & 7))) this.set(x + g.x + i, y + g.y + j, color);
        }
      }
      x += g.advance;
    }
    return x;
  }

  // Word-wraps `str` into `maxWidth`; returns the baseline below the last line.
  paragraph(str, x, y, maxWidth, font, { lineHeight, align = "left", color = BLACK } = {}) {
    lineHeight ??= Math.round((font.ascent + font.descent) * 1.15);
    const lines = [];
    let line = "";
    for (const word of str.split(/\s+/)) {
      const next = line ? `${line} ${word}` : word;
      if (line && this.textWidth(next, font) > maxWidth) {
        lines.push(line);
        line = word;
      } else {
        line = next;
      }
    }
    if (line) lines.push(line);
    for (const l of lines) {
      this.text(l, align === "center" ? x + maxWidth / 2 : x, y, font, { align, color });
      y += lineHeight;
    }
    return y;
  }

  // 1-bit BMP, bottom-up rows. Mid grays become a checkerboard so bars and
  // fills stay visible after thresholding.
  toBmp() {
    const { width: w, height: h, px } = this;
    const rowBytes = Math.ceil(w / 32) * 4;
    const dataOffset = 14 + 40 + 8;
    const size = dataOffset + rowBytes * h;
    const buf = new Uint8Array(size);
    const dv = new DataView(buf.buffer);
    buf[0] = 0x42;
    buf[1] = 0x4d;
    dv.setUint32(2, size, true);
    dv.setUint32(10, dataOffset, true);
    dv.setUint32(14, 40, true);
    dv.setInt32(18, w, true);
    dv.setInt32(22, h, true);
    dv.setUint16(26, 1, true);
    dv.setUint16(28, 1, true);
    dv.setUint32(34, rowBytes * h, true);
    dv.setUint32(46, 2, true);
    // Palette: index 0 black, index 1 white.
    buf.set([0, 0, 0, 0, 255, 255, 255, 0], 54);
    for (let y = 0; y < h; y++) {
      const row = dataOffset + (h - 1 - y) * rowBytes;
      for (let x = 0; x < w; x++) {
        const v = px[y * w + x];
        const white = v > 200 || (v >= 64 && (x + y) % 2 === 0);
        if (white) buf[row + (x >> 3)] |= 0x80 >> (x & 7);
      }
    }
    return buf;
  }
}
