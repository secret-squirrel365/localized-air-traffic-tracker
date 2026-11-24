#!/usr/bin/env python3
"""Sky Dial map maker: builds skymap.bin (outline map) for the radar.
Usage: python make_skymap.py LAT LON [RADIUS_KM] [DATA_DIR]
Data: Natural Earth 10m GeoJSON (public domain) + OurAirports CSV (public domain).

File format (little-endian), version 1:
  "SKYM" | u8 version | u8 reserved | u16 recordCount | f32 originLat | f32 originLon
  record: u8 type | u8 nPts | nPts x (i16 dLat, i16 dLon) in 1e-4 degree units
          type 6 (AIRPORT) is followed by u8 labelLen + label bytes
types: 1 HIGHWAY  2 RIVER  3 COUNTY  4 CITY  5 RUNWAY  6 AIRPORT(1 pt + label)
"""
import csv, json, math, struct, sys, os

HIGHWAY, RIVER, COUNTY, CITY, RUNWAY, AIRPORT = 1, 2, 3, 4, 5, 6

def main():
    lat0, lon0 = float(sys.argv[1]), float(sys.argv[2])
    radius = float(sys.argv[3]) if len(sys.argv) > 3 else 35.0
    ddir = sys.argv[4] if len(sys.argv) > 4 else "."
    coslat = math.cos(math.radians(lat0))

    def km(lon, lat):
        return ((lon - lon0) * 111.32 * coslat, (lat - lat0) * 110.57)

    def inside(lon, lat, pad=0.0):
        x, y = km(lon, lat)
        return x * x + y * y <= (radius + pad) ** 2

    def enc(lon, lat):
        dla = round((lat - lat0) * 1e4); dlo = round((lon - lon0) * 1e4)
        assert -32768 <= dla <= 32767 and -32768 <= dlo <= 32767
        return dla, dlo

    records = []

    def add_line(kind, coords):
        # Split into runs of points near the circle, keep one neighbour on each
        # side so lines reach the edge; the board clips to the radar circle.
        run = []
        n = len(coords)
        keep = [inside(c[0], c[1], 3.0) for c in coords]
        for i in range(n):
            near = keep[i] or (i > 0 and keep[i - 1]) or (i < n - 1 and keep[i + 1])
            if near:
                run.append(coords[i])
            elif run:
                flush(kind, run); run = []
        if run: flush(kind, run)

    def flush(kind, run):
        if len(run) < 2: return
        for s in range(0, len(run) - 1, 254):           # max 255 points per record
            chunk = run[s:s + 255]
            if len(chunk) >= 2:
                records.append((kind, [enc(c[0], c[1]) for c in chunk], None))

    def geom_lines(g):
        if not g: return []
        t, c = g["type"], g["coordinates"]
        if t == "LineString": return [c]
        if t in ("MultiLineString", "Polygon"): return c
        if t == "MultiPolygon": return [r for p in c for r in p]
        return []

    layers = [
        ("ne_10m_admin_2_counties.geojson", COUNTY, None),
        ("ne_10m_urban_areas.geojson", CITY, None),
        ("ne_10m_rivers_north_america.geojson", RIVER, None),
        ("ne_10m_rivers_lake_centerlines.geojson", RIVER, None),
        ("ne_10m_lakes_north_america.geojson", RIVER, None),
        ("ne_10m_lakes.geojson", RIVER, None),
        ("ne_10m_roads.geojson", HIGHWAY, None),
    ]
    for fn, kind, _ in layers:
        path = os.path.join(ddir, fn)
        if not os.path.exists(path):
            print("  (skipping missing", fn + ")"); continue
        data = json.load(open(path, encoding="utf-8"))
        before = len(records)
        for ft in data["features"]:
            for ln in geom_lines(ft.get("geometry")):
                if any(inside(p[0], p[1], 3.0) for p in ln):
                    add_line(kind, ln)
        print(f"  {fn}: {len(records) - before} lines")

    # Airports + runways (OurAirports)
    airports = {}
    for r in csv.DictReader(open(os.path.join(ddir, "airports.csv"), encoding="utf-8")):
        if r["type"] not in ("large_airport", "medium_airport", "small_airport"): continue
        if not r["latitude_deg"]: continue
        la, lo = float(r["latitude_deg"]), float(r["longitude_deg"])
        if inside(lo, la): airports[r["ident"]] = (la, lo, r["type"])
    nrw = 0
    with_runways = set()
    for r in csv.DictReader(open(os.path.join(ddir, "runways.csv"), encoding="utf-8")):
        if r["airport_ident"] not in airports or r["closed"] == "1": continue
        if not (r["le_latitude_deg"] and r["he_latitude_deg"]): continue
        a = (float(r["le_longitude_deg"]), float(r["le_latitude_deg"]))
        b = (float(r["he_longitude_deg"]), float(r["he_latitude_deg"]))
        records.append((RUNWAY, [enc(*a), enc(*b)], None)); nrw += 1
        with_runways.add(r["airport_ident"])
    # Labels: big airports first so they win if the board runs out of room.
    # Small strips only get a label if they have real runway data (skips
    # private strips with codes like "US-129").
    order = {"large_airport": 0, "medium_airport": 1, "small_airport": 2}
    for ident, (la, lo, t) in sorted(airports.items(), key=lambda kv: order[kv[1][2]]):
        if t == "small_airport" and ident not in with_runways: continue
        label = ident[1:] if (len(ident) == 4 and ident.startswith("K")) else ident
        records.append((AIRPORT, [enc(lo, la)], (label[:6], order[t])))
    print(f"  airports: {len(airports)}  runways: {nrw}")

    out = bytearray(b"SKYM")
    out += struct.pack("<BBHff", 1, 0, len(records), lat0, lon0)
    for kind, pts, extra in records:
        if kind == AIRPORT:
            label, size = extra
            out += struct.pack("<BB", kind, 1) + struct.pack("<hh", *pts[0])
            lb = label.encode("ascii", "replace")
            out += struct.pack("<BB", len(lb), size) + lb
        else:
            out += struct.pack("<BB", kind, len(pts))
            for p in pts: out += struct.pack("<hh", *p)
    with open("skymap.bin", "wb") as f: f.write(out)
    print(f"skymap.bin: {len(records)} records, {len(out)} bytes")

if __name__ == "__main__":
    main()
