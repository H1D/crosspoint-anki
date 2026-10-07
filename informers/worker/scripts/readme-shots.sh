#!/usr/bin/env bash
# Regenerates the README screenshots in English: live weather with a made-up
# shower, the holiday countdown, and the school and day screens from demo data.
set -euo pipefail
cd "$(dirname "$0")/.."
out=../../docs/informers/img
tmp=$(mktemp -d)
mkdir -p "$out"
node scripts/render.mjs weather "$tmp/weather.bmp" "lat=52.37&lon=4.90&place=Amsterdam&depth=2&lang=en&demo=rain" >/dev/null
node scripts/render.mjs vakantie "$tmp/holidays.bmp" "regio=midden&depth=2&lang=en&vandaag=2026-10-06" >/dev/null
for kid in emma noah; do
  SCHOOL_JSON=demo/school-en.json SCHOOL_READ_KEY=demo node scripts/render.mjs school "$tmp/school-$kid.bmp" \
    "kid=$kid&key=demo&vandaag=2026-10-06&depth=2&lang=en" >/dev/null
done
for offset in 0 1 2; do
  SCHOOL_JSON=demo/school-en.json AGENDA_JSON=demo/agenda-en.json SCHOOL_READ_KEY=demo node scripts/render.mjs dag \
    "$tmp/day-$offset.bmp" "kid=emma&key=demo&vandaag=2026-10-06&depth=2&lang=en&offset=$offset&demo=rain" >/dev/null
done
for name in weather holidays school-emma school-noah day-0 day-1 day-2; do
  python3 scripts/preview.py "$tmp/$name.bmp" "$out/$name.png" >/dev/null
done
rm -rf "$tmp"
ls "$out"
