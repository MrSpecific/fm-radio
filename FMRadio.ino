
// #include <User_Setup.h>
#include <SPI.h>
// #include "AiEsp32RotaryEncoder.h"

#include <Wire.h>
// #include <TEA5767Radio.h>
// #include <TEA5767.h>
#include <radio.h>
#include <TEA5767.h>
#include <TFT_eSPI.h>
#include "fm.h"
#include "fonts.h"

#define color1 0xC618
#define color2 0xE600
#define color3 0x2E35

#define ADC_EN              14  //ADC_EN is the ADC detection enable port
#define ADC_PIN             34
#define BUTTON_1            35
#define BUTTON_2            0

// Check these:
#define TFT_BL 4  // Define TFT_BL as the GPIO pin number for the backlight
#define TFT_SLPIN 33  // Define TFT_SLPIN as the GPIO pin number for the sleep control

/// The band that will be tuned by this sketch is FM.
#define FIX_BAND RADIO_BAND_FM

const int pwmFreq = 5000;
const int pwmResolution = 8;
const int pwmLedChannelTFT = 0;

int backlight[5] = { 30, 50, 60, 120, 220 };
byte br = 1;

TFT_eSPI tft = TFT_eSPI();
// TEA5767Radio radio = TEA5767Radio();
// TEA5767 radio = TEA5767();
TEA5767 radio;

String stations[5] = { "KOPB", "KMHD", "Jam'n", "KBOO", "KINK" };
String freq[5] = { "91.5", "89.1", "107.5", "90.7", "102.9" };
#define FIX_STATION 8910
// int freq[5] = { 9150, 8910, 10750, 9070, 9660 };

int sx = 20;
int sy = 130;
int fx = 88;
int fy = 134;

int chosen = 0;

void setFreq() {
  radio.setBandFrequency(FIX_BAND, freq[chosen].toFloat() * 100);
  // radio.setBandFrequency(FIX_BAND, FIX_STATION);
}

void radioInfo() {
  char s[12];
  radio.formatFrequency(s, sizeof(s));
  Serial.print("Station:"); 
  Serial.println(s);
  
  Serial.print("Radio:"); 
  radio.debugRadioInfo();
  
  Serial.print("Audio:"); 
  radio.debugAudioInfo();
}

void setup() {
  Serial.begin(115200);
  // Serial.print("Hello");
  // Serial.println("serial setup test");
  pinMode(0, INPUT_PULLUP);
  pinMode(35, INPUT_PULLUP);

  Wire.begin(26, 27);

  // Initialize the Radio 
  radio.init();

  // Enable information to the Serial port
  radio.debugEnable();

  // radio.setFrequency(freq[chosen].toFloat());
  // radio.setBandFrequency(FIX_BAND, freq[chosen]);
  setFreq();
  radio.setVolume(10);
  radio.setMono(false);
  // radio.setMuted(false);

  tft.init();
  // tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, 135, 240, fm);
  // tft.fillScreen(TFT_NAVY);


  ledcSetup(pwmLedChannelTFT, pwmFreq, pwmResolution);
  ledcAttachPin(TFT_BL, pwmLedChannelTFT);
  // ledcAttachPin(32, pwmLedChannelTFT);
  ledcWrite(pwmLedChannelTFT, backlight[br]);

  for (int i = 0; i < 5; i++) {
    tft.setTextColor(color3, TFT_BLACK);
    tft.drawString(stations[i], sx, sy + (i * 15), 2);
    tft.setTextFont(0);
    tft.setTextColor(color2, TFT_BLACK);
    tft.drawString(freq[i], fx, fy + (i * 15));
  }

  tft.setTextColor(color1, TFT_BLACK);
  // tft.fillCircle(12, sy + 8, 3, TFT_WHITE);
  tft.fillCircle(12, sy + 8, 3, TFT_WHITE);
  // tft.setFreeFont(&DSEG7_Classic_Regular_36);
  // tft.setFreeFont(&DSEG14_Classic_Regular_36Bitmaps);
  // tft.setTextFont(GLCD);
  // tft.setFreeFont(FSB9);
  tft.drawString(freq[chosen], 14, 84);
}

int b = 0;
int press2 = 0;

void loop() {

  if (digitalRead(0) == 0) {
    if (b == 0) {
      b = 1;
      tft.fillCircle(12, sy + 8 + (chosen * 15), 3, TFT_BLACK);
      chosen++;

      if (chosen > 4)
        chosen = 0;
      tft.fillCircle(12, sy + 8 + (chosen * 15), 3, TFT_WHITE);
      // radio.setFrequency(freq[chosen].toFloat());
      // radio.setBandFrequency(FIX_BAND, freq[chosen]);
      setFreq();
      tft.drawString(freq[chosen], 14, 84);
      // tft.drawString(String(radio.getFrequency()), 14, 86);
      Serial.println("New frequency:");
      // Serial.println(String(radio.getFrequency()));
      // Serial.println(radio.read_status());
      radioInfo();
    }
  } else b = 0;

  if (digitalRead(35) == 0) {
    if (press2 == 0) {
      press2 = 1;
      br++;
      if (br >= 5)
        br = 0;
      ledcWrite(pwmLedChannelTFT, backlight[br]);
    }
  } else press2 = 0;
}
