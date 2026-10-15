// NINJA CLASH - 8-bit fight for ESP32 + SSD1306 128x64 OLED
// Your hero vs. a squad of masked ninjas that rush in and throw shuriken.
//
// Wiring: OLED SDA -> GPIO 21, SCL -> GPIO 22, VCC -> 3V3, GND -> GND
//         Four buttons, each between its pin and GND (internal pull-ups used):
//           LEFT  -> GPIO 25
//           RIGHT -> GPIO 26
//           JUMP  -> GPIO 27
//           ATTACK-> GPIO 14
// Libraries: Adafruit GFX, Adafruit SSD1306
//
// Play: up to 3 ninjas are on screen at once, 6 in the whole squad.
//       Shoot them with energy bolts (4 hits each), jump over shuriken,
//       and don't let them touch you. You have 3 hearts.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define W 128
#define H 64
#define OLED_ADDR 0x3C

#define PIN_L 25
#define PIN_R 26
#define PIN_J 27
#define PIN_A 14

#define GROUND 60
#define HERO_W 12
#define HERO_H 16
#define NINJA_W 10
#define NINJA_H 14
#define NINJA_HP 4
#define TOTAL_NINJAS 6
#define HERO_MAX_HP 3

Adafruit_SSD1306 d(W, H, &Wire, -1);

// Hero, 12x16 (2 bytes per row)
const uint8_t heroBmp[] PROGMEM = {
  0x0F, 0x00, 0x1F, 0x80, 0x26, 0x40, 0x26, 0x40,
  0x1F, 0x80, 0x0F, 0x00, 0x3F, 0xC0, 0x79, 0xE0,
  0x79, 0xE0, 0x70, 0xE0, 0x1F, 0x80, 0x19, 0x80,
  0x19, 0x80, 0x19, 0x80, 0x39, 0xC0, 0x39, 0xC0
};

// Ninja, 10x14 (2 bytes per row)
const uint8_t ninjaBmp[] PROGMEM = {
  0x1E, 0x00, 0x3F, 0x00, 0x7F, 0x80, 0x21, 0x00,
  0x3F, 0x00, 0x7F, 0x80, 0xFF, 0xC0, 0xFF, 0xC0,
  0xC0, 0xC0, 0x3F, 0x00, 0x3F, 0x00, 0x33, 0x00,
  0x33, 0x00, 0x73, 0x80
};

struct Bolt { float x, y, vx; bool on; };
struct Star { float x, y, vx; bool on; };
struct Ninja { float x, y, vy; int hp, cd, flash, jumpDir; bool on; };

Bolt bolts[3];
Star stars[4];
Ninja ninjas[3];

enum State { TITLE, PLAY, WIN, LOSE };
State st = TITLE;

float hx, hy, hvy;
int facing, heroHp, inv, shootCd;
int spawned, defeated, spawnCd;
uint32_t tick;
bool prevA = false;

bool hit(float ax, float ay, float aw, float ah, float bx2, float by2, float bw, float bh) {
  return ax < bx2 + bw && ax + aw > bx2 && ay < by2 + bh && ay + ah > by2;
}

void resetGame() {
  hx = 56; hy = GROUND - HERO_H; hvy = 0; facing = 1;
  heroHp = HERO_MAX_HP; inv = 0; shootCd = 0;
  spawned = 0; defeated = 0; spawnCd = 30; tick = 0;
  for (int i = 0; i < 3; i++) { bolts[i].on = false; ninjas[i].on = false; }
  for (int i = 0; i < 4; i++) stars[i].on = false;
}

void hurtHero() {
  if (inv > 0) return;
  heroHp--;
  inv = 45;
  if (heroHp <= 0) st = LOSE;
}

void spawnBolt() {
  for (int i = 0; i < 3; i++) {
    if (!bolts[i].on) {
      bolts[i].on = true;
      bolts[i].x = hx + (facing > 0 ? HERO_W : -4);
      bolts[i].y = hy + 6;
      bolts[i].vx = 3.5 * facing;
      return;
    }
  }
}

void throwStar(Ninja &n) {
  for (int i = 0; i < 4; i++) {
    if (!stars[i].on) {
      float dir = (hx + HERO_W / 2 > n.x + NINJA_W / 2) ? 1 : -1;
      stars[i].on = true;
      stars[i].x = n.x + NINJA_W / 2;
      stars[i].y = n.y + 5;
      stars[i].vx = 2.0 * dir;
      return;
    }
  }
}

void updateHero() {
  bool l = !digitalRead(PIN_L);
  bool r = !digitalRead(PIN_R);
  bool j = !digitalRead(PIN_J);
  bool a = !digitalRead(PIN_A);

  if (l) { hx -= 1.6; facing = -1; }
  if (r) { hx += 1.6; facing = 1; }
  hx = constrain(hx, 0, W - HERO_W);

  bool onGround = hy >= GROUND - HERO_H - 0.01;
  if (j && onGround) hvy = -6.0;
  hvy += 0.5;
  hy += hvy;
  if (hy >= GROUND - HERO_H) { hy = GROUND - HERO_H; hvy = 0; }

  if (shootCd > 0) shootCd--;
  if (a && shootCd == 0) { spawnBolt(); shootCd = 10; }
  if (inv > 0) inv--;
}

void updateNinjas() {
  if (spawnCd > 0) spawnCd--;
  if (spawned < TOTAL_NINJAS && spawnCd <= 0) {
    for (int i = 0; i < 3; i++) {
      if (!ninjas[i].on) {
        ninjas[i].on = true;
        ninjas[i].x = random(0, 2) ? -10 : W;
        ninjas[i].y = GROUND - NINJA_H;
        ninjas[i].vy = 0;
        ninjas[i].hp = NINJA_HP;
        ninjas[i].cd = random(40, 90);
        ninjas[i].flash = 0;
        ninjas[i].jumpDir = 1;
        spawned++;
        spawnCd = 70;
        break;
      }
    }
  }

  for (int i = 0; i < 3; i++) {
    Ninja &n = ninjas[i];
    if (!n.on) continue;
    if (n.flash > 0) n.flash--;

    float dx = (hx + HERO_W / 2) - (n.x + NINJA_W / 2);
    bool air = n.y < GROUND - NINJA_H - 0.01;

    if (!air) {
      if (fabs(dx) > 30) n.x += (dx > 0 ? 0.8 : -0.8);
      else if (fabs(dx) < 18) n.x -= (dx > 0 ? 0.6 : -0.6);
      if (random(0, 140) == 0) { n.vy = -5.2; n.jumpDir = (dx > 0) ? 1 : -1; }
      if (--n.cd <= 0) { throwStar(n); n.cd = random(70, 130); }
    } else {
      n.x += n.jumpDir * 1.4;
    }
    n.x = constrain(n.x, -12, W + 2);

    n.vy += 0.45;
    n.y += n.vy;
    if (n.y >= GROUND - NINJA_H) { n.y = GROUND - NINJA_H; n.vy = 0; }

    if (hit(hx + 2, hy, HERO_W - 4, HERO_H, n.x + 1, n.y, NINJA_W - 2, NINJA_H)) hurtHero();
  }
}

void updateShots() {
  for (int i = 0; i < 3; i++) {
    if (!bolts[i].on) continue;
    bolts[i].x += bolts[i].vx;
    if (bolts[i].x < -4 || bolts[i].x > W) { bolts[i].on = false; continue; }
    for (int k = 0; k < 3; k++) {
      Ninja &n = ninjas[k];
      if (n.on && hit(bolts[i].x, bolts[i].y, 4, 2, n.x, n.y, NINJA_W, NINJA_H)) {
        bolts[i].on = false;
        n.hp--;
        n.flash = 4;
        if (n.hp <= 0) {
          n.on = false;
          defeated++;
          if (defeated >= TOTAL_NINJAS) st = WIN;
        }
        break;
      }
    }
  }
  for (int i = 0; i < 4; i++) {
    if (!stars[i].on) continue;
    stars[i].x += stars[i].vx;
    if (stars[i].x < -4 || stars[i].x > W + 4) { stars[i].on = false; continue; }
    if (hit(stars[i].x - 2, stars[i].y - 2, 5, 5, hx + 2, hy, HERO_W - 4, HERO_H)) {
      stars[i].on = false;
      hurtHero();
    }
  }
}

void drawStar(int x, int y) {
  if ((tick / 2) % 2) {
    d.drawFastHLine(x - 2, y, 5, WHITE);
    d.drawFastVLine(x, y - 2, 5, WHITE);
  } else {
    d.drawLine(x - 2, y - 2, x + 2, y + 2, WHITE);
    d.drawLine(x - 2, y + 2, x + 2, y - 2, WHITE);
  }
}

void drawGame() {
  d.clearDisplay();
  d.drawFastHLine(0, GROUND, W, WHITE);
  for (int x = 0; x < W; x += 8) d.drawPixel(x, GROUND + 2, WHITE);

  if (inv == 0 || (inv / 3) % 2 == 0)
    d.drawBitmap((int)hx, (int)hy, heroBmp, HERO_W, HERO_H, WHITE);

  for (int i = 0; i < 3; i++) {
    if (ninjas[i].on && (ninjas[i].flash == 0 || ninjas[i].flash % 2 == 1))
      d.drawBitmap((int)ninjas[i].x, (int)ninjas[i].y, ninjaBmp, NINJA_W, NINJA_H, WHITE);
    if (bolts[i].on) d.fillRect((int)bolts[i].x, (int)bolts[i].y, 4, 2, WHITE);
  }
  for (int i = 0; i < 4; i++)
    if (stars[i].on) drawStar((int)stars[i].x, (int)stars[i].y);

  for (int i = 0; i < heroHp; i++) d.fillRect(2 + i * 7, 2, 5, 5, WHITE);
  d.setTextSize(1);
  d.setCursor(W - 34, 1);
  d.print("NIN ");
  d.print(TOTAL_NINJAS - defeated);
  d.display();
}

void drawBanner(const char* line1, const char* line2) {
  d.clearDisplay();
  d.setTextSize(2);
  d.setCursor(4, 8);
  d.print(line1);
  d.setTextSize(1);
  d.setCursor(8, 34);
  d.print(line2);
  d.setCursor(8, 50);
  d.print("Press ATTACK");
  d.display();
}

void setup() {
  pinMode(PIN_L, INPUT_PULLUP);
  pinMode(PIN_R, INPUT_PULLUP);
  pinMode(PIN_J, INPUT_PULLUP);
  pinMode(PIN_A, INPUT_PULLUP);
  Wire.begin(21, 22);
  d.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  d.setTextColor(WHITE);
  randomSeed(micros());
  resetGame();
}

void loop() {
  static uint32_t last = 0;
  while (millis() - last < 25) {}
  last = millis();

  bool a = !digitalRead(PIN_A);
  bool aPressed = a && !prevA;
  prevA = a;

  if (st == TITLE) {
    drawBanner("NINJA CLASH", "Hero vs the squad");
    if (aPressed) { resetGame(); st = PLAY; }
  } else if (st == PLAY) {
    tick++;
    updateHero();
    updateNinjas();
    updateShots();
    drawGame();
  } else if (st == WIN) {
    drawBanner("VICTORY!", "The squad is down");
    if (aPressed) { resetGame(); st = PLAY; }
  } else {
    drawBanner("DEFEATED", "The squad wins");
    if (aPressed) { resetGame(); st = PLAY; }
  }
}
