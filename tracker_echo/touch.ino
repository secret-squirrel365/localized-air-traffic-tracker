// ─── Touch calibration & input handling ────────────────
#if HAS_TOUCH

bool loadTouchCal() {
  Preferences p;
  p.begin("tracker", true);
  if (!p.isKey("tcal2")) { p.end(); return false; }
  p.getBytes("tcal2", touchCalData, sizeof(touchCalData));
  p.end();
  return true;
}

void saveTouchCal() {
  Preferences p;
  p.begin("tracker", false);
  p.putBytes("tcal2", touchCalData, sizeof(touchCalData));
  p.end();
}

// Intro screen + the corner-cross taps (LovyanGFX), then save
static void doTouchCalibration() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, W, HDR_H, C_AMBER);
  tft.setTextColor(C_BG, C_AMBER);
  tft.setTextSize(2);
  tft.setCursor(8, 6);
  tft.print("OVERHEAD TRACKER");
  tft.setTextColor(C_AMBER, C_BG);
  tft.setTextSize(2);
  tft.setCursor(16, H/2 - 24);
  tft.print("TOUCH CALIBRATION");
  tft.setTextColor(C_DIM, C_BG);
  tft.setTextSize(1);
  tft.setCursor(16, H/2 + 4);
  tft.print("Tap each corner cross when it appears");
  delay(1200);
  tft.calibrateTouch(touchCalData, C_AMBER, C_BG, 15);
  saveTouchCal();
  tft.setTouchCalibrate(touchCalData);
  touchReady = true;
  Serial.println("Touch calibrated and saved.");
}

void initTouch() {
  if (loadTouchCal()) {
    tft.setTouchCalibrate(touchCalData);
    touchReady = true;
    Serial.println("Touch cal loaded.");
    return;
  }
  doTouchCalibration();   // first boot
}

// ─── Sky Dial: touch accuracy test ─────────────────────
// Five + targets. Tap each one: a green dot shows where the board felt the
// tap and the bottom line says how far off it was. Tap DONE (or wait 2 min).
static void drawTestTarget(int x, int y) {
  tft.drawFastHLine(x - 10, y, 21, C_AMBER);
  tft.drawFastVLine(x, y - 10, 21, C_AMBER);
  tft.drawCircle(x, y, 6, C_DIM);
}

static void runTouchTest() {
  const int tx[5] = { 40, W - 40, W / 2, 40,     W - 40 };
  const int ty[5] = { 80, 80,     H / 2, H - 60, H - 60 };
  const int doneX = W / 2 - 50, doneY = H - 40, doneW = 100, doneH = 32;
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, W, HDR_H, C_AMBER);
  tft.setTextColor(C_BG, C_AMBER); tft.setTextSize(2);
  tft.setCursor(8, 6); tft.print("TOUCH TEST");
  tft.setTextColor(C_DIM, C_BG); tft.setTextSize(1);
  tft.setCursor(90, HDR_H + 8);  tft.print("Tap the center of each +. Green dot = where it landed.");
  for (int i = 0; i < 5; i++) drawTestTarget(tx[i], ty[i]);
  tft.fillRect(doneX, doneY, doneW, doneH, C_GREEN);
  tft.setTextColor(C_BG, C_GREEN); tft.setTextSize(2);
  tft.setCursor(doneX + (doneW - 48) / 2, doneY + 9); tft.print("DONE");

  unsigned long last = millis();
  bool wasDown = true;                                // ignore a press still held from calibration
  while (millis() - last < 120000UL) {
    uint16_t x, y;
    bool down = tft.getTouch(&x, &y);
    if (down && !wasDown) {
      last = millis();
      if (x >= doneX && x < doneX + doneW && y >= doneY && y < doneY + doneH) break;
      int best = 0; long bestD = 0x7FFFFFFFL;
      for (int i = 0; i < 5; i++) {
        long dx = (long)x - tx[i], dy = (long)y - ty[i], d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = i; }
      }
      int off = (int)sqrtf((float)bestD);
      tft.fillCircle(x, y, 3, off <= 8 ? C_GREEN : (off <= 16 ? C_YELLOW : C_RED));
      char msg[64];
      snprintf(msg, sizeof(msg), "Off by %d px  %s", off,
               off <= 8 ? "(great)" : off <= 16 ? "(ok)" : "(re-run calibrate)");
      tft.fillRect(80, doneY - 14, W - 160, 10, C_BG);     // clear old text, not the corner targets
      tft.setTextColor(C_DIM, C_BG); tft.setTextSize(1);
      tft.setCursor((W - (int)strlen(msg) * 6) / 2, doneY - 14); tft.print(msg);
      Serial.printf("[TOUCH TEST] tap %d,%d  target %d  off %d px\n", x, y, best + 1, off);
    }
    wasDown = down;
    delay(15);
  }
}

// Put the normal screen back after calibrating / testing
static void redrawAfterTouchTools() {
  previousScreen = SCREEN_NONE;
  tft.fillScreen(C_BG);
  if (currentScreen == SCREEN_LIST)           renderList();
  else if (currentScreen == SCREEN_WEATHER)   renderWeather();
  else if (currentScreen == SCREEN_RADAR)     renderRadar();
  else if (flightCount > 0)                   renderFlight(flights[flightIndex]);
  else                                        renderNoAircraft();
}

// Started from the PC ("calibrate" or "touchtest" serial command).
// These wait for taps, so the watchdog is paused for this task meanwhile.
void startTouchCalibration(bool calibrate) {
  esp_task_wdt_delete(NULL);
  flagHide();
  if (calibrate) doTouchCalibration();
  runTouchTest();
  esp_task_wdt_add(NULL);
  redrawAfterTouchTools();
}

void handleTouch(uint16_t tx, uint16_t ty) {
  uint32_t now = millis();
  if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
  lastTouchMs = now;

  // ── Nav bar buttons ──
  if (ty >= NAV_Y && ty < NAV_Y + NAV_H) {

    // LST button (Sky Dial): open / close the flight list
    if (tx >= LST_BTN_X1 && tx < LST_BTN_X1 + NAV_BTN_W) {
      if (trackingMode) return;
      if (currentScreen == SCREEN_LIST) {
        if (radarPreferred) { renderRadar(); return; }
        currentScreen = SCREEN_FLIGHT;
        if (flightCount > 0) renderFlight(flights[flightIndex]);
        else renderNoAircraft();   // Sky Dial: with the waving flag
      } else {
        renderList();
      }
      return;
    }

    // RDR button (Sky Dial): switch between radar and flight cards
    if (tx >= RDR_BTN_X1 && tx < RDR_BTN_X1 + NAV_BTN_W) {
      if (trackingMode) return;
      if (currentScreen == SCREEN_RADAR) {
        radarPreferred = false;
        currentScreen = SCREEN_FLIGHT;
        if (flightCount > 0) renderFlight(flights[flightIndex]);
        else renderNoAircraft();   // Sky Dial: with the waving flag
      } else {
        radarPreferred = true;
        lastCycle = millis();
        renderRadar();
      }
      return;
    }

    // WX button
    if (tx >= WX_BTN_X1 && tx < WX_BTN_X1 + NAV_BTN_W) {
      tft.fillRect(WX_BTN_X1, NAV_Y + 2, NAV_BTN_W, NAV_BTN_H, C_AMBER);
      tft.setTextColor(C_BG, C_AMBER);
      tft.setTextSize(2);
      tft.setCursor(WX_BTN_X1 + (NAV_BTN_W - 24) / 2, NAV_Y + 10);
      tft.print("WX");
      // Sky Dial: removed 100 ms pause for snappier buttons
      if (currentScreen == SCREEN_WEATHER) {
        if (radarPreferred && !trackingMode) { renderRadar(); return; }   // Sky Dial
        currentScreen = SCREEN_FLIGHT;
        if (flightCount > 0) renderFlight(flights[flightIndex]);
        else renderNoAircraft();   // Sky Dial: with the waving flag
      } else {
        currentScreen = SCREEN_WEATHER;
        renderWeather();
      }
      return;
    }

#if SKYDIAL_GEO_BTN
    // GEO button (disabled in tracking mode)
    if (tx >= GEO_BTN_X1 && tx < GEO_BTN_X1 + NAV_BTN_W) {
      if (trackingMode) return;
      tft.fillRect(GEO_BTN_X1, NAV_Y + 2, NAV_BTN_W, NAV_BTN_H, C_AMBER);
      tft.setTextColor(C_BG, C_AMBER);
      tft.setTextSize(2);
      tft.setCursor(GEO_BTN_X1 + 8, NAV_Y + 10);
      tft.printf("%dmi", (int)(GEOFENCE_KM * 0.621371f + 0.5f));
      // Sky Dial: removed 100 ms pause for snappier buttons
      geoIndex = (geoIndex + 1) % GEO_COUNT;
      GEOFENCE_KM = GEO_PRESETS[geoIndex];
      saveGeoIndex();
      Serial.printf("Geofence: %.0f mi\n", GEOFENCE_KM * 0.621371f);
      drawNavBar();
      if (!isFetching) {
        flightCount = 0;
        flightIndex = 0;
        requestFlights();   // Sky Dial: background download
        countdown = REFRESH_SECS;
        lastCycle  = millis();
      }
      return;
    }
#endif

    // CFG button (two-tap confirmation — reboot is destructive)
    if (tx >= CFG_BTN_X1 && tx < CFG_BTN_X1 + NAV_BTN_W) {
      if (isFetching) return;
      tft.fillRect(CFG_BTN_X1, NAV_Y + 2, NAV_BTN_W, NAV_BTN_H, C_RED);
      tft.setTextColor(C_BG, C_RED);
      tft.setTextSize(1);
      tft.setCursor(CFG_BTN_X1 + 5, NAV_Y + 6);
      tft.print("REBOOT?");
      tft.setCursor(CFG_BTN_X1 + 2, NAV_Y + 20);
      tft.print("TAP AGAIN");
      uint32_t confirmDeadline = millis() + 3000;
      bool confirmed = false;
      while (millis() < confirmDeadline) {
        uint16_t cx, cy;
        if (tft.getTouch(&cx, &cy)) {
          if (cx >= CFG_BTN_X1 && cy >= NAV_Y && cy < NAV_Y + NAV_H) {
            confirmed = true;
            break;
          } else {
            break;
          }
        }
        delay(30);
      }
      if (confirmed) {
        startCaptivePortal();
      } else {
        drawNavBar();
      }
      return;
    }
    return;
  }

  // ── Sky Dial: taps on the flight list ──
  if (ty >= CONTENT_Y && ty < (H - FOOT_H) && currentScreen == SCREEN_LIST) {
    int r = listHandleTouch(tx, ty);
    if (r == 1) renderList();
    else if (r == 2) { radarPreferred = true; lastCycle = millis(); renderRadar(); }
    return;
  }

  // ── Sky Dial: taps on the radar screen ──
  if (ty >= CONTENT_Y && ty < (H - FOOT_H) && currentScreen == SCREEN_RADAR) {
    if (radarHandleTouch(tx, ty)) { lastCycle = millis(); renderRadar(); }
    return;
  }

  // ── Content area: tap left/right to cycle flights (suppressed in tracking mode) ──
  if (ty >= CONTENT_Y && ty < (H - FOOT_H) && !trackingMode &&
      currentScreen == SCREEN_FLIGHT && flightCount > 1) {
    if (tx < W / 2) {
      flightIndex = (flightIndex - 1 + flightCount) % flightCount;
    } else {
      flightIndex = (flightIndex + 1) % flightCount;
    }
    lastCycle = millis();
    renderFlight(flights[flightIndex]);
    return;
  }
}

#endif // HAS_TOUCH
