#pragma once
#include <Arduino.h>
#include <Adafruit_SSD1306.h>

// ── Sprites ────────────────────────────────────────────────────────────
static const uint8_t SI_SPR[2][8] PROGMEM = {
  { 0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x42, 0x81 },  // legs out
  { 0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x81, 0x42 },  // legs in
};
static const uint8_t SI_EXPSPR[3][8] PROGMEM = {
  { 0x00, 0x42, 0x3C, 0x5A, 0x5A, 0x3C, 0x42, 0x00 },
  { 0x81, 0x24, 0x18, 0x00, 0x00, 0x18, 0x24, 0x81 },
  { 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42 },
};

// ── Layout ─────────────────────────────────────────────────────────────
static const int SI_ROWS = 2;
static const int SI_COLS = 5;
static const int SI_CW   = 12;   // column width
static const int SI_RH   = 10;   // row height
static const int SI_OX   = 18;   // formation left edge
static const int SI_OY   = 8;    // formation top
static const int SI_CB   = 52;   // cannon body top y
static const int SI_TOTAL  = SI_ROWS * SI_COLS;  // 10
#define SI_FRAMES 200  // safety timeout (real exit via return true)

// Scripted kill order: bottom L→R, top R→L
static const uint8_t SI_KILL[SI_TOTAL][2] PROGMEM = {
  {1,0},{1,1},{1,2},{1,3},{1,4},
  {0,4},{0,3},{0,2},{0,1},{0,0},
};

// ── Game state ─────────────────────────────────────────────────────────
static bool    siAlive[SI_ROWS][SI_COLS];
static int16_t siCx;           // cannon barrel center x
static int16_t siBx, siBy;    // bullet x, y
static bool    siBullet;
static int16_t siExpX, siExpY;
static uint8_t siExpT;         // explosion frame counter
static uint8_t siKill;         // current kill index
static uint8_t siVictory;      // victory frame counter
static int8_t  siFX;           // formation x offset (oscillates)
static int8_t  siFDir;
static uint8_t siFCnt;

enum SISt : uint8_t { SI_AIM, SI_FLY, SI_EXP, SI_WIN };
static SISt siSt;

// ── Helpers ────────────────────────────────────────────────────────────
static void siDraw8(Adafruit_SSD1306& d, const uint8_t* spr, int x, int y) {
  for (int r = 0; r < 8; r++) {
    uint8_t b = pgm_read_byte(spr + r);
    for (int c = 0; c < 8; c++)
      if (b & (0x80 >> c)) d.drawPixel(x + c, y + r, WHITE);
  }
}
static inline int siEX(int col) { return SI_OX + siFX + col * SI_CW; }
static inline int siEY(int row) { return SI_OY + row * SI_RH; }

// ── Main function — returns true when animation is complete ────────────
bool drawSpaceInvaders(Adafruit_SSD1306& d, int f) {

  // Reset all state on first frame
  if (f == 0) {
    for (int r = 0; r < SI_ROWS; r++)
      for (int c = 0; c < SI_COLS; c++)
        siAlive[r][c] = true;
    siCx = 64; siBullet = false; siKill = 0;
    siExpT = 0; siVictory = 0;
    siFX = 0; siFDir = 1; siFCnt = 0;
    siSt = SI_AIM;
  }

  // Formation oscillates left-right
  if (++siFCnt >= 3) {
    siFCnt = 0;
    siFX += siFDir;
    if (siFX >= 16 || siFX <= 0) siFDir = -siFDir;
  }

  // ── State machine ──────────────────────────────────────────────────

  switch (siSt) {

    case SI_AIM: {
      // Advance past dead enemies
      while (siKill < SI_TOTAL &&
             !siAlive[pgm_read_byte(&SI_KILL[siKill][0])]
                      [pgm_read_byte(&SI_KILL[siKill][1])])
        siKill++;

      if (siKill >= SI_TOTAL) { siSt = SI_WIN; break; }

      uint8_t tc = pgm_read_byte(&SI_KILL[siKill][1]);
      int tx = siEX(tc) + 4;             // target center x
      int diff = tx - siCx;
      if (abs(diff) <= 3) {
        // Aligned — fire
        siBx = siCx; siBy = SI_CB - 5;
        siBullet = true; siSt = SI_FLY;
      } else {
        siCx += constrain(diff, -14, 14);  // move toward target
      }
      break;
    }

    case SI_FLY: {
      siBy -= 18;                        // bullet flies up
      uint8_t tr = pgm_read_byte(&SI_KILL[siKill][0]);
      uint8_t tc = pgm_read_byte(&SI_KILL[siKill][1]);
      int ex = siEX(tc), ey = siEY(tr);

      // Hit detection — vertical window covers full bullet step to prevent skipping
      if (siBy <= ey + 8 && siBy >= ey - 20 &&
          abs(siBx - (ex + 4)) <= 8) {
        siAlive[tr][tc] = false;
        siBullet = false;
        siExpX = ex; siExpY = ey; siExpT = 0;
        siKill++; siSt = SI_EXP;
      } else if (siBy < 0) {
        // Bullet missed (safety fallback)
        siBullet = false; siSt = SI_AIM;
      }
      break;
    }

    case SI_EXP:
      if (++siExpT >= 6) siSt = SI_AIM;  // explosion lasts 6 frames
      break;

    case SI_WIN:
      if (++siVictory >= 25) return true; // animation complete
      break;
  }

  // ── Draw ──────────────────────────────────────────────────────────

  uint8_t anim = (f / 10) & 1;

  // Invaders
  for (int r = 0; r < SI_ROWS; r++)
    for (int c = 0; c < SI_COLS; c++)
      if (siAlive[r][c])
        siDraw8(d, SI_SPR[anim], siEX(c), siEY(r));

  // Explosion
  if (siSt == SI_EXP)
    siDraw8(d, SI_EXPSPR[min((int)(siExpT / 3), 2)], siExpX, siExpY);

  // Bullet
  if (siBullet) d.drawFastVLine(siBx, siBy, 5, WHITE);

  // Ground line
  d.drawFastHLine(0, 62, 128, WHITE);

  // Cannon
  d.drawFastVLine(siCx, SI_CB - 4, 4, WHITE);           // barrel
  d.fillRect(siCx - 4, SI_CB,     9, 3, WHITE);         // body
  d.fillRect(siCx - 6, SI_CB + 3, 13, 2, WHITE);        // base

  // Kill indicator dots (top-left corner)
  for (int i = 0; i < siKill; i++)
    d.drawPixel(1 + i * 3, 1, WHITE);

  // Victory flash
  if (siSt == SI_WIN && (siVictory / 5) % 2 == 0) {
    d.setTextSize(1);
    d.setTextColor(WHITE);
    d.setCursor(22, 26);
    d.print(F("STAGE  CLEAR!"));
  }

  return false;
}
