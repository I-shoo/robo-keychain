#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "TextHello.h"
#include "eyes_anim_main.h"
// #include "eyes_anim.h"  // disabled
#include "sleepy.h"
#include "fly.h"
#include "space_invaders.h"
#include "worried.h"
#include "susp.h"
#include "dance.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define MIC_DO_PIN D5

const unsigned long MIN_DANCE_MS    = 3000;
const unsigned long SILENCE_EXIT_MS = 5000;
const unsigned long FRAME_MS        = 50;
const unsigned long REPORT_MS       = 50;

// One-shot idle animation pool
// bitmap: set frames+len, leave draw=nullptr
// procedural: set draw+len, leave frames=nullptr
struct OneShot {
  const unsigned char** frames;
  bool (*draw)(Adafruit_SSD1306&, int);  // returns true when complete
  int len;
};
const OneShot idlePool[] = {
  { sleepy_allArray,   nullptr,           sleepy_allArray_LEN   },
  { fly_allArray,      nullptr,           fly_allArray_LEN      },
  { worried_allArray,  nullptr,           worried_allArray_LEN  },
  { susp_allArray,     nullptr,           susp_allArray_LEN     },
  { nullptr,           drawSpaceInvaders, SI_FRAMES             },
  // bitmap:      { myAnim_allArray, nullptr, myAnim_allArray_LEN }
  // procedural:  { nullptr, myDrawFunc,     FRAME_COUNT         }
};
const int IDLE_POOL_SIZE = sizeof(idlePool) / sizeof(idlePool[0]);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

enum Mode { EYES, ONESHOT, DANCE };
Mode mode  = EYES;
int  frame = 0;
int  currentOneShot = -1;
int  lastOneShot    = -1;

unsigned long tFrame         = 0;
unsigned long tReport        = 0;
unsigned long tDanceEnter    = 0;
unsigned long tSilentStart   = 0;
unsigned long tLastSound     = 0;
unsigned long tNextOneShot   = 0;

void drawFrame(const unsigned char *bmp) {
  display.clearDisplay();
  display.drawBitmap(0, 0, bmp, SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
  display.display();
}

int pickOneShot() {
  if (IDLE_POOL_SIZE == 1) return 0;
  int pick;
  do { pick = random(IDLE_POOL_SIZE); } while (pick == lastOneShot);
  return pick;
}

void scheduleNextOneShot(unsigned long now) {
  tNextOneShot = now + 15000 + random(5000);
}

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(A0));
  Wire.begin(D2, D1);
  pinMode(MIC_DO_PIN, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 not found"));
    while (true) { yield(); }
  }

  for (int i = 0; i < hello_allArray_LEN; i++) {
    drawFrame(hello_allArray[i]);
    delay(70);
  }

  unsigned long now = millis();
  tFrame = tReport = tLastSound = now;
  scheduleNextOneShot(now);
}

void loop() {
  unsigned long now = millis();

  bool loud = (digitalRead(MIC_DO_PIN) == HIGH);
  if (loud) tLastSound = now;

  if (now - tReport >= REPORT_MS) {
    switch (mode) {

      case EYES:
        if (loud) {
          mode        = DANCE;
          frame       = 0;
          tDanceEnter = now;
          tSilentStart = 0;
          Serial.println(F("-> DANCE"));
          break;
        }
        if (now >= tNextOneShot) {
          currentOneShot = pickOneShot();
          lastOneShot    = currentOneShot;
          mode  = ONESHOT;
          frame = 0;
          Serial.printf("-> ONESHOT %d\n", currentOneShot);
        }
        break;

      case ONESHOT:
        if (loud) {
          mode        = DANCE;
          frame       = 0;
          tDanceEnter = now;
          tSilentStart = 0;
          Serial.println(F("-> DANCE (from ONESHOT)"));
        }
        break;

      case DANCE: {
        bool minHeld = (now - tDanceEnter) >= MIN_DANCE_MS;
        if (minHeld) {
          if (!loud) {
            if (tSilentStart == 0) tSilentStart = now;
            if ((now - tSilentStart) >= SILENCE_EXIT_MS) {
              mode  = EYES;
              frame = 0;
              tSilentStart = 0;
              scheduleNextOneShot(now);
              Serial.println(F("-> EYES"));
            }
          } else {
            tSilentStart = 0;
          }
        }
        break;
      }
    }
    tReport = now;
  }

  if (now - tFrame >= FRAME_MS) {
    switch (mode) {
      case DANCE:
        drawFrame(dance_allArray[frame]);
        frame = (frame + 1) % dance_allArray_LEN;
        break;
      case ONESHOT: {
        const OneShot& cur = idlePool[currentOneShot];
        display.clearDisplay();
        bool done;
        if (cur.frames) {
          display.drawBitmap(0, 0, cur.frames[frame], SCREEN_WIDTH, SCREEN_HEIGHT, WHITE);
          done = (frame >= cur.len - 1);
        } else {
          done = cur.draw(display, frame);
        }
        display.display();
        frame++;
        if (done) {
          mode  = EYES;
          frame = 0;
          scheduleNextOneShot(millis());
          Serial.println(F("-> EYES (oneshot done)"));
        }
        break;
      }
      default:
        drawFrame(eyes_allArray[frame]);
        frame = (frame + 1) % eyes_allArray_LEN;
        break;
    }
    tFrame = millis();
  }

  yield();
}
