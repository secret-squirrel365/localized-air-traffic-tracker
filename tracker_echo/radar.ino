// ─── Sky Dial: Radar screen ─────────────────────────────
// Draws a radar scope on the left (planes around home) and a
// flight info panel on the right. Tap a plane to select it.
#if HAS_TOUCH

// Radar geometry (content area is y = CONTENT_Y .. H - FOOT_H)
#define RADAR_R    112
#define RADAR_CX   (RADAR_R + 8)
#define RADAR_CY   (CONTENT_Y + CONTENT_H / 2)
#define PANEL_X    (RADAR_CX + RADAR_R + 12)
#define PANEL_W    (W - PANEL_X - 6)
#define TAP_RADIUS 28          // big tap area for the stylus (pixels)

#define C_RING     0x0320      // dim green rings
#define C_RING_HI  0x05A0      // brighter green outer ring

// Turn a plane's lat/lon into a spot on the radar. North is up.
// Returns false if the plane is outside the circle.
static bool radarProject(float lat, float lon, int& px, int& py) {
  float dxKm = (lon - HOME_LON) * 111.32f * cosf(HOME_LAT * DEG_TO_RAD);
  float dyKm = (lat - HOME_LAT) * 110.57f;
  float scale = (float)RADAR_R / GEOFENCE_KM;
  float x = dxKm * scale, y = dyKm * scale;
  if (x * x + y * y > (float)(RADAR_R * RADAR_R)) return false;
  px = RADAR_CX + (int)x;
  py = RADAR_CY - (int)y;
  return true;
}

// ─── Sky Dial: outline map (skymap.bin on the SD card) ─────
// Made on a PC by make_skymap.py. Kept in RAM (it's tiny) so drawing is fast.
#define MAP_MAX_BYTES 16384
#define C_MAP_HWY     0x6180   // dim orange  — highways
#define C_MAP_WATER   0x01F4   // dim blue    — rivers / lakes
#define C_MAP_COUNTY  0x39E7   // grey        — county lines
#define C_MAP_CITY    0x318A   // blue-grey   — city outline
#define C_MAP_RWY     0xCE59   // light grey  — runways
#define C_MAP_APT     0x8C71   // grey        — airport labels

static uint8_t* mapBuf   = nullptr;
static size_t   mapLen   = 0;
static bool     mapTried = false;
static uint16_t mapCount = 0;
static float    mapOLat = 0, mapOLon = 0;

static void loadSkyMap() {
  mapTried = true;
#if HAS_SD
  if (!sdAvailable) { Serial.println("[MAP] No SD card"); return; }
  File f = SD.open("/skymap.bin", FILE_READ);
  if (!f) { Serial.println("[MAP] /skymap.bin not found"); return; }
  size_t sz = f.size();
  if (sz < 16 || sz > MAP_MAX_BYTES) {
    Serial.printf("[MAP] Bad size %u\n", (unsigned)sz); f.close(); return;
  }
  mapBuf = (uint8_t*)malloc(sz);
  if (!mapBuf) { Serial.println("[MAP] Out of memory"); f.close(); return; }
  mapLen = f.read(mapBuf, sz);
  f.close();
  if (mapLen != sz || memcmp(mapBuf, "SKYM", 4) != 0 || mapBuf[4] != 1) {
    Serial.println("[MAP] Not a Sky Dial map file");
    free(mapBuf); mapBuf = nullptr; mapLen = 0; return;
  }
  mapCount = mapBuf[6] | (mapBuf[7] << 8);
  memcpy(&mapOLat, mapBuf + 8, 4);
  memcpy(&mapOLon, mapBuf + 12, 4);
  Serial.printf("[MAP] Loaded %u items (%u bytes)\n", mapCount, (unsigned)mapLen);
#endif
}

// Map point -> screen offset from the radar centre (uses the same math as planes)
static void mapPoint(const uint8_t* p, float kx, float ky, float& x, float& y) {
  int16_t dla, dlo;
  memcpy(&dla, p, 2); memcpy(&dlo, p + 2, 2);
  float lat = mapOLat + dla * 1e-4f;
  float lon = mapOLon + dlo * 1e-4f;
  x =  (lon - HOME_LON) * kx;
  y = -(lat - HOME_LAT) * ky;
}

// Cut a line so only the part inside the radar circle is left
static bool clipToCircle(float& x0, float& y0, float& x1, float& y1, float r) {
  float dx = x1 - x0, dy = y1 - y0;
  float a = dx * dx + dy * dy;
  float c = x0 * x0 + y0 * y0 - r * r;
  if (a < 1e-6f) return c <= 0;
  float b = 2 * (x0 * dx + y0 * dy);
  float disc = b * b - 4 * a * c;
  if (disc < 0) return false;
  float sq = sqrtf(disc);
  float t0 = (-b - sq) / (2 * a), t1 = (-b + sq) / (2 * a);
  if (t1 < 0 || t0 > 1) return false;
  float ta = fmaxf(t0, 0.0f), tb = fminf(t1, 1.0f);
  float nx0 = x0 + dx * ta, ny0 = y0 + dy * ta;
  x1 = x0 + dx * tb; y1 = y0 + dy * tb;
  x0 = nx0; y0 = ny0;
  return true;
}

static void drawSkyMap() {
  if (!mapTried) loadSkyMap();
  if (!mapBuf) return;
  const float r  = RADAR_R - 1;
  const float sc = (float)RADAR_R / GEOFENCE_KM;
  const float kx = 111.32f * cosf(HOME_LAT * DEG_TO_RAD) * sc;
  const float ky = 110.57f * sc;
  size_t p = 16;
  for (uint16_t i = 0; i < mapCount && p + 2 <= mapLen; i++) {
    uint8_t type = mapBuf[p], n = mapBuf[p + 1];
    p += 2;
    if (p + 4 * (size_t)n > mapLen) break;
    if (type == 6) {                      // airport dot + label
      float x, y;
      mapPoint(mapBuf + p, kx, ky, x, y);
      p += 4;
      if (p + 2 > mapLen) break;
      uint8_t len = mapBuf[p];
      p += 2;
      if (p + len > mapLen) break;
      char lab[8];
      uint8_t L = len < 7 ? len : 7;
      memcpy(lab, mapBuf + p, L); lab[L] = 0;
      p += len;
      if (x * x + y * y <= r * r) {
        int px = RADAR_CX + (int)x, py = RADAR_CY + (int)y;
        tft.fillRect(px - 1, py - 1, 3, 3, C_MAP_APT);
        int lw = L * 6;
        int edge = RADAR_CX + (int)sqrtf(fmaxf(r * r - y * y, 0.0f));
        int lx = (px + 4 + lw > edge) ? px - 4 - lw : px + 4;   // keep label inside
        tft.setTextSize(1);
        tft.setTextColor(C_MAP_APT);                           // see-through text
        tft.setCursor(lx, py - 3);
        tft.print(lab);
      }
      continue;
    }
    uint16_t col = type == 1 ? C_MAP_HWY : type == 2 ? C_MAP_WATER :
                   type == 3 ? C_MAP_COUNTY : type == 4 ? C_MAP_CITY : C_MAP_RWY;
    float x0, y0;
    mapPoint(mapBuf + p, kx, ky, x0, y0);
    for (uint8_t k = 1; k < n; k++) {
      float x1, y1;
      mapPoint(mapBuf + p + 4 * k, kx, ky, x1, y1);
      float a0 = x0, b0 = y0, a1 = x1, b1 = y1;
      if (clipToCircle(a0, b0, a1, b1, r)) {
        int sx0 = RADAR_CX + (int)a0, sy0 = RADAR_CY + (int)b0;
        int sx1 = RADAR_CX + (int)a1, sy1 = RADAR_CY + (int)b1;
        tft.drawLine(sx0, sy0, sx1, sy1, col);
        if (type == 5) tft.drawLine(sx0 + 1, sy0, sx1 + 1, sy1, col);   // thicker runways
      }
      x0 = x1; y0 = y1;
    }
    p += 4 * (size_t)n;
  }
}

static void drawRadarScope() {
  // Range rings at 1/3, 2/3 and full range
  tft.drawCircle(RADAR_CX, RADAR_CY, RADAR_R,         C_RING_HI);
  tft.drawCircle(RADAR_CX, RADAR_CY, RADAR_R * 2 / 3, C_RING);
  tft.drawCircle(RADAR_CX, RADAR_CY, RADAR_R / 3,     C_RING);
  // Cross lines
  tft.drawFastHLine(RADAR_CX - RADAR_R, RADAR_CY, RADAR_R * 2, C_RING);
  tft.drawFastVLine(RADAR_CX, RADAR_CY - RADAR_R, RADAR_R * 2, C_RING);
  // Compass letters
  tft.setTextSize(1);
  tft.setTextColor(C_GREEN, C_BG);
  tft.setCursor(RADAR_CX - 2, RADAR_CY - RADAR_R + 3);  tft.print("N");
  tft.setCursor(RADAR_CX - 2, RADAR_CY + RADAR_R - 10); tft.print("S");
  tft.setCursor(RADAR_CX + RADAR_R - 9, RADAR_CY - 3);  tft.print("E");
  tft.setCursor(RADAR_CX - RADAR_R + 4, RADAR_CY - 3);  tft.print("W");
  // Range label (miles) at the outer ring
  char buf[12];
  snprintf(buf, sizeof(buf), "%.0f MI", GEOFENCE_KM * 0.621371f);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(RADAR_CX + 4, RADAR_CY - RADAR_R + 14);
  tft.print(buf);
  // Home
  tft.fillCircle(RADAR_CX, RADAR_CY, 3, C_CYAN);
}

// Small arrow pointing the way the plane is flying
static void drawPlaneIcon(int px, int py, int track, uint16_t col, bool selected) {
  if (track >= 0) {
    float t = track * DEG_TO_RAD;
    float s = sinf(t), c = cosf(t);
    int tipX = px + (int)(8 * s),            tipY = py - (int)(8 * c);
    int lX   = px + (int)(-5 * c - 5 * s),   lY   = py + (int)(-5 * s + 5 * c);
    int rX   = px + (int)( 5 * c - 5 * s),   rY   = py + (int)( 5 * s + 5 * c);
    tft.fillTriangle(tipX, tipY, lX, lY, rX, rY, col);
    // Heading line ahead of the plane
    tft.drawLine(tipX, tipY, px + (int)(16 * s), py - (int)(16 * c), col);
  } else {
    tft.fillCircle(px, py, 3, col);
  }
  if (selected) tft.drawCircle(px, py, 11, C_YELLOW);
}

static void drawRadarPlanes() {
  for (int i = 0; i < flightCount; i++) {
    const Flight& f = flights[i];
    int px, py;
    if (!radarProject(f.lat, f.lon, px, py)) continue;
    bool emerg = strcmp(f.squawk,"7700")==0 || strcmp(f.squawk,"7600")==0 || strcmp(f.squawk,"7500")==0;
    bool mil = isMilitary(f);
    uint16_t col = emerg ? C_RED : (mil ? AL_MIL : statusColor(f.status));
    bool sel = (i == flightIndex);
    drawPlaneIcon(px, py, f.track, col, sel);
    // Callsign label next to the plane
    tft.setTextSize(1);
    tft.setTextColor(sel ? C_YELLOW : (mil ? AL_MIL : C_DIM), C_BG);
    int lx = px + 9, ly = py + 6;
    int lw = (int)strlen(f.callsign) * 6;
    if (lx + lw > RADAR_CX + RADAR_R) lx = px - 9 - lw;   // keep label inside the scope
    tft.setCursor(lx, ly);
    tft.print(f.callsign[0] ? f.callsign : "?");
  }
}

// Print up to 2 lines of size-1 text inside the panel width
static void panelPrint2(const char* s, int x, int& y, uint16_t col) {
  const int maxChars = PANEL_W / 6;
  char line[48];
  tft.setTextSize(1);
  tft.setTextColor(col, C_BG);
  int len = strlen(s);
  for (int start = 0, n = 0; start < len && n < 2; n++) {
    int take = min(maxChars, len - start);
    strlcpy(line, s + start, min((int)sizeof(line), take + 1));
    tft.setCursor(x, y); tft.print(line);
    y += 10; start += take;
  }
}

static void drawRadarPanel() {
  int x = PANEL_X, y = CONTENT_Y + 6;
  tft.drawFastVLine(PANEL_X - 6, CONTENT_Y + 4, CONTENT_H - 8, C_DIMMER);
  if (flightCount == 0) {
    tft.setTextSize(2); tft.setTextColor(C_GREEN, C_BG);
    tft.setCursor(x, y + 40); tft.print("CLEAR SKIES");
    tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(x, y + 64); tft.print("No planes in range.");
    tft.setCursor(x, y + 76); tft.print("Tap GEO for more range.");
    return;
  }
  const Flight& f = flights[flightIndex];
  // Callsign + airline
  tft.setTextSize(3); tft.setTextColor(C_AMBER, C_BG);
  tft.setCursor(x, y); tft.print(f.callsign[0] ? f.callsign : "UNKNOWN");
  y += 26;
  const Airline* al = getAirline(f.callsign);
  panelPrint2(al ? al->name : "UNKNOWN AIRLINE", x, y, al ? al->color : C_DIM);
  y += 2;
  tft.drawFastHLine(x, y, PANEL_W, C_DIMMER); y += 6;
  // Aircraft type
  panelPrint2(getAircraftTypeName(f.type), x, y, C_CYAN);
  // Route
  if (f.route[0]) panelPrint2(f.route, x, y, C_YELLOW);
  else            panelPrint2("NO ROUTE DATA", x, y, C_DIMMER);
  y += 2;
  tft.drawFastHLine(x, y, PANEL_W, C_DIMMER); y += 6;
  // Altitude / speed / distance (US units)
  char buf[24];
  tft.setTextSize(1);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(x, y); tft.print("ALT");
  tft.setCursor(x + 80, y); tft.print("SPEED");
  tft.setCursor(x + 160, y); tft.print("DIST");
  y += 10;
  tft.setTextSize(2); tft.setTextColor(C_AMBER, C_BG);
  formatAlt(f.alt, buf, sizeof(buf));
  tft.setCursor(x, y); tft.print(buf);
  tft.setTextSize(1); tft.setTextColor(C_AMBER, C_BG);
  snprintf(buf, sizeof(buf), "%d MPH", (int)(f.speed * 1.15078f));
  tft.setCursor(x + 80, y + 4); tft.print(buf);
  snprintf(buf, sizeof(buf), "%.1f MI", f.dist * 0.621371f);
  tft.setCursor(x + 160, y + 4); tft.print(buf);
  y += 24;
  // Phase
  uint16_t sCol = statusColor(f.status);
  tft.fillRect(x, y, 4, 12, sCol);
  tft.setTextSize(1); tft.setTextColor(sCol, C_BG);
  tft.setCursor(x + 8, y + 2); tft.print(statusLabel(f.status));
  if (isMilitary(f)) {   // MIL tag
    tft.fillRect(x + PANEL_W - 30, y, 30, 12, AL_MIL);
    tft.setTextColor(C_BG, AL_MIL);
    tft.setCursor(x + PANEL_W - 24, y + 2); tft.print("MIL");
  }
  y += 18;
  tft.setTextColor(C_DIMMER, C_BG);
  tft.setCursor(x, y); tft.print("Tap a plane to select it");
}

void renderRadar() {
  if (previousScreen != SCREEN_RADAR) drawHeader();
  previousScreen = SCREEN_RADAR;
  currentScreen  = SCREEN_RADAR;
  drawNavBar();
  tft.fillRect(0, CONTENT_Y, W, CONTENT_H, C_BG);
  drawSkyMap();        // outline map underneath everything
  drawRadarScope();
  drawRadarPlanes();
  drawRadarPanel();
  drawStatusBar();
}

// Tap on the radar: select the nearest plane. Tap the panel: next plane.
// Returns true if the tap was used.
bool radarHandleTouch(uint16_t tx, uint16_t ty) {
  if (flightCount == 0) return false;
  if (tx >= PANEL_X - 6) {
    flightIndex = (flightIndex + 1) % flightCount;
    return true;
  }
  int best = -1;
  long bestD2 = (long)TAP_RADIUS * TAP_RADIUS;
  for (int i = 0; i < flightCount; i++) {
    int px, py;
    if (!radarProject(flights[i].lat, flights[i].lon, px, py)) continue;
    long dx = (long)tx - px, dy = (long)ty - py;
    long d2 = dx * dx + dy * dy;
    if (d2 <= bestD2) { bestD2 = d2; best = i; }
  }
  if (best < 0) return false;
  flightIndex = best;
  return true;
}

#endif // HAS_TOUCH
