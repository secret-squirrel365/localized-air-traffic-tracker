// ─── Sky Dial: Radar screen ─────────────────────────────
// Radar scope on the left (outline map + planes + sweep) and a
// flight info panel on the right. Tap a plane to select it.
//
// The scope is drawn into an off-screen 16-colour "sprite" first and then
// copied to the screen in one go, so the spinning sweep doesn't flicker.
// If there isn't enough memory for the sprite, it draws straight to the
// screen instead (no sweep).
#if HAS_TOUCH

// Radar geometry (content area is y = CONTENT_Y .. H - FOOT_H)
#define RADAR_R    112
#define RADAR_CX   (RADAR_R + 8)
#define RADAR_CY   (CONTENT_Y + CONTENT_H / 2)
#define PANEL_X    (RADAR_CX + RADAR_R + 12)
#define PANEL_W    (W - PANEL_X - 6)
#define TAP_RADIUS 28          // big tap area for the stylus (pixels)

// Sprite that holds the scope (16 colours = 4 bits per pixel, ~26 KB)
#define SPR_PAD    3
#define SPR_SIZE   (RADAR_R * 2 + SPR_PAD * 2 + 1)
#define SPR_C      (RADAR_R + SPR_PAD)               // centre inside the sprite
#define SPR_X      (RADAR_CX - SPR_C)                // sprite position on screen
#define SPR_Y      (RADAR_CY - SPR_C)

#define SWEEP_FRAME_MS  50     // 20 frames per second
#define SWEEP_STEP_DEG  6      // one full turn every 3 seconds
#define SWEEP_TRAIL     30     // degrees of fading glow behind the line

// Radar colours (RGB565)
#define C_RING        0x0320   // dim green rings
#define C_RING_HI     0x05A0   // brighter green outer ring
#define C_SWEEP_MID   0x0500
#define C_MAP_HWY     0x6180   // dim orange  — highways
#define C_MAP_WATER   0x01F4   // dim blue    — rivers / lakes
#define C_MAP_COUNTY  0x39E7   // grey        — county lines + city outline
#define C_MAP_RWY     0xCE59   // light grey  — runways + airport labels

// The 16 colours the sprite can use. Any other colour is drawn as the
// closest one in this list.
static const uint16_t PAL[16] = {
  C_BG, C_RING, C_RING_HI, C_SWEEP_MID, C_GREEN, C_MAP_HWY, C_MAP_WATER, C_MAP_COUNTY,
  C_MAP_RWY, C_DIM, C_CYAN, C_AMBER, C_ORANGE, C_RED, C_YELLOW, AL_MIL
};

static LGFX_Sprite radarSpr(&tft);
static bool  sprTried = false, sprOk = false;
static float sweepDeg = 0;

static void initRadarSprite() {
  sprTried = true;
#if SKYDIAL_SWEEP
  radarSpr.setColorDepth(4);
  if (!radarSpr.createSprite(SPR_SIZE, SPR_SIZE)) {
    Serial.printf("[RADAR] No memory for sweep sprite (free %d) - sweep off\n", ESP.getFreeHeap());
    return;
  }
  radarSpr.createPalette();
  for (int i = 0; i < 16; i++) {
    uint16_t c = PAL[i];
    radarSpr.setPaletteColor(i, ((c >> 11) & 0x1F) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3);
  }
  sprOk = true;
  Serial.printf("[RADAR] Sweep sprite ready (%d bytes, free heap %d)\n",
                SPR_SIZE * SPR_SIZE / 2, ESP.getFreeHeap());
#endif
}

// Colour for the current drawing target: palette index for the sprite,
// the real colour for the screen.
static uint16_t pc(uint16_t c565) {
  if (!sprOk) return c565;
  int r = (c565 >> 11) & 0x1F, g = (c565 >> 5) & 0x3F, b = c565 & 0x1F;
  int best = 0; long bestD = 0x7FFFFFFFL;
  for (int i = 0; i < 16; i++) {
    int pr = (PAL[i] >> 11) & 0x1F, pg = (PAL[i] >> 5) & 0x3F, pb = PAL[i] & 0x1F;
    long d = (long)(r - pr) * (r - pr) * 4 + (long)(g - pg) * (g - pg) + (long)(b - pb) * (b - pb) * 4;
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

// Turn a plane's lat/lon into a spot on the screen. North is up.
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

// ─── Outline map (skymap.bin on the SD card) ────────────
// Made on a PC by tools/make_skymap.py. Kept in RAM (it's tiny).
#define MAP_MAX_BYTES 16384

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

// Map point -> offset from the radar centre (same math as planes)
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

static void drawSkyMap(LovyanGFX& g, int cx, int cy) {
  if (!mapTried) loadSkyMap();
  if (!mapBuf) return;
  const float r  = RADAR_R - 1;
  const float sc = (float)RADAR_R / GEOFENCE_KM;
  const float kx = 111.32f * cosf(HOME_LAT * DEG_TO_RAD) * sc;
  const float ky = 110.57f * sc;
  const uint16_t cHwy = pc(C_MAP_HWY), cWater = pc(C_MAP_WATER),
                 cGrey = pc(C_MAP_COUNTY), cRwy = pc(C_MAP_RWY);
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
        int px = cx + (int)x, py = cy + (int)y;
        g.fillRect(px - 1, py - 1, 3, 3, cRwy);
        int lw = L * 6;
        int edge = cx + (int)sqrtf(fmaxf(r * r - y * y, 0.0f));
        int lx = (px + 4 + lw > edge) ? px - 4 - lw : px + 4;   // keep label inside
        g.setTextSize(1);
        g.setTextColor(cRwy);                                   // see-through text
        g.setCursor(lx, py - 3);
        g.print(lab);
      }
      continue;
    }
    uint16_t col = type == 1 ? cHwy : type == 2 ? cWater : type == 5 ? cRwy : cGrey;
    float x0, y0;
    mapPoint(mapBuf + p, kx, ky, x0, y0);
    for (uint8_t k = 1; k < n; k++) {
      float x1, y1;
      mapPoint(mapBuf + p + 4 * k, kx, ky, x1, y1);
      float a0 = x0, b0 = y0, a1 = x1, b1 = y1;
      if (clipToCircle(a0, b0, a1, b1, r)) {
        int sx0 = cx + (int)a0, sy0 = cy + (int)b0;
        int sx1 = cx + (int)a1, sy1 = cy + (int)b1;
        g.drawLine(sx0, sy0, sx1, sy1, col);
        if (type == 5) g.drawLine(sx0 + 1, sy0, sx1 + 1, sy1, col);   // thicker runways
      }
      x0 = x1; y0 = y1;
    }
    p += 4 * (size_t)n;
  }
}

// ─── Sweep ───────────────────────────────────────────────
static void sweepPoint(int cx, int cy, float deg, int& x, int& y) {
  float t = deg * DEG_TO_RAD;
  x = cx + (int)(RADAR_R * sinf(t));
  y = cy - (int)(RADAR_R * cosf(t));
}

// Faint glowing wedge behind the line (drawn under the map)
static void drawSweepGlow(LovyanGFX& g, int cx, int cy) {
  const int steps = 6;
  for (int i = 0; i < steps; i++) {
    float a0 = sweepDeg - (float)SWEEP_TRAIL * i / steps;
    float a1 = sweepDeg - (float)SWEEP_TRAIL * (i + 1) / steps;
    int x0, y0, x1, y1;
    sweepPoint(cx, cy, a0, x0, y0);
    sweepPoint(cx, cy, a1, x1, y1);
    g.fillTriangle(cx, cy, x0, y0, x1, y1, pc(i < 2 ? C_SWEEP_MID : C_RING));
  }
}

// Bright leading line (drawn over the map)
static void drawSweepLine(LovyanGFX& g, int cx, int cy) {
  int x, y;
  sweepPoint(cx, cy, sweepDeg, x, y);
  g.drawLine(cx, cy, x, y, pc(C_GREEN));
  sweepPoint(cx, cy, sweepDeg - 1.5f, x, y);
  g.drawLine(cx, cy, x, y, pc(C_RING_HI));
}

// ─── Scope pieces ────────────────────────────────────────
static void drawRadarScope(LovyanGFX& g, int cx, int cy) {
  uint16_t ring = pc(C_RING);
  g.drawCircle(cx, cy, RADAR_R,         pc(C_RING_HI));
  g.drawCircle(cx, cy, RADAR_R * 2 / 3, ring);
  g.drawCircle(cx, cy, RADAR_R / 3,     ring);
  g.drawFastHLine(cx - RADAR_R, cy, RADAR_R * 2, ring);
  g.drawFastVLine(cx, cy - RADAR_R, RADAR_R * 2, ring);
  g.setTextSize(1);
  g.setTextColor(pc(C_GREEN));
  g.setCursor(cx - 2, cy - RADAR_R + 3);  g.print("N");
  g.setCursor(cx - 2, cy + RADAR_R - 10); g.print("S");
  g.setCursor(cx + RADAR_R - 9, cy - 3);  g.print("E");
  g.setCursor(cx - RADAR_R + 4, cy - 3);  g.print("W");
  char buf[12];
  snprintf(buf, sizeof(buf), "%.0f MI", GEOFENCE_KM * 0.621371f);
  g.setTextColor(pc(C_DIM));
  g.setCursor(cx + 4, cy - RADAR_R + 14);
  g.print(buf);
  g.fillCircle(cx, cy, 3, pc(C_CYAN));     // home
}

// Small arrow pointing the way the plane is flying
static void drawPlaneIcon(LovyanGFX& g, int px, int py, int track, uint16_t col, bool selected) {
  if (track >= 0) {
    float t = track * DEG_TO_RAD;
    float s = sinf(t), c = cosf(t);
    int tipX = px + (int)(8 * s),            tipY = py - (int)(8 * c);
    int lX   = px + (int)(-5 * c - 5 * s),   lY   = py + (int)(-5 * s + 5 * c);
    int rX   = px + (int)( 5 * c - 5 * s),   rY   = py + (int)( 5 * s + 5 * c);
    g.fillTriangle(tipX, tipY, lX, lY, rX, rY, col);
    g.drawLine(tipX, tipY, px + (int)(16 * s), py - (int)(16 * c), col);   // heading line
  } else {
    g.fillCircle(px, py, 3, col);
  }
  if (selected) g.drawCircle(px, py, 11, pc(C_YELLOW));
}

static void drawRadarPlanes(LovyanGFX& g, int cx, int cy) {
  for (int i = 0; i < flightCount; i++) {
    const Flight& f = flights[i];
    int sx, sy;
    if (!radarProject(f.lat, f.lon, sx, sy)) continue;
    int px = sx - RADAR_CX + cx, py = sy - RADAR_CY + cy;      // screen -> target coords
    bool emerg = strcmp(f.squawk,"7700")==0 || strcmp(f.squawk,"7600")==0 || strcmp(f.squawk,"7500")==0;
    bool mil = isMilitary(f);
    uint16_t col = emerg ? C_RED : (mil ? AL_MIL : statusColor(f.status));
    bool sel = (i == flightIndex);
    drawPlaneIcon(g, px, py, f.track, pc(col), sel);
    g.setTextSize(1);
    g.setTextColor(pc(sel ? C_YELLOW : (mil ? AL_MIL : C_DIM)));
    int lx = px + 9, ly = py + 6;
    int lw = (int)strlen(f.callsign) * 6;
    if (lx + lw > cx + RADAR_R) lx = px - 9 - lw;            // keep label inside the scope
    g.setCursor(lx, ly);
    g.print(f.callsign[0] ? f.callsign : "?");
  }
}

// Draw the whole scope once. Uses the sprite (with sweep) if available.
static void drawScopeFrame() {
  if (!sprTried) initRadarSprite();
  if (sprOk) {
    radarSpr.fillSprite(0);                 // palette 0 = background
    drawSweepGlow(radarSpr, SPR_C, SPR_C);
    drawSkyMap(radarSpr, SPR_C, SPR_C);
    drawRadarScope(radarSpr, SPR_C, SPR_C);
    drawSweepLine(radarSpr, SPR_C, SPR_C);
    drawRadarPlanes(radarSpr, SPR_C, SPR_C);
    radarSpr.pushSprite(SPR_X, SPR_Y);
  } else {
    tft.fillRect(SPR_X, SPR_Y, SPR_SIZE, SPR_SIZE, C_BG);
    drawSkyMap(tft, RADAR_CX, RADAR_CY);
    drawRadarScope(tft, RADAR_CX, RADAR_CY);
    drawRadarPlanes(tft, RADAR_CX, RADAR_CY);
  }
}

// Load the map and grab the sweep memory at start-up, before downloads begin
// (big memory blocks are easier to get early, and the SD card is free).
void radarPreload() {
  if (!mapTried) loadSkyMap();
  if (!sprTried) initRadarSprite();
}

// Called from loop(): spins the sweep while the radar is on screen.
void radarAnimate(unsigned long now) {
#if SKYDIAL_SWEEP
  static unsigned long lastFrame = 0;
  if (currentScreen != SCREEN_RADAR || !sprOk) return;   // keeps spinning during downloads
  if (now - lastFrame < SWEEP_FRAME_MS) return;
  lastFrame = now;
  sweepDeg += SWEEP_STEP_DEG;
  if (sweepDeg >= 360) sweepDeg -= 360;
  drawScopeFrame();
#else
  (void)now;
#endif
}

// ─── Side panel ──────────────────────────────────────────
// Sky Dial: no-flicker panel. Every line sits in a fixed spot, text is
// printed with its own background, and only the leftover space to the right
// of the new text is cleared. Nothing on screen is ever wiped then redrawn.
static bool panelShowsEmpty = true;

// Clear from the text cursor to x2 on a row of height h
static void clrTo(int x2, int y, int h) {
  int cx = tft.getCursorX();
  if (cx < x2) tft.fillRect(cx, y, x2 - cx, h, C_BG);
}

// Print text in exactly 2 size-1 lines (second line blanked if not needed)
static void panelPrint2(const char* s, int x, int& y, uint16_t col) {
  const int maxChars = PANEL_W / 6;
  char line[48];
  tft.setTextSize(1);
  tft.setTextColor(col, C_BG);
  int len = strlen(s), start = 0;
  for (int n = 0; n < 2; n++) {
    int take = max(0, min(maxChars, len - start));
    strlcpy(line, s + start, min((int)sizeof(line), take + 1));
    tft.setCursor(x, y); tft.print(line);
    clrTo(x + PANEL_W, y, 8);
    y += 10; start += take;
  }
}

// Route in big text like the flight card: "From" city on one line,
// "> To" city on the next (the server sends "San Antonio > Dallas").
static void drawRadarRoute(const Flight& f, int x, int& y) {
  const int maxChars = PANEL_W / 12;               // size-2 letters are 12 px wide
  char from[24] = "", to[24] = "";
  if (f.route[0]) {
    const char* sep = strstr(f.route, " > ");
    if (sep) {
      int n = min((int)(sep - f.route), maxChars);
      strlcpy(from, f.route, n + 1);
      snprintf(to, sizeof(to), "> %s", sep + 3);
    } else {
      strlcpy(from, f.route, maxChars + 1);
    }
    to[min((int)strlen(to), maxChars)] = 0;
  }
  tft.setTextSize(2);
  tft.setTextColor(f.route[0] ? C_YELLOW : C_DIMMER, C_BG);
  tft.setCursor(x, y); tft.print(f.route[0] ? from : "NO ROUTE");
  clrTo(x + PANEL_W, y, 16);
  y += 18;
  tft.setCursor(x, y); tft.print(to);
  clrTo(x + PANEL_W, y, 16);
  y += 18;
}

static void drawRadarPanel(bool full) {
  const int x = PANEL_X, x2 = PANEL_X + PANEL_W;
  int y = CONTENT_Y + 6;
  bool empty = (flightCount == 0);
  if (full || empty != panelShowsEmpty) {          // only when the layout changes
    tft.fillRect(PANEL_X - 2, CONTENT_Y, W - (PANEL_X - 2), CONTENT_H, C_BG);
    full = true;
  }
  panelShowsEmpty = empty;
  tft.drawFastVLine(PANEL_X - 6, CONTENT_Y + 4, CONTENT_H - 8, C_DIMMER);
  if (empty) {
    if (!full) return;                              // already showing
    tft.setTextSize(2); tft.setTextColor(C_GREEN, C_BG);
    tft.setCursor(x, y + 40); tft.print("CLEAR SKIES");
    tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(x, y + 64); tft.print("No planes in range.");
    tft.setCursor(x, y + 76); tft.print("Tap GEO for more range.");
    return;
  }
  const Flight& f = flights[flightIndex];
  tft.setTextSize(3); tft.setTextColor(C_AMBER, C_BG);
  tft.setCursor(x, y); tft.print(f.callsign[0] ? f.callsign : "UNKNOWN");
  clrTo(x2, y, 24);
  y += 26;
  const Airline* al = getAirline(f.callsign);
  panelPrint2(al ? al->name : "UNKNOWN AIRLINE", x, y, al ? al->color : C_DIM);
  y += 2;
  tft.drawFastHLine(x, y, PANEL_W, C_DIMMER); y += 6;
  panelPrint2(getAircraftTypeName(f.type), x, y, C_CYAN);
  drawRadarRoute(f, x, y);
  y += 2;
  tft.drawFastHLine(x, y, PANEL_W, C_DIMMER); y += 6;
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
  clrTo(x + 78, y, 16);
  tft.setTextSize(1); tft.setTextColor(C_AMBER, C_BG);
  snprintf(buf, sizeof(buf), "%d MPH", (int)(f.speed * 1.15078f));
  tft.setCursor(x + 80, y + 4); tft.print(buf);
  clrTo(x + 158, y + 4, 8);
  snprintf(buf, sizeof(buf), "%.1f MI", f.dist * 0.621371f);
  tft.setCursor(x + 160, y + 4); tft.print(buf);
  clrTo(x2, y + 4, 8);
  y += 24;
  uint16_t sCol = statusColor(f.status);
  tft.fillRect(x, y, 4, 12, sCol);
  tft.setTextSize(1); tft.setTextColor(sCol, C_BG);
  tft.setCursor(x + 8, y + 2); tft.print(statusLabel(f.status));
  clrTo(x2 - 32, y + 2, 8);
  if (isMilitary(f)) {
    tft.fillRect(x + PANEL_W - 30, y, 30, 12, AL_MIL);
    tft.setTextColor(C_BG, AL_MIL);
    tft.setCursor(x + PANEL_W - 24, y + 2); tft.print("MIL");
  } else {
    tft.fillRect(x + PANEL_W - 30, y, 30, 12, C_BG);
  }
  y += 18;
  if (full) {
    tft.setTextColor(C_DIMMER, C_BG);
    tft.setCursor(x, y); tft.print("Tap a plane to select it");
  }
}

// Update just the "< 3/7 >" counter in the nav bar (no button redraw)
static void drawRadarCounter() {
  int x2 = LST_BTN_X1 - 2;
  if (flightCount <= 1) { tft.fillRect(0, NAV_Y + 2, x2, NAV_H - 2, C_BG); return; }
  char navBuf[16];
  snprintf(navBuf, sizeof(navBuf), "< %d/%d >", flightIndex + 1, flightCount);
  bool big = 8 + (int)strlen(navBuf) * 12 < LST_BTN_X1 - 4;
  int ty = NAV_Y + (big ? 10 : 14);
  tft.setTextSize(big ? 2 : 1);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(8, ty);
  tft.print(navBuf);
  clrTo(x2, ty, big ? 16 : 8);
}

// Sky Dial: full redraw only when arriving from another screen.
// After that (new planes, next plane, taps) only the changing parts update.
void renderRadar() {
  bool full = (previousScreen != SCREEN_RADAR);
  previousScreen = SCREEN_RADAR;
  currentScreen  = SCREEN_RADAR;
  if (full) {
    drawHeader();
    drawNavBar();
    tft.fillRect(0, CONTENT_Y, W, CONTENT_H, C_BG);
  }
  drawScopeFrame();
  drawRadarPanel(full);
  if (full) drawStatusBar();
  else      drawRadarCounter();
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
