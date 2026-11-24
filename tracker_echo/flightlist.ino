// ─── Sky Dial: Flight list screen ───────────────────────
// Every plane in range, closest first. ▲/▼ to scroll, tap a row
// to jump to that plane on the radar.
#if HAS_TOUCH

#define LIST_ROW_H    26
#define LIST_ROWS     (CONTENT_H / LIST_ROW_H)        // rows that fit (9)
#define LIST_SCROLL_W 44                              // ▲/▼ column on the right
#define LIST_SCROLL_X (W - LIST_SCROLL_W)

static int listTop = 0;   // first row shown

static void drawListArrow(int y, int h, bool up, bool enabled) {
  uint16_t bg = enabled ? C_DIMMER : C_BG;
  uint16_t fg = enabled ? C_AMBER  : C_DIMMER;
  tft.fillRect(LIST_SCROLL_X + 2, y + 2, LIST_SCROLL_W - 4, h - 4, bg);
  int cx = LIST_SCROLL_X + LIST_SCROLL_W / 2, cy = y + h / 2;
  if (up) tft.fillTriangle(cx, cy - 8, cx - 10, cy + 6, cx + 10, cy + 6, fg);
  else    tft.fillTriangle(cx, cy + 8, cx - 10, cy - 6, cx + 10, cy - 6, fg);
}

static void drawListRow(int idx, int y) {
  const Flight& f = flights[idx];
  bool sel  = (idx == flightIndex);
  bool mil  = isMilitary(f);
  bool emerg = strcmp(f.squawk,"7700")==0 || strcmp(f.squawk,"7600")==0 || strcmp(f.squawk,"7500")==0;
  uint16_t sCol = emerg ? C_RED : statusColor(f.status);
  char buf[24];

  if (sel) tft.fillRect(0, y, LIST_SCROLL_X, LIST_ROW_H - 1, 0x2104);   // highlight
  uint16_t bg = sel ? 0x2104 : C_BG;
  tft.fillRect(2, y + 3, 4, LIST_ROW_H - 7, sCol);                     // phase colour bar

  // Callsign
  tft.setTextSize(2);
  tft.setTextColor(emerg ? C_RED : C_AMBER, bg);
  tft.setCursor(12, y + 6);
  strlcpy(buf, f.callsign[0] ? f.callsign : "?", 8);
  tft.print(buf);

  // Airline (top) + aircraft type (bottom)
  const Airline* al = getAirline(f.callsign);
  tft.setTextSize(1);
  tft.setTextColor(mil ? AL_MIL : (al ? al->color : C_DIM), bg);
  tft.setCursor(104, y + 3);
  strlcpy(buf, al ? al->name : (mil ? "MILITARY" : "---"), 19);
  tft.print(buf);
  tft.setTextColor(C_CYAN, bg);
  tft.setCursor(104, y + 14);
  strlcpy(buf, getAircraftTypeName(f.type), 19);
  tft.print(buf);

  // Altitude (top) + speed (bottom)
  tft.setTextColor(C_AMBER, bg);
  formatAlt(f.alt, buf, sizeof(buf));
  tft.setCursor(222, y + 3); tft.print(buf);
  tft.setTextColor(C_DIM, bg);
  snprintf(buf, sizeof(buf), "%d MPH", (int)(f.speed * 1.15078f));
  tft.setCursor(222, y + 14); tft.print(buf);

  // Distance (top) + phase (bottom)
  tft.setTextColor(C_AMBER, bg);
  snprintf(buf, sizeof(buf), "%.1f MI", f.dist * 0.621371f);
  tft.setCursor(296, y + 3); tft.print(buf);
  tft.setTextColor(sCol, bg);
  tft.setCursor(296, y + 14); tft.print(statusLabel(f.status));

  // MIL / emergency tag
  if (emerg || mil) {
    uint16_t tc = emerg ? C_RED : AL_MIL;
    tft.fillRect(LIST_SCROLL_X - 34, y + 7, 30, 12, tc);
    tft.setTextColor(C_BG, tc);
    tft.setCursor(LIST_SCROLL_X - 28, y + 9);
    tft.print(emerg ? "SOS" : "MIL");
  }
  tft.drawFastHLine(0, y + LIST_ROW_H - 1, LIST_SCROLL_X, C_DIMMER);
}

void renderList() {
  if (previousScreen != SCREEN_LIST) drawHeader();
  previousScreen = SCREEN_LIST;
  currentScreen  = SCREEN_LIST;
  drawNavBar();
  tft.fillRect(0, CONTENT_Y, W, CONTENT_H, C_BG);

  if (flightCount == 0) {
    listTop = 0;
    tft.setTextSize(2); tft.setTextColor(C_GREEN, C_BG);
    tft.setCursor(16, CONTENT_Y + 60); tft.print("CLEAR SKIES");
    tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(16, CONTENT_Y + 86); tft.print("No planes in range. Tap GEO for more range.");
    drawStatusBar();
    return;
  }
  int maxTop = flightCount > LIST_ROWS ? flightCount - LIST_ROWS : 0;
  if (listTop > maxTop) listTop = maxTop;
  if (listTop < 0) listTop = 0;

  for (int r = 0; r < LIST_ROWS && listTop + r < flightCount; r++)
    drawListRow(listTop + r, CONTENT_Y + r * LIST_ROW_H);

  // Scroll column: ▲ on top half, ▼ on bottom half, position text in between
  tft.drawFastVLine(LIST_SCROLL_X, CONTENT_Y, CONTENT_H, C_DIMMER);
  int half = CONTENT_H / 2;
  drawListArrow(CONTENT_Y, half - 10, true,  listTop > 0);
  drawListArrow(CONTENT_Y + half + 10, half - 10, false, listTop < maxTop);
  char pos[12];
  int last = min(listTop + LIST_ROWS, flightCount);
  snprintf(pos, sizeof(pos), "%d-%d", listTop + 1, last);
  tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(LIST_SCROLL_X + 4, CONTENT_Y + half - 4);
  tft.print(pos);
  drawStatusBar();
}

// Returns: 0 = not used, 1 = list changed (redraw list), 2 = plane picked (show radar)
int listHandleTouch(uint16_t tx, uint16_t ty) {
  if (flightCount == 0) return 0;
  int maxTop = flightCount > LIST_ROWS ? flightCount - LIST_ROWS : 0;
  if (tx >= LIST_SCROLL_X) {
    int half = CONTENT_H / 2;
    if (ty < CONTENT_Y + half) { if (listTop > 0) { listTop -= LIST_ROWS - 1; if (listTop < 0) listTop = 0; return 1; } }
    else                       { if (listTop < maxTop) { listTop += LIST_ROWS - 1; if (listTop > maxTop) listTop = maxTop; return 1; } }
    return 0;
  }
  int row = (ty - CONTENT_Y) / LIST_ROW_H;
  int idx = listTop + row;
  if (row < 0 || row >= LIST_ROWS || idx >= flightCount) return 0;
  flightIndex = idx;
  return 2;
}

#endif // HAS_TOUCH
