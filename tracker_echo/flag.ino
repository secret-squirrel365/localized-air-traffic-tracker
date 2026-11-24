// ─── Sky Dial: waving US flag for empty skies ──────────
// Shown on the radar side panel and the empty flight list when no planes
// are in range. The pole and gold ball are drawn once; the flag is redrawn
// column by column about 12 times a second.
#if HAS_TOUCH

#if SKYDIAL_FLAG
#define FLAG_AMP       6                              // wave height at the far end (px)
#define FLAG_SPR_H     (FLAG_H + 2 * FLAG_AMP + 2)
#define FLAG_FRAME_MS  80                             // about 12 frames a second
#define FLAG_POLE_COL  0x94B2                         // silver-grey
#define FLAG_BALL_COL  0xD566                         // gold

// Sky Dial v2: no sprite. Each column of the flag is written straight to the
// screen once per frame in its final colours (background above/below too),
// so nothing is ever wiped then redrawn - no flicker, and zero extra memory.
// (v1 used a 10 KB sprite, which left too little unbroken memory for the
// secure downloads and made the board fall back to CACHE.)
static uint16_t flagCol[3][3];                        // [red/white/blue][shade]
static bool  flagTried = false, flagOk = false;
static float flagPhase = 0;
static int   flagX = -1, flagY = -1;                  // flag's top-left on screen (-1 = hidden)

void flagPreload() {
  if (flagTried) return;
  flagTried = true;
  const uint8_t base[3][3] = { {178, 34, 52}, {235, 235, 235}, {40, 50, 120} };   // red, white, blue
  const float   shade[3]   = { 0.72f, 0.86f, 1.0f };                             // shadow → light
  for (int c = 0; c < 3; c++)
    for (int s = 0; s < 3; s++)
      flagCol[c][s] = tft.color565(base[c][0] * shade[s], base[c][1] * shade[s], base[c][2] * shade[s]);
  flagOk = true;
}

// Wave for one column: how far it moves up/down, and how bright it is (0-2)
static void flagColumn(int x, int& dy, int& sh) {
  const float k = 2.0f * PI / (FLAG_W * 0.75f);
  float ph  = x * k - flagPhase;
  float amp = FLAG_AMP * (0.15f + 0.85f * x / FLAG_W);   // pole side barely moves
  dy = (int)lroundf(amp * sinf(ph));
  float c = cosf(ph);
  sh = c > 0.33f ? 2 : (c > -0.33f ? 1 : 0);
}

static void drawFlagFrame() {
  const float stripe  = FLAG_H / 13.0f;
  const int   cantonW = (int)(FLAG_W * 0.4f);
  const int   cantonH = (int)(7 * stripe + 0.5f);
  const float gx = cantonW / 12.0f, gy = cantonH / 10.0f;
  const int   areaTop = flagY - FLAG_AMP - 1;         // top of the area the wave moves in
  tft.startWrite();
  for (int x = 0; x < FLAG_W; x++) {
    int dy, sh;
    flagColumn(x, dy, sh);
    int sx  = flagX + x;
    int top = flagY + dy;                             // flag's top edge in this column
    if (top > areaTop) tft.writeFastVLine(sx, areaTop, top - areaTop, C_BG);
    int first = 0;
    if (x < cantonW) {                                // blue corner, with stars cut in
      int c = (int)(x / gx + 0.5f);                   // nearest star column
      int starX = (int)(c * gx + 0.5f);
      bool starCol = (c >= 1 && c <= 11 && (x == starX || x == starX + 1));
      int y = 0;
      if (starCol) {
        for (int r = 1; r <= 9; r++) {
          if ((r + c) % 2) continue;
          int sy = (int)(r * gy + 0.5f);
          if (sy > y) tft.writeFastVLine(sx, top + y, sy - y, flagCol[2][sh]);
          tft.writeFastVLine(sx, top + sy, 2, TFT_WHITE);
          y = sy + 2;
        }
      }
      if (cantonH > y) tft.writeFastVLine(sx, top + y, cantonH - y, flagCol[2][sh]);
      first = 7;
    }
    for (int s = first; s < 13; s++) {                // stripes, red on top
      int y0 = (int)(s * stripe + 0.5f), y1 = (int)((s + 1) * stripe + 0.5f);
      tft.writeFastVLine(sx, top + y0, y1 - y0, flagCol[(s % 2 == 0) ? 0 : 1][sh]);
    }
    int bottom = top + FLAG_H, areaBot = areaTop + FLAG_SPR_H;
    if (bottom < areaBot) tft.writeFastVLine(sx, bottom, areaBot - bottom, C_BG);
  }
  tft.endWrite();
}

// Show the flag with its top-left corner at (x, y). Draws the pole and ball.
void flagShow(int x, int y) {
  if (!flagTried) flagPreload();
  flagX = x; flagY = y;
  tft.fillRect(x - 5, y - 10, 3, FLAG_H + 40, FLAG_POLE_COL);   // pole (stops above the small text)
  tft.fillCircle(x - 4, y - 13, 4, FLAG_BALL_COL);              // gold ball
  if (flagOk) drawFlagFrame();
}

// Called from loop(): keeps the flag waving while the sky is empty
void flagAnimate(unsigned long now) {
  static unsigned long lastFrame = 0;
  if (flagX < 0 || !flagOk) return;
  bool emptyScreen = (currentScreen == SCREEN_RADAR || currentScreen == SCREEN_LIST ||
                      currentScreen == SCREEN_FLIGHT) && flightCount == 0;
  if (!emptyScreen) { flagX = -1; return; }           // planes arrived or screen changed
  if (now - lastFrame < FLAG_FRAME_MS) return;
  lastFrame = now;
  flagPhase += 0.35f;
  if (flagPhase > 2.0f * PI) flagPhase -= 2.0f * PI;
  drawFlagFrame();
}

// Stop waving (another screen is about to draw over this spot)
void flagHide() { flagX = -1; }

// The "NO AIRCRAFT" page (flight-card mode with an empty sky), now with the flag
void renderNoAircraft() {
  flagHide();
  tft.fillScreen(C_BG);
  previousScreen = SCREEN_NONE;          // next screen does a full redraw
  currentScreen  = SCREEN_FLIGHT;
  drawHeader();
  drawNavBar();
  int fx = (W - FLAG_W) / 2 + 6, fy = CONTENT_Y + 40;
  flagShow(fx, fy);
  tft.setTextSize(2); tft.setTextColor(C_AMBER, C_BG);
  tft.setCursor((W - 20 * 12) / 2, fy + FLAG_H + 34); tft.print("NO AIRCRAFT IN RANGE");   // below the pole
  tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
  char msg[64];
  snprintf(msg, sizeof(msg), "Nothing within %d miles. Checking every 10 seconds.", SKYDIAL_RANGE_MI);
  tft.setCursor((W - (int)strlen(msg) * 6) / 2, fy + FLAG_H + 56); tft.print(msg);
  drawStatusBar();
}

#else
void flagHide() {}
void renderNoAircraft() { renderMessage("NO AIRCRAFT", "IN RANGE"); }
void flagPreload() {}
void flagShow(int, int) {}
void flagAnimate(unsigned long) {}
#endif // SKYDIAL_FLAG

#endif // HAS_TOUCH
