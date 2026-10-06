# Canvas API (`informers/worker/src/canvas.js`)

Coordinates are pixels, origin top-left, screen 480 wide by 800 high.

## Colors and depth

`BLACK` (0), `DARK_GRAY` (85), `GRAY` (170), `WHITE` (255): the panel's four
native levels.

The router creates the canvas from the query: `depth=2` gives a 2-bit BMP
(each pixel rounded to the nearest native level, text anti-aliased), anything
else a 1-bit BMP (text solid; grays become a 2x2 ordered dither, so `GRAY`
leaves 1 pixel in 4 black and `DARK_GRAY` 3 in 4). `canvas.gray` and
`canvas.depth` tell an informer which one it is drawing.

## Canvas

| Method | Draws |
| --- | --- |
| `new Canvas({ width = 480, height = 800, depth = 1 })` | white canvas (informers get one; they do not create it) |
| `rect(x, y, w, h, color = BLACK)` | filled rectangle |
| `frame(x, y, w, h, thickness = 2, color)` | rectangle outline |
| `circle(cx, cy, r, color)` | filled circle |
| `ring(cx, cy, r, thickness = 3, color)` | circle outline |
| `line(x0, y0, x1, y1, thickness = 3, color)` | thick line with round ends |
| `polyline([[x, y], ...], thickness, color)` | connected lines |
| `text(str, x, y, font, { align, color })` | text; `y` is the baseline; `align` is `left`, `center`, or `right` (x is then the center or right edge); returns the x after the text |
| `textWidth(str, font)` | width in pixels |
| `paragraph(str, x, y, maxWidth, font, { lineHeight, align, color })` | word-wrapped text from baseline `y`; returns the baseline after the last line |
| `toBmp()` | `Uint8Array` with the BMP at the canvas depth (done by `render.js`) |

## Fonts (`import * as F from "../fonts.js"`)

| Font | Face | Size | Characters |
| --- | --- | --- | --- |
| `F.small` | Noto Sans Regular | 20px | text set |
| `F.body` | Noto Sans Regular | 26px | text set |
| `F.bodyBold` | Noto Sans Bold | 26px | text set |
| `F.title` | Noto Sans Bold | 34px | text set |
| `F.big` | Noto Sans Bold | 44px | text set |
| `F.huge` | Noto Sans Bold | 150px | `0-9 - °` only |

Text set: printable ASCII plus `°éëèêïöüáóúàç–·`. A baseline at `y` means
capitals reach up about 0.73 × size above `y`.

## Icons (`import { drawIcon, drop, iconFor } from "../icons.js"`)

- `drawIcon(canvas, kind, cx, cy, size)`: `kind` is `sun`, `partly`, `cloud`,
  `rain`, `showers`, `thunder`, `snow`, or `fog`; centered on `cx, cy`, about
  `size` wide.
- `drop(canvas, cx, cy, r)`: small raindrop marker.
- `iconFor(dutchDescription)`: maps a Buienradar text such as "Zwaar bewolkt"
  to a `kind`.

## Time (`import { clock, dateLong, weekdayShort } from "../time.js"`)

All in Europe/Amsterdam, Dutch:

- `clock(ms)` → `"18:25"`
- `dateLong(ms)` → `"dinsdag 6 oktober"`
- `weekdayShort("2026-10-07T00:00:00")` → `"wo"` (for date-only local stamps)
