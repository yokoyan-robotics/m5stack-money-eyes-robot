/*****************************************************************
  Money-Eyes Robot Face (M5Stack Basic v2.7)
  - Random eye movement and blinking animation
  - Eyes transform to $ symbols with sound effects
  - I2S audio support (MAX98357A) with noise reduction
  
  Firmware: Money-Eyes Robot v1.2 - I2S Audio Edition
  Date: 2025-08-11
  Author: Custom Build
  
  Hardware Requirements:
  - M5Stack Basic v2.7
  - MAX98357A I2S Amplifier Module
  - Speaker (4Ω, 3W recommended)
  - microSD card with cash_44_stereo.wav
  
  Wiring:
  - GPIO12 -> MAX98357A BCLK
  - GPIO13 -> MAX98357A LRC
  - GPIO15 -> MAX98357A DIN
  - 3.3V   -> MAX98357A VIN
  - GND    -> MAX98357A GND

  Audio Credits:
  - Sound Effect: cash_44_stereo.wav
  - Source: OtoLogic (https://otologic.jp/)
  - License: Free for commercial and non-commercial use
  - License URL: https://otologic.jp/free/license.html
*****************************************************************/
#include <M5Stack.h>
#include <SD.h>
#include <WiFi.h>
#include "driver/i2s.h"

// Firmware Information
#define FW_NAME "Money-Eyes Robot"
#define FW_VERSION "v1.2"
#define FW_BUILD "I2S Audio Edition"
#define FW_DATE "2025-08-11"

// I2S Configuration
#define I2S_NUM         I2S_NUM_0
#define I2S_SAMPLE_RATE 44100
#define I2S_SAMPLE_BITS 16
#define I2S_CHANNELS    2

// MAX98357A Pin Assignment (Avoiding internal circuit interference)
#define I2S_BCK_IO      (12)  // BCLK
#define I2S_WS_IO       (13)  // LRC
#define I2S_DO_IO       (15)  // DIN

/* ===== Eye Layout Constants ===== */
const int LEFT_EYE_X   = 100;
const int RIGHT_EYE_X  = 220;
const int EYE_CENTER_Y = 120;
const int WHITE_RADIUS = 55;
const int BLACK_RADIUS = 20;

/* ===== Animation Parameters ===== */
int currentX = 0, currentY = 0;          // Current pupil offset
int targetX = 0, targetY = 0;            // Target pupil offset
bool moneyMode = false, previousMoney = false;

unsigned long nextBlinkTime = 0;         // Next blink timing
unsigned long nextMoneyTime = 0;         // Next money mode toggle timing

/* ===== Graphics and Audio Objects ===== */
TFT_eSprite sprite(&M5.Lcd);             // Sprite for smooth rendering
bool i2sInitialized = false;
bool soundPlayedFlag = false;            // Prevent duplicate playback

/* ===== Simple I2S Initialization with Noise Reduction ===== */
void setupI2S() {
  if (i2sInitialized) {
    i2s_driver_uninstall(I2S_NUM);
    i2sInitialized = false;
  }
  
  // Noise reduction measures
  WiFi.mode(WIFI_OFF);
  btStop();  // Disable Bluetooth
  
  // Disable built-in speaker pins
  pinMode(25, INPUT_PULLDOWN);  // Original LRC
  pinMode(26, INPUT_PULLDOWN);  // Original BCLK
  
  // Initialize I2S pins
  pinMode(I2S_BCK_IO, INPUT);
  pinMode(I2S_WS_IO, INPUT);
  pinMode(I2S_DO_IO, INPUT);
  delay(100);  // Stabilization wait
  
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,          // Increased buffer count for stability
    .dma_buf_len = 1024,         // Large buffer for stable operation
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_IO,
    .ws_io_num = I2S_WS_IO,
    .data_out_num = I2S_DO_IO,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  if (i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL) != ESP_OK) {
    Serial.println("I2S driver installation failed");
    return;
  }
  
  if (i2s_set_pin(I2S_NUM, &pin_config) != ESP_OK) {
    Serial.println("I2S pin configuration failed");
    return;
  }
  
  if (i2s_set_clk(I2S_NUM, I2S_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO) != ESP_OK) {
    Serial.println("I2S clock configuration failed");
    return;
  }
  
  // Clear DMA buffer and initialize
  i2s_zero_dma_buffer(I2S_NUM);
  delay(100);
  
  // Fill buffer with silence to prevent pop noise
  int16_t silence[2048] = {0};
  size_t bytesWritten;
  for (int i = 0; i < 15; i++) {  // Increased iterations for reliable buffer fill
    i2s_write(I2S_NUM, silence, sizeof(silence), &bytesWritten, 1000);
    delay(5);  // Wait between writes
  }
  
  i2sInitialized = true;
  Serial.println("I2S initialized successfully");
}

/* ===== Audio Playback (OtoLogic Sound Effect) ===== */
// Sound effect: cash_44_stereo.wav
// Source: OtoLogic (https://otologic.jp/)
// License: Free for commercial and non-commercial use
// License: https://otologic.jp/free/license.html

void playWAVFile() {
  if (!i2sInitialized || soundPlayedFlag) return;
  
  File audioFile = SD.open("/cash_44_stereo.wav");
  if (!audioFile) {
    Serial.println("WAV file not found");
    return;
  }
  
  Serial.println("Playing WAV file...");
  
  // Skip WAV header (44 bytes)
  audioFile.seek(44);
  
  // Allocate large buffer for efficient reading
  uint8_t* buffer = (uint8_t*)malloc(4096);
  if (!buffer) {
    Serial.println("Memory allocation failed");
    audioFile.close();
    return;
  }
  
  size_t bytesWritten;
  unsigned long startTime = millis();
  
  while (audioFile.available() && (millis() - startTime < 5000)) {  // Maximum 5 seconds
    int bytesRead = audioFile.read(buffer, 4096);
    if (bytesRead > 0) {
      // Align to 4-byte boundary (16-bit stereo)
      bytesRead = (bytesRead / 4) * 4;
      
      esp_err_t result = i2s_write(I2S_NUM, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
      if (result != ESP_OK) {
        Serial.printf("I2S write error: %d\n", result);
        break;
      }
    }
    
    // Yield occasionally to prevent watchdog timeout
    if ((millis() - startTime) % 100 == 0) {
      yield();
    }
  }
  
  // End processing: Gradual fade-out
  int16_t fadeBuffer[256];
  
  // 1. Clear buffer with short silence
  memset(fadeBuffer, 0, sizeof(fadeBuffer));
  for (int i = 0; i < 3; i++) {
    i2s_write(I2S_NUM, fadeBuffer, sizeof(fadeBuffer), &bytesWritten, 100);
    delay(5);
  }
  
  // 2. Gradual DMA buffer clear
  i2s_zero_dma_buffer(I2S_NUM);
  delay(50);
  
  // 3. Additional silence for complete muting
  for (int i = 0; i < 5; i++) {
    i2s_write(I2S_NUM, fadeBuffer, sizeof(fadeBuffer), &bytesWritten, 50);
    delay(10);
  }
  
  // 4. Final DMA clear
  i2s_zero_dma_buffer(I2S_NUM);
  delay(20);
  
  free(buffer);
  audioFile.close();
  soundPlayedFlag = true;
  
  Serial.println("WAV playback completed");
}

/* ===== Draw One Frame of Eyes ===== */
void drawEyes(int offsetX, int offsetY, bool closed = false) {
  sprite.fillScreen(TFT_BLACK);

  /* Draw white eye background */
  sprite.fillCircle(LEFT_EYE_X, EYE_CENTER_Y, WHITE_RADIUS, TFT_WHITE);
  sprite.fillCircle(RIGHT_EYE_X, EYE_CENTER_Y, WHITE_RADIUS, TFT_WHITE);

  if (closed) { /* Blinking state */
    sprite.fillRect(LEFT_EYE_X - 30, EYE_CENTER_Y - 5, 60, 10, TFT_BLACK);
    sprite.fillRect(RIGHT_EYE_X - 30, EYE_CENTER_Y - 5, 60, 10, TFT_BLACK);
  }
  else if (moneyMode) { /* Money mode: $ symbols */
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextSize(5);
    sprite.setTextColor(TFT_BLACK, TFT_WHITE);
    sprite.drawString("$", LEFT_EYE_X + offsetX, EYE_CENTER_Y + offsetY);
    sprite.drawString("$", RIGHT_EYE_X + offsetX, EYE_CENTER_Y + offsetY);
  }
  else { /* Normal state: black pupils */
    sprite.fillCircle(LEFT_EYE_X + offsetX, EYE_CENTER_Y + offsetY, BLACK_RADIUS, TFT_BLACK);
    sprite.fillCircle(RIGHT_EYE_X + offsetX, EYE_CENTER_Y + offsetY, BLACK_RADIUS, TFT_BLACK);
  }

  sprite.pushSprite(0, 0);

  /* Play sound effect when entering money mode (once only) */
  if (moneyMode && !previousMoney && !soundPlayedFlag) {
    playWAVFile();
  }
  
  // Reset sound flag when exiting money mode
  if (!moneyMode && previousMoney) {
    soundPlayedFlag = false;
  }
  
  previousMoney = moneyMode;
}

/* ===== Display Firmware Information ===== */
void showFirmwareInfo() {
  M5.Lcd.clear();
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  
  M5.Lcd.setCursor(10, 20);
  M5.Lcd.printf("Firmware: %s %s", FW_NAME, FW_VERSION);
  M5.Lcd.setCursor(10, 50);
  M5.Lcd.printf("Build: %s", FW_BUILD);
  M5.Lcd.setCursor(10, 80);
  M5.Lcd.printf("Date: %s", FW_DATE);
  
  M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Lcd.setCursor(10, 120);
  M5.Lcd.println("Features:");
  M5.Lcd.setCursor(10, 150);
  M5.Lcd.println("- Random eye movement");
  M5.Lcd.setCursor(10, 170);
  M5.Lcd.println("- Blinking animation");
  M5.Lcd.setCursor(10, 190);
  M5.Lcd.println("- Money mode with sound");
  M5.Lcd.setCursor(10, 210);
  M5.Lcd.println("- I2S Audio (MAX98357A)");
  
  delay(5000);  // Display for 5 seconds
}

/* ===== Setup Function ===== */
void setup() {
  M5.begin();
  M5.Power.begin();
  Serial.begin(115200);
  
  // Display firmware information
  showFirmwareInfo();
  
  // Noise reduction measures
  WiFi.mode(WIFI_OFF);
  btStop();
  
  pinMode(I2S_BCK_IO, OUTPUT);
  pinMode(I2S_WS_IO, OUTPUT);
  pinMode(I2S_DO_IO, OUTPUT);
  digitalWrite(I2S_BCK_IO, LOW);
  digitalWrite(I2S_WS_IO, LOW);
  digitalWrite(I2S_DO_IO, LOW);
  
  M5.Lcd.clear();
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Lcd.println("Initializing...");
  
  /* --- SD Card Initialization --- */
  M5.Lcd.println("Init SD card...");
  
  bool sdMounted = false;
  if (SD.begin()) {
    sdMounted = true;
    M5.Lcd.println("SD mounted");
  } else if (SD.begin(4)) {
    sdMounted = true;
    M5.Lcd.println("SD mounted (CS=4)");
  }
  
  if (!sdMounted) {
    M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
    M5.Lcd.println("SD Mount Failed!");
    while(1) delay(1000);
  }
  
  if (SD.exists("/cash_44_stereo.wav")) {
    M5.Lcd.println("WAV file found!");
  } else {
    M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Lcd.println("WAV file not found!");
    M5.Lcd.println("Will run without sound");
  }
  
  /* --- I2S Initialization --- */
  M5.Lcd.println("Init I2S...");
  setupI2S();
  
  if (i2sInitialized) {
    M5.Lcd.println("I2S OK");
  } else {
    M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
    M5.Lcd.println("I2S Failed!");
  }
  
  delay(2000);
  
  /* --- Sprite Initialization --- */
  sprite.setColorDepth(8);
  sprite.createSprite(320, 240);
  drawEyes(0, 0);

  /* --- Random Seed and Timer Initialization --- */
  randomSeed(analogRead(0));
  nextBlinkTime = millis() + random(2000, 5000);   // 2-5 seconds
  nextMoneyTime = millis() + random(4000, 8000);   // 4-8 seconds
  
  M5.Lcd.clear();
  Serial.printf("Setup completed - %s %s\n", FW_NAME, FW_VERSION);
}

/* ===== Main Loop ===== */
void loop() {
  unsigned long now = millis();

  /* Blinking animation */
  if (now >= nextBlinkTime) {
    drawEyes(currentX, currentY, true);  
    delay(120);  // Eyes closed duration
    drawEyes(currentX, currentY, false);
    nextBlinkTime = now + random(2000, 5000);
  }

  /* Money mode toggle */
  if (now >= nextMoneyTime) {
    moneyMode = !moneyMode;
    nextMoneyTime = now + random(4000, 8000);
  }

  /* Eye movement target determination */
  if (currentX == targetX && currentY == targetY) {
    targetX = random(-30, 31);   // Horizontal ±30 pixels
    targetY = random(-15, 16);   // Vertical ±15 pixels
  }

  /* Smooth pixel-by-pixel movement */
  if (currentX < targetX) currentX++; else if (currentX > targetX) currentX--;
  if (currentY < targetY) currentY++; else if (currentY > targetY) currentY--;

  drawEyes(currentX, currentY);

  delay(10);  // ~100 FPS
}