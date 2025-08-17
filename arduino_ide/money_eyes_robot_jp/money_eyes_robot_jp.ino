/*****************************************************************
   Money-Eyes Robot Face (M5Stack Basic v2.7)
  - ランダム視線移動・まばたきアニメーション
  - 目がドル記号に変化して効果音再生
  - I2S音声対応（MAX98357A）ノイズ対策済み
  
  ファームウェア: Money-Eyes Robot v1.2 - I2S Audio Edition
  日付: 2025-08-11
  作成者: Custom Build
  
  必要なハードウェア:
  - M5Stack Basic v2.7
  - MAX98357A I2Sアンプモジュール
  - スピーカー (4Ω、3W推奨)
  - cash_44_stereo.wav入りmicroSDカード
  
  配線:
  - GPIO12 -> MAX98357A BCLK
  - GPIO13 -> MAX98357A LRC
  - GPIO15 -> MAX98357A DIN
  - 3.3V   -> MAX98357A VIN
  - GND    -> MAX98357A GND
  
  音声素材クレジット:
  - 効果音: cash_44_stereo.wav
  - 提供元: OtoLogic (https://otologic.jp/)
  - ライセンス: 商用・非商用利用可能
  - ライセンス詳細: https://otologic.jp/free/license.html
*****************************************************************/
#include <M5Stack.h>
#include <SD.h>
#include <WiFi.h>
#include "driver/i2s.h"

// ファームウェア情報
#define FW_NAME "Money-Eyes Robot"
#define FW_VERSION "v1.2"
#define FW_BUILD "I2S Audio Edition"
#define FW_DATE "2025-08-11"

// I2S設定
#define I2S_NUM         I2S_NUM_0
#define I2S_SAMPLE_RATE 44100
#define I2S_SAMPLE_BITS 16
#define I2S_CHANNELS    2

// MAX98357A接続ピン（内部回路干渉回避）
#define I2S_BCK_IO      (12)  // BCLK
#define I2S_WS_IO       (13)  // LRC
#define I2S_DO_IO       (15)  // DIN

/* ===== 目のレイアウト定数 ===== */
const int LEFT_X   = 100;
const int RIGHT_X  = 220;
const int CENTER_Y = 120;
const int R_WHITE  = 55;
const int R_BLACK  = 20;

/* ===== 動作パラメータ ===== */
int curX = 0, curY = 0;          // 現在の瞳オフセット
int tgtX = 0, tgtY = 0;          // 目標オフセット
bool moneyMode = false, prevMoney = false;

unsigned long nextBlink = 0;     // 次のまばたき時刻
unsigned long nextMoney = 0;     // 次の$切替時刻

/* ===== 描画・オーディオ ===== */
TFT_eSprite spr(&M5.Lcd);        // スプライト描画
bool i2sInitialized = false;
bool soundPlayed = false;        // 重複再生防止

/* ===== シンプルなI2S初期化 ===== */
void setupI2S() {
  if (i2sInitialized) {
    i2s_driver_uninstall(I2S_NUM);
    i2sInitialized = false;
  }
  
  // ノイズ対策
  WiFi.mode(WIFI_OFF);
  btStop();
  
  // 内蔵スピーカーピンを無効化
  pinMode(25, INPUT_PULLDOWN);
  pinMode(26, INPUT_PULLDOWN);
  
  // I2Sピンを初期化
  pinMode(I2S_BCK_IO, INPUT);
  pinMode(I2S_WS_IO, INPUT);
  pinMode(I2S_DO_IO, INPUT);
  delay(100);
  
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,          // バッファを多めに確保
    .dma_buf_len = 1024,         // 大きなバッファで安定性重視
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
    Serial.println("I2S install failed");
    return;
  }
  
  if (i2s_set_pin(I2S_NUM, &pin_config) != ESP_OK) {
    Serial.println("I2S pin failed");
    return;
  }
  
  if (i2s_set_clk(I2S_NUM, I2S_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO) != ESP_OK) {
    Serial.println("I2S clk failed");
    return;
  }
  
  // DMAバッファクリアと初期化
  i2s_zero_dma_buffer(I2S_NUM);
  delay(100);
  
  // 無音データでバッファを満たす（プチ音防止）
  int16_t silence[2048] = {0};
  size_t bytesWritten;
  for (int i = 0; i < 15; i++) {  // 回数を増やして確実にバッファを満たす
    i2s_write(I2S_NUM, silence, sizeof(silence), &bytesWritten, 1000);
    delay(5);  // 各書き込み間に待機
  }
  
  i2sInitialized = true;
  Serial.println("I2S initialized successfully");
}

/* ===== 音声再生（OtoLogic効果音） ===== */
// 効果音: cash_44_stereo.wav
// 提供元: OtoLogic (https://otologic.jp/)
// ライセンス: 商用・非商用利用可能
// ライセンス詳細: https://otologic.jp/free/license.html

void playWAVFile() {
  if (!i2sInitialized || soundPlayed) return;
  
  File audioFile = SD.open("/cash_44_stereo.wav");
  if (!audioFile) {
    Serial.println("WAV file not found");
    return;
  }
  
  Serial.println("Playing WAV file...");
  
  // WAVヘッダーをスキップ
  audioFile.seek(44);
  
  // バッファを大きくして一気に読み込み
  uint8_t* buffer = (uint8_t*)malloc(4096);
  if (!buffer) {
    Serial.println("Memory allocation failed");
    audioFile.close();
    return;
  }
  
  size_t bytesWritten;
  unsigned long startTime = millis();
  
  while (audioFile.available() && (millis() - startTime < 5000)) {  // 最大5秒
    int bytesRead = audioFile.read(buffer, 4096);
    if (bytesRead > 0) {
      // 4バイト境界に調整
      bytesRead = (bytesRead / 4) * 4;
      
      esp_err_t result = i2s_write(I2S_NUM, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
      if (result != ESP_OK) {
        Serial.printf("I2S write error: %d\n", result);
        break;
      }
    }
    
    // 短時間のyieldでWatchdog回避
    if ((millis() - startTime) % 100 == 0) {
      yield();
    }
  }
  
  // 終了処理：段階的フェードアウト
  int16_t fadeBuffer[256];
  
  // 1. 短い無音でバッファをクリア
  memset(fadeBuffer, 0, sizeof(fadeBuffer));
  for (int i = 0; i < 3; i++) {
    i2s_write(I2S_NUM, fadeBuffer, sizeof(fadeBuffer), &bytesWritten, 100);
    delay(5);
  }
  
  // 2. DMAバッファを段階的にクリア
  i2s_zero_dma_buffer(I2S_NUM);
  delay(50);
  
  // 3. 追加の無音データで完全に消音
  for (int i = 0; i < 5; i++) {
    i2s_write(I2S_NUM, fadeBuffer, sizeof(fadeBuffer), &bytesWritten, 50);
    delay(10);
  }
  
  // 4. 最終的なDMAクリア
  i2s_zero_dma_buffer(I2S_NUM);
  delay(20);
  
  free(buffer);
  audioFile.close();
  soundPlayed = true;
  
  Serial.println("WAV playback completed");
}

/* ===== 目１フレーム描画 ===== */
void drawEyes(int offX, int offY, bool closed = false) {
  spr.fillScreen(TFT_BLACK);

  /* 白目 */
  spr.fillCircle(LEFT_X, CENTER_Y, R_WHITE, TFT_WHITE);
  spr.fillCircle(RIGHT_X, CENTER_Y, R_WHITE, TFT_WHITE);

  if (closed) { /* まばたき */
    spr.fillRect(LEFT_X - 30, CENTER_Y - 5, 60, 10, TFT_BLACK);
    spr.fillRect(RIGHT_X - 30, CENTER_Y - 5, 60, 10, TFT_BLACK);
  }
  else if (moneyMode) { /* $ モード */
    spr.setTextDatum(MC_DATUM);
    spr.setTextSize(5);
    spr.setTextColor(TFT_BLACK, TFT_WHITE);
    spr.drawString("$", LEFT_X + offX, CENTER_Y + offY);
    spr.drawString("$", RIGHT_X + offX, CENTER_Y + offY);
  }
  else { /* 通常黒目 */
    spr.fillCircle(LEFT_X + offX, CENTER_Y + offY, R_BLACK, TFT_BLACK);
    spr.fillCircle(RIGHT_X + offX, CENTER_Y + offY, R_BLACK, TFT_BLACK);
  }

  spr.pushSprite(0, 0);

  /* $ になった瞬間に効果音（1回のみ） */
  if (moneyMode && !prevMoney && !soundPlayed) {
    playWAVFile();
  }
  
  // $モードが終わったらサウンドフラグをリセット
  if (!moneyMode && prevMoney) {
    soundPlayed = false;
  }
  
  prevMoney = moneyMode;
}

/* ===== FW情報表示 ===== */
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
  
  delay(5000);  // 5秒間表示
}

/* ===== setup ===== */
void setup() {
  M5.begin();
  M5.Power.begin();
  Serial.begin(115200);
  
  // ファームウェア情報を表示
  showFirmwareInfo();
  
  // ノイズ対策
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
  
  /* --- SD 初期化 --- */
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
  
  /* --- I2S初期化 --- */
  M5.Lcd.println("Init I2S...");
  setupI2S();
  
  if (i2sInitialized) {
    M5.Lcd.println("I2S OK");
  } else {
    M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
    M5.Lcd.println("I2S Failed!");
  }
  
  delay(2000);
  
  /* --- スプライト初期化 --- */
  spr.setColorDepth(8);
  spr.createSprite(320, 240);
  drawEyes(0, 0);

  /* --- 乱数シード & タイマ初期化 --- */
  randomSeed(analogRead(0));
  nextBlink = millis() + random(2000, 5000);
  nextMoney = millis() + random(4000, 8000);
  
  M5.Lcd.clear();
  Serial.printf("Setup completed - %s %s\n", FW_NAME, FW_VERSION);
}

/* ===== loop ===== */
void loop() {
  unsigned long now = millis();

  /* まばたき */
  if (now >= nextBlink) {
    drawEyes(curX, curY, true);  
    delay(120);
    drawEyes(curX, curY, false);
    nextBlink = now + random(2000, 5000);
  }

  /* $ 切替 */
  if (now >= nextMoney) {
    moneyMode = !moneyMode;
    nextMoney = now + random(4000, 8000);
  }

  /* 視線目標決定 */
  if (curX == tgtX && curY == tgtY) {
    tgtX = random(-30, 31);
    tgtY = random(-15, 16);
  }

  /* 1 px ずつ移動 */
  if (curX < tgtX) curX++; else if (curX > tgtX) curX--;
  if (curY < tgtY) curY++; else if (curY > tgtY) curY--;

  drawEyes(curX, curY);

  delay(10);
}
