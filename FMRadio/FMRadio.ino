
#include <SPI.h>
#include "AiEsp32RotaryEncoder.h"
#include <Wire.h>
#include <radio.h>
#include <RDA5807M.h>
#include <RDSParser.h>
#include <TFT_eSPI.h>
#include "fonts.h"
// fm.h (bitmap background) removed — replaced with programmatic UI

// Color palette (RGB565)
#define color1  0xC618   // silver — primary text
#define color2  0xE600   // amber  — accents / freq
#define color3  0x7BCF   // muted blue-grey — secondary text
#define colorBG 0x10A2   // dark blue-grey  — top bar background

#define BUTTON_1 35   // right: short = volume mode, long = mute
#define BUTTON_2  0   // left:  short = backlight,   long = bass boost

#define TFT_BL    4
#define TFX_SLPIN 33

#define FIX_BAND RADIO_BAND_FM

// Rotary encoder (dt-067)
#define ENCODER_CLK   25
#define ENCODER_DT    32
#define ENCODER_SW    13
#define ENCODER_VCC   -1
#define ENCODER_STEPS  2

#define FM_FREQ_MIN  8750   // 87.5 MHz in 10kHz units
#define FM_FREQ_MAX 10800   // 108.0 MHz

#define LONG_PRESS_MS   600UL
#define SLEEP_TIMEOUT_MS 60000UL

// ---- Display layout ----
#define TOP_BAR_Y   0
#define TOP_BAR_H  15
#define FREQ_Y     17   // large freq / VOL display (font 4, ~26px)
#define FREQ_H     30
#define RDS_Y      49   // RDS name, or volume bar
#define RDS_H      11
#define MODE_Y     62   // mode label row
#define MODE_H     10
#define SEP_Y      73
#define LIST_Y     75   // preset list start
#define LIST_ROW   26   // px per preset row
#define LIST_N      5

const int pwmFreq       = 5000;
const int pwmResolution = 8;

int  backlight[5] = {30, 50, 60, 120, 220};
byte br = 1;

TFT_eSPI tft = TFT_eSPI();
RDA5807M radio;
RDSParser rds;
AiEsp32RotaryEncoder *rotaryEncoder = nullptr;

String stations[LIST_N] = {"KOPB", "KMHD", "Jam'n", "KBOO", "KINK"};
String freqs[LIST_N]    = {"91.5", "89.1", "107.5", "90.7", "102.9"};

int chosen   = 0;
int scanFreq = 9150;  // 10kHz units; 9150 = 91.5 MHz

enum RadioMode { MODE_PRESET, MODE_SCAN, MODE_VOLUME };
RadioMode currentMode  = MODE_PRESET;
RadioMode previousMode = MODE_PRESET;

bool   isMuted     = false;
bool   isBassBoost = false;
String rdsText     = "";

unsigned long lastInteractionTime = 0;
bool isAsleep = false;

// Long/short press tracking per button
struct BtnState { bool down, longHandled; unsigned long pressStart; };
BtnState btn1 = {}, btn2 = {}, encBtn = {};

// ───── ISR ─────────────────────────────────────────────────────────────────

void IRAM_ATTR readEncoderISR() { if (rotaryEncoder) rotaryEncoder->readEncoder_ISR(); }

// ───── RDS ─────────────────────────────────────────────────────────────────

void RDS_process(uint16_t b1, uint16_t b2, uint16_t b3, uint16_t b4) {
  rds.processData(b1, b2, b3, b4);
}

void DisplayServiceName(const char *name) {
  Serial.print("RDS:"); Serial.println(name);
  rdsText = String(name);
  rdsText.trim();
  drawRdsRow();
}

// ───── Utility ─────────────────────────────────────────────────────────────

void wakeFromSleep() {
  if (isAsleep) {
    isAsleep = false;
    ledcWrite(TFT_BL, backlight[br]);
  }
  lastInteractionTime = millis();
}

// 9150 → "91.5"
String fmtFreq(int f) {
  return String(f / 100) + "." + String((f % 100) / 10);
}

// ───── Draw: top bar ───────────────────────────────────────────────────────

void drawTopBar() {
  tft.fillRect(0, TOP_BAR_Y, 135, TOP_BAR_H, colorBG);

  // "FM" label
  tft.setTextColor(color1, colorBG);
  tft.drawString("FM", 4, 3, 1);

  // Signal bars (4 bars, bottom-aligned at y=13, increasing height)
  RADIO_INFO info;
  radio.getRadioInfo(&info);
  int bars = map(constrain((int)info.rssi, 0, 63), 0, 63, 0, 4);
  const int barH[4] = {4, 6, 9, 12};
  for (int i = 0; i < 4; i++) {
    uint16_t c = (i < bars) ? color2 : 0x4208;
    tft.fillRect(50 + i * 9, TOP_BAR_H - 1 - barH[i], 6, barH[i], c);
  }

  // Stereo/Mono indicator
  tft.setTextColor(info.stereo ? color1 : 0x4208, colorBG);
  tft.drawString(info.stereo ? "ST" : "MN", 90, 3, 1);

  // Mute indicator
  tft.setTextColor(isMuted ? TFT_RED : colorBG, colorBG);
  tft.drawString("M", 110, 3, 1);

  // Bass boost indicator
  tft.setTextColor(isBassBoost ? color2 : colorBG, colorBG);
  tft.drawString("B", 124, 3, 1);

  tft.drawFastHLine(0, TOP_BAR_H, 135, color1);
}

// ───── Draw: large display area ────────────────────────────────────────────

void drawLargeFreqArea(const String &text) {
  tft.fillRect(0, FREQ_Y, 135, FREQ_H, TFT_BLACK);
  tft.setTextColor(color1, TFT_BLACK);
  tft.drawString(text, 14, FREQ_Y + 2, 4);
}

// ───── Draw: RDS / volume bar ──────────────────────────────────────────────

void drawRdsRow() {
  tft.fillRect(0, RDS_Y, 135, RDS_H, TFT_BLACK);
  if (currentMode != MODE_VOLUME && rdsText.length() > 0) {
    tft.setTextColor(color3, TFT_BLACK);
    tft.drawString(rdsText, 14, RDS_Y + 2, 1);
  }
}

void drawVolBar(int vol) {
  tft.fillRect(0, RDS_Y, 135, RDS_H, TFT_BLACK);
  int w = (vol * 107) / 15;
  if (w > 0) tft.fillRect(14, RDS_Y + 2, w, 7, color2);
}

// ───── Draw: mode label row ─────────────────────────────────────────────────

void drawModeRow() {
  tft.fillRect(0, MODE_Y, 135, MODE_H, TFT_BLACK);
  tft.setTextColor(color1, TFT_BLACK);
  const char *label = currentMode == MODE_PRESET ? "PRESET" :
                      currentMode == MODE_SCAN   ? "SCAN"   : "VOLUME";
  tft.drawString(label, 14, MODE_Y + 1, 1);
  tft.drawFastHLine(0, SEP_Y, 135, 0x4208);
}

// ───── Draw: single preset row ─────────────────────────────────────────────

void drawPresetRow(int i, bool selected) {
  int y = LIST_Y + i * LIST_ROW;
  tft.fillRect(0, y, 135, LIST_ROW - 1, TFT_BLACK);

  // Selection dot
  tft.fillCircle(8, y + LIST_ROW / 2, 3, selected ? TFT_WHITE : TFT_BLACK);

  // Station name (font 2, 16px tall); highlighted when selected
  tft.setTextColor(selected ? color1 : color3, TFT_BLACK);
  tft.drawString(stations[i], 16, y + 5, 2);

  // Frequency (font 1, right column)
  tft.setTextColor(color2, TFT_BLACK);
  tft.drawString(freqs[i], 88, y + 9, 1);
}

void drawPresetList() {
  for (int i = 0; i < LIST_N; i++)
    drawPresetRow(i, currentMode == MODE_PRESET && i == chosen);
}

// ───── Full-screen redraw (mode transitions) ───────────────────────────────

void drawFullUI() {
  tft.fillScreen(TFT_BLACK);
  drawTopBar();
  if (currentMode == MODE_VOLUME) {
    drawLargeFreqArea("VOL " + String(radio.getVolume()));
    drawVolBar(radio.getVolume());
  } else {
    drawLargeFreqArea(currentMode == MODE_PRESET ? freqs[chosen] : fmtFreq(scanFreq));
    drawRdsRow();
  }
  drawModeRow();
  drawPresetList();
}

// ───── Mode switching ──────────────────────────────────────────────────────

void switchToPresetMode() {
  currentMode = MODE_PRESET;
  radio.setBandFrequency(FIX_BAND, freqs[chosen].toFloat() * 100);
  drawFullUI();
  rotaryEncoder->setAcceleration(0);
  rotaryEncoder->setBoundaries(0, LIST_N - 1, true);
  rotaryEncoder->setEncoderValue(chosen);
}

void switchToScanMode() {
  currentMode = MODE_SCAN;
  scanFreq = (int)(freqs[chosen].toFloat() * 100);
  radio.setBandFrequency(FIX_BAND, scanFreq);
  drawFullUI();
  rotaryEncoder->setAcceleration(50);
  rotaryEncoder->setBoundaries(FM_FREQ_MIN, FM_FREQ_MAX, false);
  rotaryEncoder->setEncoderValue(scanFreq);
}

void switchToVolumeMode() {
  previousMode = currentMode;
  currentMode  = MODE_VOLUME;
  int vol = radio.getVolume();
  drawLargeFreqArea("VOL " + String(vol));
  drawVolBar(vol);
  drawModeRow();
  rotaryEncoder->setAcceleration(0);
  rotaryEncoder->setBoundaries(0, 15, false);
  rotaryEncoder->setEncoderValue(vol);
}

void exitVolumeMode() {
  if (previousMode == MODE_SCAN) switchToScanMode();
  else                           switchToPresetMode();
}

// ───── Seek ────────────────────────────────────────────────────────────────

void doSeekUp() {
  drawLargeFreqArea("SEEK...");
  radio.seekUp(true);
  // Poll for Seek/Tune Complete (max 4 s)
  RADIO_INFO info;
  unsigned long start = millis();
  do {
    delay(100);
    radio.getRadioInfo(&info);
  } while (!info.tuned && millis() - start < 4000);
  scanFreq = radio.getFrequency();
  drawLargeFreqArea(fmtFreq(scanFreq));
  rotaryEncoder->setEncoderValue(scanFreq);
  rdsText = "";
  drawRdsRow();
  drawTopBar();  // refresh signal strength after landing
}

// ───── Periodic status refresh ─────────────────────────────────────────────

unsigned long lastStatusUpdate = 0;

void maybeRefreshStatus() {
  if (millis() - lastStatusUpdate < 2000) return;
  lastStatusUpdate = millis();
  drawTopBar();
}

// ───── Setup ───────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);

  Serial.println("1: Wire");
  Wire.begin(26, 27);  // SDA=26, SCL=27 → radio board
  delay(100);

  // I2C scan — confirm radio board is visible before calling init()
  Serial.println("1b: I2C scan");
  bool radioFound = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.print("  found: 0x"); Serial.println(addr, HEX);
      if (addr == 0x10 || addr == 0x11) radioFound = true;
    }
  }
  if (!radioFound) Serial.println("  *** RDA5807M NOT FOUND — check Qwiic cable (SDA=26, SCL=27) ***");

  Serial.println("2: radio.initWire + init");
  radio.initWire(Wire);  // must call before init() — otherwise _i2cPort is null → crash
  bool ok = radio.init();
  Serial.print("3: radio.init returned "); Serial.println(ok ? "true" : "false");
  if (ok) {
    radio.debugEnable();
    radio.attachReceiveRDS(RDS_process);
    rds.attachServiceNameCallback(DisplayServiceName);
    radio.setBandFrequency(FIX_BAND, freqs[chosen].toFloat() * 100);
    radio.setVolume(10);
    radio.setMono(false);
  } else {
    Serial.println("  *** radio.init() failed — continuing without radio ***");
  }

  Serial.println("4: tft.init");
  tft.init();
  tft.setSwapBytes(true);

  Serial.println("5: backlight");
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  ledcAttach(TFT_BL, pwmFreq, pwmResolution);
  ledcWrite(TFT_BL, backlight[br]);

  Serial.println("6: drawFullUI");
  drawFullUI();

  rotaryEncoder = new AiEsp32RotaryEncoder(ENCODER_CLK, ENCODER_DT, ENCODER_SW, ENCODER_VCC, ENCODER_STEPS);
  rotaryEncoder->begin();
  rotaryEncoder->setup(readEncoderISR);
  rotaryEncoder->setAcceleration(0);
  rotaryEncoder->setBoundaries(0, LIST_N - 1, true);
  rotaryEncoder->setEncoderValue(chosen);

  lastInteractionTime = millis();
}

// ───── Loop ────────────────────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // Idle sleep
  if (!isAsleep && now - lastInteractionTime > SLEEP_TIMEOUT_MS) {
    isAsleep = true;
    ledcWrite(TFT_BL, 0);
  }

  // ── Encoder button: short = toggle PRESET↔SCAN / exit VOLUME
  //                   long  = seek (SCAN mode only)
  if (rotaryEncoder->isEncoderButtonDown()) {
    if (!encBtn.down) {
      encBtn = {true, false, now};
      wakeFromSleep();
    }
    if (!encBtn.longHandled && now - encBtn.pressStart >= LONG_PRESS_MS) {
      encBtn.longHandled = true;
      if (currentMode == MODE_SCAN) doSeekUp();
    }
  } else if (encBtn.down) {
    encBtn.down = false;
    if (!encBtn.longHandled) {
      wakeFromSleep();
      if      (currentMode == MODE_VOLUME) exitVolumeMode();
      else if (currentMode == MODE_PRESET) switchToScanMode();
      else                                 switchToPresetMode();
    }
  }

  // ── Encoder rotation
  if (rotaryEncoder->encoderChanged()) {
    wakeFromSleep();
    long val = rotaryEncoder->readEncoder();
    if (currentMode == MODE_PRESET) {
      int prev = chosen;
      chosen = (int)val;
      drawPresetRow(prev, false);
      drawPresetRow(chosen, true);
      radio.setBandFrequency(FIX_BAND, freqs[chosen].toFloat() * 100);
      drawLargeFreqArea(freqs[chosen]);
      rdsText = "";
      drawRdsRow();
    } else if (currentMode == MODE_SCAN) {
      scanFreq = (int)val;
      radio.setBandFrequency(FIX_BAND, scanFreq);
      drawLargeFreqArea(fmtFreq(scanFreq));
      rdsText = "";
      drawRdsRow();
    } else {  // MODE_VOLUME
      int vol = (int)val;
      radio.setVolume(vol);
      drawLargeFreqArea("VOL " + String(vol));
      drawVolBar(vol);
    }
  }

  // ── BUTTON_1 (GPIO 35): short = volume mode toggle, long = mute toggle
  if (digitalRead(BUTTON_1) == 0) {
    if (!btn1.down) {
      btn1 = {true, false, now};
      wakeFromSleep();
    }
    if (!btn1.longHandled && now - btn1.pressStart >= LONG_PRESS_MS) {
      btn1.longHandled = true;
      isMuted = !isMuted;
      radio.setMute(isMuted);
      drawTopBar();
    }
  } else if (btn1.down) {
    btn1.down = false;
    if (!btn1.longHandled) {
      wakeFromSleep();
      if (currentMode == MODE_VOLUME) exitVolumeMode();
      else                            switchToVolumeMode();
    }
  }

  // ── BUTTON_2 (GPIO 0): short = cycle backlight, long = bass boost toggle
  if (digitalRead(BUTTON_2) == 0) {
    if (!btn2.down) {
      btn2 = {true, false, now};
      wakeFromSleep();
    }
    if (!btn2.longHandled && now - btn2.pressStart >= LONG_PRESS_MS) {
      btn2.longHandled = true;
      isBassBoost = !isBassBoost;
      radio.setBassBoost(isBassBoost);
      drawTopBar();
    }
  } else if (btn2.down) {
    btn2.down = false;
    if (!btn2.longHandled) {
      wakeFromSleep();
      br = (br + 1) % 5;
      ledcWrite(TFT_BL, backlight[br]);
    }
  }

  radio.checkRDS();
  maybeRefreshStatus();
}
