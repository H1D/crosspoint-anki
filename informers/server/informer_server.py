#!/usr/bin/env python3
"""Renders informer screens as 1-bit 480x800 BMPs for CrossPoint SD plugins.

The reader stays a thin client: a plugin's `sleep.enter` download handler
fetches a finished image from here and the firmware only displays it.

    GET /weather.bmp?lat=52.37&lon=4.90&place=Amsterdam

Data comes from Open-Meteo (no API key). Run with:

    python3 informer_server.py --port 8790
"""

import argparse
import io
import json
import logging
import urllib.parse
import urllib.request
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from PIL import Image, ImageDraw, ImageFont

W, H = 480, 800
FONT_DIRS = ["/usr/share/fonts/noto", "/usr/share/fonts/truetype/noto", "/usr/share/fonts/TTF"]

# WMO weather interpretation codes, as documented by Open-Meteo.
WMO = {
    0: "Clear",
    1: "Mostly clear",
    2: "Partly cloudy",
    3: "Overcast",
    45: "Fog",
    48: "Rime fog",
    51: "Light drizzle",
    53: "Drizzle",
    55: "Heavy drizzle",
    56: "Freezing drizzle",
    57: "Freezing drizzle",
    61: "Light rain",
    63: "Rain",
    65: "Heavy rain",
    66: "Freezing rain",
    67: "Freezing rain",
    71: "Light snow",
    73: "Snow",
    75: "Heavy snow",
    77: "Snow grains",
    80: "Light showers",
    81: "Showers",
    82: "Heavy showers",
    85: "Snow showers",
    86: "Snow showers",
    95: "Thunderstorm",
    96: "Thunderstorm, hail",
    99: "Thunderstorm, hail",
}


def font(size, bold=False):
    name = "NotoSans-Bold.ttf" if bold else "NotoSans-Regular.ttf"
    for d in FONT_DIRS:
        try:
            return ImageFont.truetype(f"{d}/{name}", size)
        except OSError:
            continue
    return ImageFont.load_default(size)


def fetch_forecast(lat, lon):
    query = urllib.parse.urlencode(
        {
            "latitude": lat,
            "longitude": lon,
            "current": "temperature_2m,apparent_temperature,weather_code,wind_speed_10m,relative_humidity_2m",
            "hourly": "temperature_2m,precipitation_probability",
            "daily": "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset",
            "timezone": "auto",
            "forecast_days": 6,
            "forecast_hours": 24,
            "wind_speed_unit": "kmh",
        }
    )
    with urllib.request.urlopen(f"https://api.open-meteo.com/v1/forecast?{query}", timeout=10) as resp:
        return json.load(resp)


def draw_hourly(d, data, top):
    """Temperature line with precipitation-chance bars for the next 24 hours."""
    left, right, height = 40, W - 20, 150
    temps = data["hourly"]["temperature_2m"]
    rain = data["hourly"]["precipitation_probability"]
    times = data["hourly"]["time"]
    lo, hi = min(temps), max(temps)
    span = max(hi - lo, 1)
    step = (right - left) / (len(temps) - 1)

    for i, p in enumerate(rain):
        if p:
            x = left + i * step
            bar = height * p / 100
            d.rectangle([x - step / 2 + 2, top + height - bar, x + step / 2 - 2, top + height], fill=170)

    points = [(left + i * step, top + height - 10 - (t - lo) / span * (height - 30)) for i, t in enumerate(temps)]
    d.line(points, fill=0, width=4)
    d.line([left, top + height, right, top + height], fill=0, width=2)

    small = font(18)
    d.text((left - 8, top + 4), f"{hi:.0f}°", font=small, fill=0, anchor="rt")
    d.text((left - 8, top + height - 14), f"{lo:.0f}°", font=small, fill=0, anchor="rt")
    for i in range(0, len(times), 6):
        d.text((left + i * step, top + height + 6), times[i][11:13], font=small, fill=0, anchor="mt")


def render_weather(data, place):
    img = Image.new("L", (W, H), 255)
    d = ImageDraw.Draw(img)
    cur = data["current"]
    daily = data["daily"]
    now = datetime.fromisoformat(cur["time"])

    d.text((24, 20), place or "Weather", font=font(34, bold=True), fill=0)
    d.text((W - 24, 30), now.strftime("%a %d %b"), font=font(24), fill=0, anchor="ra")

    d.text((24, 80), f"{cur['temperature_2m']:.0f}°", font=font(150, bold=True), fill=0)
    d.text((W - 24, 120), WMO.get(cur["weather_code"], "?"), font=font(30, bold=True), fill=0, anchor="ra")
    details = [
        f"Feels {cur['apparent_temperature']:.0f}°",
        f"H {daily['temperature_2m_max'][0]:.0f}°  L {daily['temperature_2m_min'][0]:.0f}°",
        f"Wind {cur['wind_speed_10m']:.0f} km/h",
        f"Rain {daily['precipitation_probability_max'][0]}%",
    ]
    for i, line in enumerate(details):
        d.text((W - 24, 170 + i * 30), line, font=font(22), fill=0, anchor="ra")

    d.line([24, 312, W - 24, 312], fill=0, width=2)
    d.text((24, 324), "Next 24 hours", font=font(22, bold=True), fill=0)
    draw_hourly(d, data, 360)

    d.line([24, 560, W - 24, 560], fill=0, width=2)
    row = font(24)
    for i in range(1, len(daily["time"])):
        y = 576 + (i - 1) * 36
        day = datetime.fromisoformat(daily["time"][i]).strftime("%a")
        d.text((24, y), day, font=font(24, bold=True), fill=0)
        d.text((96, y), WMO.get(daily["weather_code"][i], "?"), font=row, fill=0)
        d.text((W - 110, y), f"{daily['precipitation_probability_max'][i]}%", font=row, fill=0, anchor="ra")
        d.text(
            (W - 24, y),
            f"{daily['temperature_2m_max'][i]:.0f}/{daily['temperature_2m_min'][i]:.0f}°",
            font=row,
            fill=0,
            anchor="ra",
        )

    sunrise = daily["sunrise"][0][11:16]
    sunset = daily["sunset"][0][11:16]
    footer = f"Sun {sunrise}–{sunset}   ·   updated {now.strftime('%H:%M')}"
    d.text((W // 2, H - 22), footer, font=font(18), fill=0, anchor="ms")
    return img


def to_bmp(img):
    # Gray bars become a 50% dither; text and lines stay crisp black.
    bw = img.point(lambda v: 0 if v < 128 else (255 if v > 200 else v)).convert("1")
    out = io.BytesIO()
    bw.save(out, format="BMP")
    return out.getvalue()


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = {k: v[0] for k, v in urllib.parse.parse_qs(url.query).items()}
        try:
            if url.path == "/weather.bmp":
                body = to_bmp(render_weather(fetch_forecast(q["lat"], q["lon"]), q.get("place", "")))
            else:
                self.send_error(404)
                return
        except KeyError as e:
            self.send_error(400, f"missing parameter {e}")
            return
        except Exception:
            logging.exception("render failed")
            self.send_error(502)
            return
        self.send_response(200)
        self.send_header("Content-Type", "image/bmp")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8790)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO)
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
