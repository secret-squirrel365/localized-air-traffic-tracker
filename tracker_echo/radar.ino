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
    uint16_t col = emerg ? C_RED : statusColor(f.status);
    bool sel = (i == flightIndex);
    drawPlaneIcon(px, py, f.track, col, sel);
    // Callsign label next to the plane
    tft.setTextSize(1);
    tft.setTextColor(sel ? C_YELLOW : C_DIM, C_BG);
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
