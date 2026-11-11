// §: WristMate_Orbit
/*
u22第２回アップロード提出状態
第一回アップロードと変わった詳細は、"protpedia"に追加しておきました。
第一回アップロードと変わった詳細は、"protpedia"に追加しておきました。
第一回アップロードと変わった詳細は、"protpedia"に追加しておきました。
第一回アップロードと変わった詳細は、"protpedia"に追加しておきました。
*/
// §: Global
#include <Wire.h>
#include "esp_system.h"
#include <Preferences.h>
#include <time.h>
#include <esp_sleep.h>
#include <math.h>
#include <string.h>
#include <driver/rtc_io.h>
#include <ESP32Encoder.h>

// §: Pins
// XIAO ESP32-S3 (D0=GPIO1, D1=GPIO2, D2=GPIO3, D3=GPIO4, D4=GPIO5, D5=GPIO6, D6=GPIO43, D7=GPIO44, D8=GPIO7, D9=GPIO8, D10=GPIO9)
const int SW_PIN = D10;
const int CDS_PIN = D8;
const int VIBES_PIN = D5; // high:ON, low:OFF
const int RTC_INT_PIN = D9;
const int LED_PIN = D1; // high:OFF, low:ON
// #define WRIST_CALIB_MODE // 有効にすると加速度を流し続ける実測モードで起動する
const int ACC_INT1_PIN = D0;
const int IS_EN_PIN = D2;
const int SDA_PIN = D3;
const int SCL_PIN = D4;
const int ENC_A_PIN = D6; // GPIO43: RTCピンではないのでエンコーダでは起床できない
const int ENC_B_PIN = D7; // GPIO44
const int SW_WAKE_PIN = SW_PIN;
unsigned long IDLE_DEEP_SLEEP_MS = 8000; // スリープ時間は設定画面で変更される
// const unsigned long IDLE_DEEP_SLEEP_MS = 600000;
unsigned long MENU_DEEP_SLEEP_MS = 16000;
unsigned long CONTENT_DEEP_SLEEP_MS = 20000; // コンテンツ内はMENUの2倍の無操作でスリープ
bool isLedOn = false;
RTC_DATA_ATTR bool isAntiNapEnabled = false;
bool isStopwatchRunning = false;
long lastStopWatchMsPart = -1;
long stopWatchStartMillis = -1;
RTC_DATA_ATTR int lastDay = -1;

const int DEFAULT_VIBE = 155;

// §: Morse table
const char *MORSE_TABLE[36] = {
    "-----", // 0
    ".----", // 1
    "..---", // 2
    "...--", // 3
    "....-", // 4
    ".....", // 5
    "-....", // 6
    "--...", // 7
    "---..", // 8
    "----.", // 9
    ".-",    // A
    "-...",  // B
    "-.-.",  // C
    "-..",   // D
    ".",     // E
    "..-.",  // F
    "--.",   // G
    "....",  // H
    "..",    // I
    ".---",  // J
    "-.-",   // K
    ".-..",  // L
    "--",    // M
    "-.",    // N
    "---",   // O
    ".--.",  // P
    "--.-",  // Q
    ".-.",   // R
    "...",   // S
    "-",     // T
    "..-",   // U
    "...-",  // V
    ".--",   // W
    "-..-",  // X
    "-.--",  // Y
    "--.."}; // Z
char MORSE_TEXT[] = "0123456789abcdefghijklmnopqrstuvwxyz";
int nowMorseIndex = 0;
int nowMorseDotIndex = 0;
bool isDoneNowMorse = true;
int dotMs = 300;

RTC_DATA_ATTR bool isManner = false;
// §: Contents, status settings
enum Status
{
  IDLE,
  MENU,
  CONTENT
};
Status nowStatus = IDLE;
// String MenusName[] = {
//     "STEPS",
//     "DIET",
//     "ANTI_NAP",
//     "LIGHT",
//     "STOP_WATCH",
//     "MORSE"};
// *  CONTENTS DEFINE
#define MENU_LIST(X)                                                  \
  X(TRAIN)                                                            \
  X(STEPS)                                                            \
  X(DIET)                                                             \
  X(MANNER)                                                           \
  X(ANTI_NAP)                                                         \
  X(LIGHT)                                                            \
  X(STOP_WATCH)                                                       \
  X(COUNT)                                                            \
  X(POMODORO)                                                         \
  X(ROULETTE)                                                         \
  X(GAME)                                                             \
  X(MORSE)                                                            \
  X(SOS)                                                              \
  X(BLUETOOTH)                                                        \
  X(UPLOAD)  /* 書き込み・転送用。この中ではディープスリープしない */ \
  X(SETTING) /* スマホ設定(Wi-Fi)。常に最後に出る。順番とオン/オフの対象外 */
enum Menu
{
#define X(name) name,
  MENU_LIST(X)
#undef X
};

// メニュー名
const char *MenusName[] = {
#define X(name) #name,
    MENU_LIST(X)
#undef X
};

int nowMenuIndex = 0;
// §: Individual define
// スイッチの押し方イベント定義
enum SwitchEvent
{
  SW_NONE,   // 何もなし
  SW_SINGLE, // 短押し
  SW_DOUBLE, // ダブルタップ(今は検出しない)
  SW_LONG    // 長押し
};
// スイッチ判定用の設定定数 (ミリ秒)
const unsigned long LONG_PRESS_MS = 800;      // 長押し判定時間 (0.8秒以上)
const unsigned long DOUBLE_TAP_MS = 250;      // ダブルタップ受入時間 (0.3秒以内)
const unsigned long RELEASE_DEBOUNCE_MS = 40; // 離したと確定するまでの安定時間
const unsigned long CHATTERING_MS = 20;       // チャタリング防止時間

// §: BMA400
#include "SparkFun_BMA400_Arduino_Library.h"
BMA400 accelerometer;
// #define SLEEP_TIME_SEC 30 // watchdog
RTC_DATA_ATTR uint32_t wakeCount = 0;
RTC_DATA_ATTR uint32_t bmaStepCount = 0;
RTC_DATA_ATTR uint32_t totalStepCount = 0;
RTC_DATA_ATTR uint32_t totalKcal = 0;
uint32_t lastBmaStepCount = -1;
volatile bool bma400InterruptOccurred = false;
volatile bool bma400ActivityOccurred = false;
bool fallDetectOn = false; // 転倒検知のため加速度を100Hzで見ている間 true
Preferences preferences;
unsigned long lastIdleActivityMs = 0;
unsigned long lastStatusChangeMs = 0;

// §: Firmware ID / 歩数・kcal
const char *FW_BUILD_ID = __DATE__ " " __TIME__;
const char *FW_PREF_NS = "wristmate";
const char *FW_PREF_KEY = "last_build";
const char *FW_ARM_KEY = "armed_build";
const char *FW_REMAIN_KEY = "sync_remaining";

const long GMT_OFFSET_SEC = 9 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;
const float USER_WEIGHT_KG = 62.0f;
const float USER_HEIGHT_CM = 162.0f;

float kcalPerStep()
{
  // 62kg/162cm を基準にしたざっくり推定係数
  // 身長は歩幅の目安として少しだけ効かせる
  float weightFactor = USER_WEIGHT_KG / 62.0f;
  float heightFactor = USER_HEIGHT_CM / 162.0f;
  return 0.04f * weightFactor * (0.5f + 0.5f * heightFactor);
}

long wakeupLog[10] = {0};

void printBma400Accel()
{
  if (accelerometer.getSensorData() != BMA400_OK)
  {
    Serial.println("BMA400 accel read failed");
    return;
  }

  Serial.print("Accel g's  X: ");
  Serial.print(accelerometer.data.accelX, 3);
  Serial.print("  Y: ");
  Serial.print(accelerometer.data.accelY, 3);
  Serial.print("  Z: ");
  Serial.println(accelerometer.data.accelZ, 3);
}
bool syncBmaStepCount(bool forcePrint = false)
{
  uint32_t sensorStepCount = 0;
  uint8_t activityType = 0;
  uint32_t previousStepCount = bmaStepCount;

  if (accelerometer.getStepCount(&sensorStepCount, &activityType) != BMA400_OK)
  {
    Serial.println("BMA400 step count read failed");
    return false;
  }

  bmaStepCount = sensorStepCount;
  Serial.printf("Step count: %lu (delta: %+ld)\n",
                (unsigned long)bmaStepCount,
                (long)bmaStepCount - (long)previousStepCount);
  Serial.print("Step activity: ");
  switch (activityType)
  {
  case BMA400_RUN_ACT:
    Serial.println("Running");
    break;
  case BMA400_WALK_ACT:
    Serial.println("Walking");
    break;
  case BMA400_STILL_ACT:
    Serial.println("Standing still");
    break;
  default:
    Serial.println("Unknown");
    break;
  }
  return forcePrint || (sensorStepCount != previousStepCount);
}
bool resetBmaStepCount()
{
  // BMA400 step counter clear command: 0xB1 to command register 0x7E
  Wire.beginTransmission(BMA400_I2C_ADDRESS_DEFAULT);
  Wire.write(0x7E);
  Wire.write(0xB1);
  return Wire.endTransmission() == 0;
}
bool syncAndResetStepCount(bool forcePrint = false)
{
  uint32_t sensorStepCount = 0;
  uint8_t activityType = 0;

  if (accelerometer.getStepCount(&sensorStepCount, &activityType) != BMA400_OK)
  {
    Serial.println("BMA400 step count read failed");
    return false;
  }

  if (sensorStepCount == 0 && !forcePrint)
  {
    return false;
  }

  totalStepCount += sensorStepCount;
  totalKcal += (uint32_t)(sensorStepCount * kcalPerStep() + 0.5f);
  bmaStepCount = sensorStepCount;
  Serial.printf("Step count read: %lu, total: %lu, kcal: %lu, activity: %u\n",
                (unsigned long)sensorStepCount,
                (unsigned long)totalStepCount,
                (unsigned long)totalKcal,
                (unsigned)activityType);

  if (!resetBmaStepCount())
  {
    Serial.println("BMA400 step count reset failed");
    return false;
  }

  Serial.println("BMA400 step count reset ok");

  return true;
}
// §: RTC
#define RV3028_ADDR 0x52
int year;
int month;
int day;
int hour = 7;
int minute = 10;
int second = 0;
int weekday = 0;
int lastMinute = -1;
int lastSecond = -1;
long lastMillis = -1;
// 最後にsleepに入った時間 hour*60*60 + minute*60 + second
RTC_DATA_ATTR long lastSleepTime;
// 日付が変わっていたら歩数とkcalを0に戻す（rtcPrint()でdayを読んだ後に呼ぶ）
void orbitApplyDailyDrift();        // OrbitConfig.h
void orbitBootPreview(uint8_t pat); // OrbitConfig.h(設定画面で起動パターンを選んだ時に再生)
void resetDailyCountIfNewDay()
{
  if (day < 1 || day > 31 || day == lastDay)
  {
    return; // RTC未読込(0)などの不正値では何もしない
  }
  if (lastDay != -1)
  {
    Serial.printf("New day (%d -> %d), reset steps: %lu, kcal: %lu\n",
                  lastDay, day,
                  (unsigned long)totalStepCount,
                  (unsigned long)totalKcal);
    totalStepCount = 0;
    totalKcal = 0;
    orbitApplyDailyDrift(); // 記録したドリフトを日に1回RTCへ反映
  }
  lastDay = day;
}

long lastChangeSecondMillis = -1; // 最後にrv3028の秒が変わった時のmillis()値    コロンを更新するために

long nowMillis;
int lastWireMillis;
const uint8_t RV3028_CTRL1_REG = 0x0F;
const uint8_t RV3028_STATUS_REG = 0x0E;
const uint8_t RV3028_EECMD_REG = 0x27;
const uint8_t RV3028_EE_BACKUP_REG = 0x37;
const uint8_t RV3028_EERD_BIT = 1 << 3;
const uint8_t RV3028_EEBUSY_BIT = 1 << 7;
const uint8_t RV3028_BSM_MASK = 0b11 << 2;
const uint8_t RV3028_TCE_BIT = 1 << 5;
const uint8_t RV3028_FEDE_BIT = 1 << 4;
const uint8_t RV3028_TE_BIT = 1 << 2;
const uint8_t RV3028_TD_MASK = 0b11;
const uint8_t RV3028_TD_1HZ = 0b10;
const uint8_t RV3028_CTRL2_REG = 0x10;
const uint8_t RV3028_TIE_BIT = 1 << 4;
const uint8_t RV3028_TIMER_VAL0_REG = 0x0A;
const uint8_t RV3028_TIMER_VAL1_REG = 0x0B;
const uint8_t RV3028_TF_BIT = 1 << 3;
uint8_t decToBcd(uint8_t v)
{
  return ((v / 10) << 4) | (v % 10);
}
uint8_t bcdToDec(uint8_t v)
{
  return ((v >> 4) * 10) + (v & 0x0F);
}
void rtcWrite(uint8_t reg, uint8_t val)
{
  for (int attempt = 0; attempt < 3; attempt++)
  {
    Wire.beginTransmission(RV3028_ADDR);
    Wire.write(reg);
    Wire.write(val);
    if (Wire.endTransmission() == 0)
    {
      return;
    }
    delay(2);
  }
  Serial.printf("RTC write failed (reg 0x%02X)\n", reg);
}
// 読み出しに失敗したら数回リトライする。それでもだめなら0を返す。
uint8_t rtcRead(uint8_t reg)
{
  for (int attempt = 0; attempt < 3; attempt++)
  {
    Wire.beginTransmission(RV3028_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(RV3028_ADDR, 1) == 1 && Wire.available())
    {
      return Wire.read();
    }
    delay(2);
  }
  Serial.printf("RTC read failed (reg 0x%02X)\n", reg);
  return 0;
}
// RTCが応答しなくても永久に待たない(約10秒で諦めて先へ進み、スリープできる状態を保つ)。
bool waitForRtcI2c()
{
  for (int attempt = 0; attempt < 20; attempt++)
  {
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setTimeOut(50); // I2Cがハングしてもloop()が戻らなくならないように
    Wire.beginTransmission(RV3028_ADDR);
    uint8_t error = Wire.endTransmission();
    if (error == 0)
    {
      return true;
    }

    Serial.printf("RV3028 I2C not responding (error %u); retrying...\n", error);
    Wire.end();
    delay(500);
  }
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeOut(50);
  return false;
}
/*

// §: RTC Wi-Fi sync
const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
} */
void rtcSetEERD(bool enabled)
{
  uint8_t control1 = rtcRead(RV3028_CTRL1_REG);
  if (enabled)
  {
    control1 |= RV3028_EERD_BIT;
  }
  else
  {
    control1 &= ~RV3028_EERD_BIT;
  }
  rtcWrite(RV3028_CTRL1_REG, control1);
}
bool rtcWaitEepromReady(uint16_t timeoutMs = 100)
{
  unsigned long startMillis = millis();
  while (millis() - startMillis < timeoutMs)
  {
    if ((rtcRead(RV3028_STATUS_REG) & RV3028_EEBUSY_BIT) == 0)
    {
      return true;
    }
    delay(2);
  }
  return false;
}
/*
bool syncRtcFromWiFi()
{
  Serial.println("WiFi NTP sync start");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm); // ←これを入れる
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long connectStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - connectStart < 15000)
  {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi connect failed");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  Serial.print("WiFi status: ");
  Serial.println((int)WiFi.status());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  configTzTime("JST-9", "pool.ntp.org", "time.google.com", "ntp.nict.jp");

  struct tm timeinfo;
  bool timeReady = false;
  for (int i = 0; i < 60; i++)
  {
    if (getLocalTime(&timeinfo, 500))
    {
      timeReady = true;
      break;
    }
    delay(500);
  }

  if (!timeReady)
  {
    time_t now = time(nullptr);
    Serial.printf("NTP time fetch failed, epoch=%ld\n", (long)now);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  int yearValue = timeinfo.tm_year + 1900;
  int monthValue = timeinfo.tm_mon + 1;
  int dayValue = timeinfo.tm_mday;
  int hourValue = timeinfo.tm_hour;
  int minuteValue = timeinfo.tm_min;
  int secondValue = timeinfo.tm_sec;
  int weekdayValue = timeinfo.tm_wday == 0 ? 7 : timeinfo.tm_wday;

  rtcSetTime(yearValue, monthValue, dayValue, hourValue, minuteValue, secondValue, weekdayValue);
  Serial.printf("RTC updated from NTP: %04d-%02d-%02d %02d:%02d:%02d\n",
                yearValue, monthValue, dayValue,
                hourValue, minuteValue, secondValue);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return true;
}
*/
void rtcConfigureBackupSwitchover()
{
  rtcSetEERD(true);

  if (!rtcWaitEepromReady())
  {
    Serial.println("RTC EEPROM busy, backup config skipped");
    rtcSetEERD(false);
    return;
  }

  uint8_t backupConfig = rtcRead(RV3028_EE_BACKUP_REG);
  uint8_t updatedConfig = backupConfig;

  // バックアップ電源を使う前提で、switchover を有効にする。
  // 1.5V バックアップ電源を保持用に使うなら、DSM/LSM のどちらかが必要。
  updatedConfig &= ~RV3028_BSM_MASK;
  updatedConfig |= (0b01 << 2);     // Direct Switching Mode
  updatedConfig &= ~RV3028_TCE_BIT; // 充電経路は使わない
  updatedConfig |= RV3028_FEDE_BIT; // Fast Edge Detection は有効のままにする

  if (updatedConfig != backupConfig)
  {
    rtcWrite(RV3028_EE_BACKUP_REG, updatedConfig);
    delay(10);

    rtcWrite(RV3028_EECMD_REG, 0x00);
    rtcWrite(RV3028_EECMD_REG, 0x11);
    delay(10);
  }

  rtcSetEERD(false);
}
void rtcSetTime(int y, int m, int d, int h, int min, int s, int w)
{
  rtcWrite(0x00, decToBcd(s));
  rtcWrite(0x01, decToBcd(min));
  rtcWrite(0x02, decToBcd(h));
  rtcWrite(0x03, w);
  rtcWrite(0x04, decToBcd(d));
  rtcWrite(0x05, decToBcd(m));
  rtcWrite(0x06, decToBcd(y - 2000));
} // 指定秒後にRV3028のカウントダウンタイマー割り込みが発生するようにセット
// (1Hzクロック、Single Mode = 満了したら自動停止)
void rtcArmWakeTimer(uint16_t seconds)
{
  if (seconds > 4095)
    seconds = 4095; // 12bitまで

  // 一旦止める
  uint8_t ctrl1 = rtcRead(RV3028_CTRL1_REG);
  ctrl1 &= ~RV3028_TE_BIT;
  rtcWrite(RV3028_CTRL1_REG, ctrl1);

  // タイマー値セット (1Hzクロック)
  rtcWrite(RV3028_TIMER_VAL0_REG, seconds & 0xFF);
  rtcWrite(RV3028_TIMER_VAL1_REG, (seconds >> 8) & 0x0F);

  // クロック周波数=1Hz, Single Mode(TRPT=0)
  ctrl1 = rtcRead(RV3028_CTRL1_REG);
  ctrl1 &= ~(0b10000000 | RV3028_TD_MASK); // TRPT=0, TDクリア
  ctrl1 |= RV3028_TD_1HZ;
  rtcWrite(RV3028_CTRL1_REG, ctrl1);

  // Timer Flag(TF)を先にクリア
  uint8_t status = rtcRead(RV3028_STATUS_REG);
  status &= ~RV3028_TF_BIT;
  rtcWrite(RV3028_STATUS_REG, status);

  // Timer Interrupt Enable
  uint8_t ctrl2 = rtcRead(RV3028_CTRL2_REG);
  ctrl2 |= RV3028_TIE_BIT;
  rtcWrite(RV3028_CTRL2_REG, ctrl2);

  // タイマー開始
  ctrl1 = rtcRead(RV3028_CTRL1_REG);
  ctrl1 |= RV3028_TE_BIT;
  rtcWrite(RV3028_CTRL1_REG, ctrl1);
}
// 起床後の後始末：TFフラグとTIEをクリアしてINTピンを解放
bool rtcConsumeTimerFlag()
{
  uint8_t status = rtcRead(RV3028_STATUS_REG);
  bool wasTimerFired = (status & RV3028_TF_BIT) != 0;

  status &= ~RV3028_TF_BIT;
  rtcWrite(RV3028_STATUS_REG, status);

  uint8_t ctrl2 = rtcRead(RV3028_CTRL2_REG);
  ctrl2 &= ~RV3028_TIE_BIT;
  rtcWrite(RV3028_CTRL2_REG, ctrl2);

  return wasTimerFired;
}
void rtcDebugPrint()
{
  int debugWeekday = bcdToDec(rtcRead(0x03));
  int debugSecond = bcdToDec(rtcRead(0x00));
  int debugMinute = bcdToDec(rtcRead(0x01));
  int debugHour = bcdToDec(rtcRead(0x02));
  int debugDay = bcdToDec(rtcRead(0x04));
  int debugMonth = bcdToDec(rtcRead(0x05));
  int debugYear = bcdToDec(rtcRead(0x06));

  Serial.printf("RTC readback: 20%02d-%02d-%02d %02d:%02d:%02d %d\n",
                debugYear, debugMonth, debugDay,
                debugHour, debugMinute, debugSecond,
                debugWeekday);
}
void rtcPrint()
{
  if (nowMillis - lastWireMillis > 30)
  {
    // 秒〜年を1回の通信でまとめて読む(別々に読むと繰り上がりや読み取り失敗で時刻がずれる)
    Wire.beginTransmission(RV3028_ADDR);
    Wire.write((uint8_t)0x00);
    Wire.endTransmission(false);
    if (Wire.requestFrom(RV3028_ADDR, 7) == 7)
    {
      uint8_t r[7];
      for (int i = 0; i < 7; i++)
      {
        r[i] = Wire.read();
      }
      int newSecond = bcdToDec(r[0]);
      int newMinute = bcdToDec(r[1]);
      int newHour = bcdToDec(r[2]);
      if (newSecond < 60 && newMinute < 60 && newHour < 24)
      {
        second = newSecond;
        minute = newMinute;
        hour = newHour;
        weekday = bcdToDec(r[3]);
        day = bcdToDec(r[4]);
        month = bcdToDec(r[5]);
        year = bcdToDec(r[6]);
      }
    }

    if (nowMillis - lastWireMillis > 100)
    {
      Serial.printf("20%02d-%02d-%02d %02d:%02d:%02d %d\n", year, month, day, hour, minute, second, weekday);
    }
    lastWireMillis = nowMillis;
  }
}
bool shouldSyncRtcOnThisPowerOn()
{
  preferences.begin(FW_PREF_NS, false);
  String lastBuild = preferences.getString(FW_PREF_KEY, "");
  int remaining = preferences.getInt(FW_REMAIN_KEY, 0);
  bool isNewUpload = (lastBuild != FW_BUILD_ID);

  Serial.print("Build ID: ");
  Serial.println(FW_BUILD_ID);
  Serial.print("Last build: ");
  Serial.println(lastBuild);
  Serial.print("Remaining sync count: ");
  Serial.println(remaining);

  if (isNewUpload)
  {
    preferences.putString(FW_PREF_KEY, FW_BUILD_ID);
    preferences.putInt(FW_REMAIN_KEY, 3);
    remaining = 3;
    Serial.println("Upload armed for next 3 power-ons");
  }

  bool shouldSync = (remaining > 0);
  if (shouldSync)
  {
    remaining--;
    preferences.putInt(FW_REMAIN_KEY, remaining);
    if (remaining == 0)
    {
      preferences.remove(FW_REMAIN_KEY);
    }
  }

  preferences.end();
  return shouldSync;
}

// §: Wrist pose wake
// 手首を上げる姿勢の基準値。±4gレンジ・12bitで 1g = 512LSB。
// 実測(左手首): 画面を見る姿勢 X≈-0.35g Y≈-0.07g Z≈-1.0g / 机上タイピング X≈+0.15g Y≈-0.23g Z≈-1.04g
const int16_t WRIST_POSE_REF_X_RAW = -179;
const int16_t WRIST_POSE_REF_Y_RAW = -36;
const int16_t WRIST_POSE_REF_Z_RAW = -512;
const uint8_t WRIST_POSE_THRES_LSB = 31;         // 1LSB = 8mg なので約±0.25g
const uint16_t WRIST_POSE_DURATION_SAMPLES = 10; // 25Hzで約0.4秒保たれたら起床
const unsigned long WRIST_POSE_RELEASE_MS = 300; // スリープ前に姿勢が外れていると見なす時間

// 「動いてから静止」方式の手首起床。姿勢に依存しない。
// BMA400のGeneric2(Activity・基準値は毎サンプル更新)=サンプル間の加速度変化が閾値超でESPを起こし、
// 起きた後に25Hzで測って WRIST_HOLD_MS の間向きが安定していれば点灯、静止しなければ寝直す。
const uint8_t WRIST_MOVE_THRES_LSB = 16;          // 1LSB=8mg → 約0.13g/サンプル。小さいほど軽い動きで起きる(電池を食う)
const uint16_t WRIST_MOVE_DURATION_SAMPLES = 2;   // 25Hzで約80ms続いたら起床
const unsigned long WRIST_HOLD_MS = 800;          // この間静止していたら「ホールドされた」とみなす
const unsigned long WRIST_HOLD_TIMEOUT_MS = 3500; // 起きてからこの時間内に満たさなければ寝直す(平均を溜める分も含む)
const int WRIST_HOLD_AVG_SAMPLES = 12;            // 揺れをならす平均のサンプル数(25Hzで約0.5秒。歩行の1歩ぶんで揺れを打ち消す)
const float WRIST_HOLD_RANGE_G = 0.35f;           // WRIST_HOLD_MS の間、平均した向きの変化幅がこれ未満ならホールド
const float WRIST_HOLD_Z_MAX = -0.3f;             // Z がこれより下(画面が上〜顔側)でないと点灯しない。ポケット内などの誤点灯対策

// Apple Watch風の速い点灯。スリープ中もBMA400のFIFOに25Hzで加速度を溜めておき、起きた直後に
// 「直前WRIST_RAISE_LOOKBACK_N サンプルの間に向きが大きく変わって、今は画面を見る向きで止まっている」なら
// 静止0.8秒を待たずに点灯する。満たさない時は従来の静止判定(上のWRIST_HOLD_*)にまかせる。
const int WRIST_RAISE_HIST_N = 64;              // 判定に使う履歴の長さ(FIFO+起床後。25Hzで約2.5秒)
const int WRIST_RAISE_LOOKBACK_N = 50;          // 向きの変化を探す範囲(25Hzで約2秒前まで)
const int WRIST_RAISE_STEADY_N = 5;             // 直近この数のサンプル(約0.2秒)が揃っていれば「止まった」
const float WRIST_RAISE_STEADY_RANGE_G = 0.22f; // 止まったとみなす各軸の変化幅(2サンプル平均後)。歩行中に見る時の揺れを許す
const float WRIST_RAISE_VIEW_Z_MAX = -0.5f;     // 止まった向きのZがこれより下(画面が顔側)なら見る姿勢
const float WRIST_RAISE_MIN_ANGLE_DEG = 35.0f;  // 直前の向きとの差がこれ以上あれば「腕を上げた」
const bool WRIST_SLOW_FALLBACK = true;          // false にすると、上げる動きのない時(机上など)は点灯しない

// 腕を下ろしたら消す(時計表示中のみ)。見ている向きで止まった時の向きから WRIST_DOWN_ANGLE_DEG 以上
// 傾いた状態が WRIST_DOWN_MS 続いたらすぐスリープする。詳しくは wristLoweredCheck()。
const float WRIST_DOWN_ANGLE_DEG = 40.0f;
const float WRIST_DOWN_MOVED_ANGLE_DEG = 25.0f; // 直前 WRIST_DOWN_MOVE_WINDOW_MS に大きく動いていれば、この角度で下ろしたとみなす
const float WRIST_DOWN_MOVE_G = 0.3f;           // 「大きく動いた」= なめらかにした値からのずれ(机に手を置く時の30〜40°対策)
const unsigned long WRIST_DOWN_MOVE_WINDOW_MS = 1000;
const float WRIST_DOWN_STEADY_G = 0.08f; // 基準を取る時の「止まっている」(なめらかにした値の変化幅)
const unsigned long WRIST_DOWN_MS = 320;
const unsigned long WRIST_DOWN_SAMPLE_MS = 40;

void IRAM_ATTR bma400InterruptHandler()
{
  bma400InterruptOccurred = true;
}
void IRAM_ATTR bma400ActivityInterruptHandler()
{
  bma400ActivityOccurred = true;
}
bool configureBma400ActivityWakeup()
{
  bool found = false;
  for (int i = 0; i < 5 && !found; i++)
  {
    found = (accelerometer.beginI2C() == BMA400_OK);
    if (!found)
    {
      Serial.println("BMA400が見つかりません");
      delay(500);
    }
  }
  if (!found)
  {
    return false; // センサー無しでもスリープ処理まで進めるようにする
  }

  Serial.println("BMA400接続成功");

  accelerometer.setODR(BMA400_ODR_25HZ);
  accelerometer.setRange(BMA400_RANGE_4G);

  // 動き(サンプル間の加速度変化)を検出してESPを起こす。静止の確認は起床後に wristHoldConfirm() で行う。
  // Generic2のActivity、基準値は毎サンプル更新=直前サンプルとの差で判定する(姿勢に依存しない)。
  accelerometer.enableInterrupt(BMA400_GEN1_INT_EN, false);
  bma400_gen_int_conf genConfig = {};
  genConfig.gen_int_thres = WRIST_MOVE_THRES_LSB;
  genConfig.gen_int_dur = WRIST_MOVE_DURATION_SAMPLES;
  genConfig.axes_sel = BMA400_AXIS_X_EN | BMA400_AXIS_Y_EN | BMA400_AXIS_Z_EN;
  genConfig.data_src = BMA400_DATA_SRC_ACC_FILT1;
  genConfig.criterion_sel = BMA400_ACTIVITY_INT;
  genConfig.evaluate_axes = BMA400_ANY_AXES_INT;
  genConfig.ref_update = BMA400_UPDATE_EVERY_TIME;
  genConfig.hysteresis = BMA400_HYST_0_MG;
  genConfig.int_chan = BMA400_INT_CHANNEL_1;

  accelerometer.setGeneric2Interrupt(&genConfig);
  accelerometer.setInterruptPinMode(
      BMA400_INT_CHANNEL_1,
      BMA400_INT_PUSH_PULL_ACTIVE_0);

  accelerometer.enableInterrupt(BMA400_GEN2_INT_EN, true);

  // 歩数カウンタも有効化
  bma400_step_int_conf stepConfig = {
      .int_chan = BMA400_UNMAP_INT_PIN};
  accelerometer.setStepCounterInterrupt(&stepConfig);
  accelerometer.enableInterrupt(BMA400_STEP_COUNTER_INT_EN, true);

  pinMode(ACC_INT1_PIN, INPUT_PULLDOWN);

  attachInterrupt(
      digitalPinToInterrupt(ACC_INT1_PIN),
      bma400ActivityInterruptHandler,
      FALLING);

  Serial.println("手首の動き検出の割り込みを有効化");
  return true;
}
// INT1(Active Low)が出ている間は手首を上げた姿勢のまま。そのままスリープすると即起床するため、
// 腕を下ろして姿勢が外れるまで待つ。
// 姿勢が外れたら true。WRIST_POSE_WAIT_MAX_MS 経っても外れなければ false を返す(固まらないため)。
const unsigned long WRIST_POSE_WAIT_MAX_MS = 3000;
bool waitForWristPoseRelease()
{
  unsigned long releasedSince = 0;
  const unsigned long waitStart = millis();
  while (true)
  {
    if (millis() - waitStart >= WRIST_POSE_WAIT_MAX_MS)
    {
      Serial.println("Wrist pose release timeout");
      return false;
    }
    if (digitalRead(ACC_INT1_PIN) == LOW)
    {
      releasedSince = 0;
    }
    else
    {
      if (releasedSince == 0)
      {
        releasedSince = millis();
      }
      if (millis() - releasedSince >= WRIST_POSE_RELEASE_MS)
      {
        return true;
      }
    }
    delay(20);
  }
}
// 起床直後に呼ぶ。センサーが手首を上げた姿勢の範囲内なら true。
bool bma400InWristPose()
{
  if (accelerometer.getSensorData() != BMA400_OK)
  {
    return true; // 読めない時は起床を妨げない
  }
  const float limit = 0.30f;
  return fabsf(accelerometer.data.accelX - WRIST_POSE_REF_X_RAW / 512.0f) < limit &&
         fabsf(accelerometer.data.accelY - WRIST_POSE_REF_Y_RAW / 512.0f) < limit &&
         fabsf(accelerometer.data.accelZ - WRIST_POSE_REF_Z_RAW / 512.0f) < limit;
}
// §: Wrist raise (FIFO)
// FIFO_CONFIG0 = XYZ・12bit・時刻なし・満杯時は古いものから捨てる。スリープ直前に呼んで中身を空にしておく
// (起きている間の100Hzのデータや設定変更フレームが混ざらないように)。
bool wristFifoRestart()
{
  Wire.beginTransmission(BMA400_I2C_ADDRESS_DEFAULT);
  Wire.write(0x26); // FIFO_CONFIG0
  Wire.write(0xE0); // Z/Y/X 有効
  if (Wire.endTransmission() != 0)
    return false;
  Wire.beginTransmission(BMA400_I2C_ADDRESS_DEFAULT);
  Wire.write(0x7E); // CMD
  Wire.write(0xB0); // fifo_flush
  return Wire.endTransmission() == 0;
}
// 手首の空振り起床から寝直す時は true。FIFOを空にせず、起きていた間の動きも次の起床の判定に使う
// (歩行中は空振り起床が続くので、その間に腕を上げた分を取りこぼさないように)。
bool wristFifoKeep = false;
// 歩行中などで INT1 が出っぱなしのまま寝ると手首起床を外すしかない。その時は RTC で WRIST_RETRY_SEC 後に起き直し、
// FIFOを見て腕を上げていれば点灯、そうでなければ手首起床を付け直して寝る。
const uint16_t WRIST_RETRY_SEC = 2;
RTC_DATA_ATTR bool wristRetryArmed = false;
// 履歴のリングバッファ(古い→新しい)。FIFOの中身と起床後のサンプルを続けて入れる。
float wristHist[WRIST_RAISE_HIST_N][3];
int wristHistCount = 0; // 入れた総数(リングの位置は % WRIST_RAISE_HIST_N)
// §: Sleep fall (FIFO)
// スリープ中の転倒検知。ESPは眠っていて100Hzで見られないので、起床時に読むFIFO(25Hz・ローパス後)と
// 起床直後のサンプルから判定する。転倒の動きで必ず手首起床(INT1)か見直し起床(RTC)が起きるので、その時に拾える。
// 25Hzでローパスが掛かった値なので、起きている間の閾値(FALL_*)より低い。未実測。
const float SLEEP_FALL_FREEFALL_G = 0.5f;              // これ未満=ほぼ無重力
const int SLEEP_FALL_FREEFALL_N = 5;                   // 無重力がこのサンプル数(25Hzで0.2秒)続いたら「落下」
const int SLEEP_FALL_WIN_N = 15;                       // 落下のあと、この数(0.6秒)以内に衝撃が来たら転倒
const float SLEEP_FALL_IMPACT_AFTER_FREEFALL_G = 2.0f; // 落下のあとの衝撃
const float SLEEP_FALL_IMPACT_G = 3.5f;                // 落下を見ていなくても、これ以上なら転倒
bool sleepFallSeen = false;
int sleepFallLowRun = 0;
int sleepFallSinceLow = 1000; // 落下が終わってからのサンプル数
void sleepFallFeed(float x, float y, float z)
{
  float g = sqrtf(x * x + y * y + z * z);
  if (g < SLEEP_FALL_FREEFALL_G)
  {
    if (++sleepFallLowRun >= SLEEP_FALL_FREEFALL_N)
      sleepFallSinceLow = 0;
    return;
  }
  sleepFallLowRun = 0;
  if (sleepFallSinceLow < 1000)
    sleepFallSinceLow++;
  bool afterFreefall = sleepFallSinceLow <= SLEEP_FALL_WIN_N;
  if (g >= (afterFreefall ? SLEEP_FALL_IMPACT_AFTER_FREEFALL_G : SLEEP_FALL_IMPACT_G))
  {
    if (!sleepFallSeen)
      Serial.printf("Sleep fall: %.2fg (after freefall=%d)\n", g, afterFreefall);
    sleepFallSeen = true;
  }
}
bool sleepFallPending(); // orbitCfg を使うので OrbitConfig.h の後で定義
void wristHistPush(float x, float y, float z)
{
  if (wristHistCount == 0)
  {
    sleepFallLowRun = 0; // 新しい履歴の始まり
    sleepFallSinceLow = 1000;
  }
  sleepFallFeed(x, y, z);
  float *s = wristHist[wristHistCount % WRIST_RAISE_HIST_N];
  s[0] = x;
  s[1] = y;
  s[2] = z;
  wristHistCount++;
}
// back=0 が最新
const float *wristHistAt(int back)
{
  return wristHist[(wristHistCount - 1 - back) % WRIST_RAISE_HIST_N];
}
// スリープ中に溜まったFIFOを読み、wristHist に入れる。読めたサンプル数を返す。
// ライブラリの getFIFOData は1回で全バイトを要求するため Wire のバッファ(128byte)を超えて欠ける。直接分割して読む。
int wristReadFifoHistory()
{
  static uint8_t buf[1024];
  Wire.beginTransmission(BMA400_I2C_ADDRESS_DEFAULT);
  Wire.write(0x12); // FIFO_LENGTH
  if (Wire.endTransmission(false) != 0)
    return 0;
  if (Wire.requestFrom((uint8_t)BMA400_I2C_ADDRESS_DEFAULT, (uint8_t)2) != 2)
    return 0;
  uint16_t len = Wire.read();
  len |= (uint16_t)(Wire.read() & 0x07) << 8;
  if (len > sizeof(buf))
    len = sizeof(buf);
  uint16_t got = 0;
  while (got < len)
  {
    uint8_t chunk = (uint8_t)min<uint16_t>(len - got, 119); // 7byteフレームの倍数
    Wire.beginTransmission(BMA400_I2C_ADDRESS_DEFAULT);
    Wire.write(0x14); // FIFO_DATA (読むたびに先へ進む)
    if (Wire.endTransmission(false) != 0)
      break;
    uint8_t n = Wire.requestFrom((uint8_t)BMA400_I2C_ADDRESS_DEFAULT, chunk);
    for (uint8_t i = 0; i < n && Wire.available(); i++)
      buf[got++] = Wire.read();
    if (n < chunk)
      break;
  }
  int samples = 0;
  for (uint16_t i = 0; i < got;)
  {
    uint8_t h = buf[i];
    if (h == 0x8E && i + 7 <= got) // XYZ 12bit: LSBの下位4bit + MSB(上位8bit)
    {
      int16_t v[3];
      for (int ax = 0; ax < 3; ax++)
      {
        int16_t r = (int16_t)(((uint16_t)buf[i + 2 + ax * 2] << 4) | (buf[i + 1 + ax * 2] & 0x0F));
        v[ax] = r > 2047 ? r - 4096 : r;
      }
      wristHistPush(v[0] / 512.0f, v[1] / 512.0f, v[2] / 512.0f); // ±4g・12bit で 1g=512
      samples++;
      i += 7;
    }
    else if (h == 0x48) // 設定変更フレーム
      i += 2;
    else if (h == 0xA0) // センサー時刻フレーム
      i += 4;
    else
      break; // 空フレーム(0x80)や途中で切れたフレーム
  }
  return samples;
}
// 直近 WRIST_RAISE_STEADY_N サンプルが止まっていて画面を見る向きで、
// かつ少し前に今と大きく違う向きだった(=腕を上げてきた)なら true。
bool wristRaiseDetected()
{
  if (wristHistCount < WRIST_RAISE_STEADY_N + 4)
    return false;
  float m[3] = {0, 0, 0};
  for (int ax = 0; ax < 3; ax++)
  {
    float lo = 99, hi = -99;
    for (int b = 0; b < WRIST_RAISE_STEADY_N; b++)
    {
      float v = (wristHistAt(b)[ax] + wristHistAt(b + 1)[ax]) * 0.5f; // 2サンプル平均で歩行の揺れを少しならす
      lo = min(lo, v);
      hi = max(hi, v);
      m[ax] += v;
    }
    if (hi - lo >= WRIST_RAISE_STEADY_RANGE_G)
      return false;
    m[ax] /= WRIST_RAISE_STEADY_N;
  }
  float mn = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
  if (mn < 0.8f || mn > 1.2f || m[2] / mn > WRIST_RAISE_VIEW_Z_MAX)
    return false;
  const float cosLimit = cosf(WRIST_RAISE_MIN_ANGLE_DEG * PI / 180.0f);
  int avail = min(wristHistCount, min(WRIST_RAISE_HIST_N, WRIST_RAISE_LOOKBACK_N));
  for (int b = WRIST_RAISE_STEADY_N + 1; b + 2 < avail; b++)
  {
    // 3サンプル平均で、動きの瞬間の加速度をならした向き
    float v[3];
    for (int ax = 0; ax < 3; ax++)
      v[ax] = (wristHistAt(b)[ax] + wristHistAt(b + 1)[ax] + wristHistAt(b + 2)[ax]) / 3.0f;
    float vn = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (vn < 0.5f || vn > 1.5f)
      continue;
    float c = (v[0] * m[0] + v[1] * m[1] + v[2] * m[2]) / (vn * mn);
    if (c < cosLimit)
      return true;
  }
  return false;
}

// 腕を下ろしたかの基準(画面を見ている向き)。wristLoweredCheck() が使う。
bool wristDownHasRef = false;
float wristDownRef[3];
// 起床直後に基準を入れておく(loopが始まって静止を待つまで基準なし、にしない)。
// 手首で起きた時は判定に使った直近の履歴の平均、それ以外は今の1サンプル。見る向きでなければ入れない。
void wristSeedDownRef()
{
  float m[3] = {0, 0, 0};
  if (wristHistCount >= WRIST_RAISE_STEADY_N)
  {
    for (int b = 0; b < WRIST_RAISE_STEADY_N; b++)
      for (int ax = 0; ax < 3; ax++)
        m[ax] += wristHistAt(b)[ax] / WRIST_RAISE_STEADY_N;
  }
  else
  {
    if (accelerometer.getSensorData() != BMA400_OK)
      return;
    m[0] = accelerometer.data.accelX;
    m[1] = accelerometer.data.accelY;
    m[2] = accelerometer.data.accelZ;
  }
  float n = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
  if (n < 0.85f || n > 1.15f || m[2] / n > WRIST_RAISE_VIEW_Z_MAX)
    return;
  memcpy(wristDownRef, m, sizeof(wristDownRef));
  wristDownHasRef = true;
  Serial.printf("Wrist down ref seeded: %.2f %.2f %.2f\n", m[0], m[1], m[2]);
}

// FIFOだけで判定する(ライブ計測なし)。RTCの見直し起床用。
bool wristRaiseInFifo()
{
  wristHistCount = 0;
  int n = wristReadFifoHistory();
  bool raised = n > 0 && wristRaiseDetected();
  Serial.printf("Wrist retry: FIFO %d, raised=%d\n", n, raised);
  return raised;
}

// 動きで起きた直後に呼ぶ。「向きが WRIST_HOLD_MS の間ほぼ変わらず、画面が上向き」なら true。
// 歩行中の細かい揺れで落ちないように、サンプルを直接比べず、直近 WRIST_HOLD_AVG_SAMPLES 個の平均(=揺れをならした向き)の
// 変化幅(最大-最小)で判定する。WRIST_HOLD_TIMEOUT_MS 内に満たなければ false(呼び出し側で寝直す)。
bool wristHoldConfirm()
{
  // 設定を触る前に、スリープ中のFIFOを読む(beginI2Cなどで中身が消えないように先に)
  wristHistCount = 0;
  int fifoN = wristReadFifoHistory();
  Serial.printf("Wrist FIFO samples: %d (t=%lums)\n", fifoN, millis());
  bool ready = false;
  for (int i = 0; i < 5 && !ready; i++)
  {
    ready = (accelerometer.beginI2C(BMA400_I2C_ADDRESS_DEFAULT) == BMA400_OK);
    if (!ready)
      delay(20);
  }
  if (!ready)
    return true; // 読めない時は起床を妨げない
  accelerometer.setODR(BMA400_ODR_25HZ);
  accelerometer.setRange(BMA400_RANGE_4G); // スリープ前と同じ設定なので、測定は途切れない
  if (wristRaiseDetected())
  {
    Serial.printf("Wrist raise (FIFO) t=%lums\n", millis());
    return true; // 起きる前に上げ終わって止まっていた
  }
  if (sleepFallPending())
    return true; // スリープ中に転倒らしい動きがあった

  const int AVG_N = WRIST_HOLD_AVG_SAMPLES;
  const int HIST_N = WRIST_HOLD_MS / 40;
  float raw[3][WRIST_HOLD_AVG_SAMPLES] = {};
  float hist[3][WRIST_HOLD_MS / 40] = {};
  int rawCount = 0, histCount = 0;
  const unsigned long start = millis();
  while (millis() - start < WRIST_HOLD_TIMEOUT_MS)
  {
    delay(40);
    if (accelerometer.getSensorData() != BMA400_OK)
      continue;
    raw[0][rawCount % AVG_N] = accelerometer.data.accelX;
    raw[1][rawCount % AVG_N] = accelerometer.data.accelY;
    raw[2][rawCount % AVG_N] = accelerometer.data.accelZ;
    rawCount++;
    wristHistPush(accelerometer.data.accelX, accelerometer.data.accelY, accelerometer.data.accelZ);
    if (wristRaiseDetected())
    {
      Serial.printf("Wrist raise t=%lums\n", millis());
      return true;
    }
    if (sleepFallPending())
      return true; // 転倒らしい: 静止を待たずに起きる
    if (!WRIST_SLOW_FALLBACK)
      continue;
    if (rawCount < AVG_N)
      continue;
    for (int ax = 0; ax < 3; ax++)
    {
      float sum = 0;
      for (int i = 0; i < AVG_N; i++)
        sum += raw[ax][i];
      hist[ax][histCount % HIST_N] = sum / AVG_N;
    }
    histCount++;
    if (histCount < HIST_N)
      continue;
    bool steady = true;
    for (int ax = 0; ax < 3 && steady; ax++)
    {
      float lo = hist[ax][0], hi = hist[ax][0];
      for (int i = 1; i < HIST_N; i++)
      {
        lo = min(lo, hist[ax][i]);
        hi = max(hi, hist[ax][i]);
      }
      steady = (hi - lo) < WRIST_HOLD_RANGE_G;
    }
    float zNow = hist[2][(histCount - 1) % HIST_N];
    if (steady && zNow < WRIST_HOLD_Z_MAX)
      return true;
  }
  return false;
}
// §: ANTI_NAP 動き判定
// 10秒ごとに起きたとき、少しでも動いていれば「起きている」、NAP_STILL_LIMIT回(=30秒)続けてまったく動かなければ「寝ている」とみなす。
// 「動いた」= 計測中の振れ幅が大きい、または前回の起床時から姿勢がずれた。
const uint8_t NAP_STILL_LIMIT = 3; // 動きなしがこの回数続いたら振動(10秒周期×3=30秒)
const int NAP_SAMPLES = 30;        // 起床中の計測回数(25Hz×30=約1.2秒)
const unsigned long NAP_SAMPLE_INTERVAL_MS = 40;
const float NAP_MOVE_P2P_G = 0.03f;   // 計測中の振れ幅(最大-最小)がこれを超えたら動いた
const float NAP_MOVE_DRIFT_G = 0.05f; // 前回の起床時から平均がこれ以上ずれたら動いた
RTC_DATA_ATTR uint8_t napStillCount = 0;
RTC_DATA_ATTR bool napRefValid = false;
RTC_DATA_ATTR float napRefX = 0, napRefY = 0, napRefZ = 0;
void napReset()
{
  napStillCount = 0;
  napRefValid = false;
}
// 動いたら true。センサーが読めない時は、誤って鳴らさないよう true を返す。
bool napDetectMovement()
{
  // 設定を入れ直してからスリープに戻るので、手首起床の設定も保たれる
  if (!configureBma400ActivityWakeup())
    return true;
  float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f}, sum[3] = {0, 0, 0};
  int n = 0;
  for (int i = 0; i < NAP_SAMPLES; i++)
  {
    delay(NAP_SAMPLE_INTERVAL_MS);
    if (accelerometer.getSensorData() != BMA400_OK)
      continue;
    float v[3] = {accelerometer.data.accelX, accelerometer.data.accelY, accelerometer.data.accelZ};
    for (int a = 0; a < 3; a++)
    {
      if (v[a] < mn[a])
        mn[a] = v[a];
      if (v[a] > mx[a])
        mx[a] = v[a];
      sum[a] += v[a];
    }
    n++;
  }
  if (n < 5)
    return true;
  float p2p = 0;
  for (int a = 0; a < 3; a++)
    p2p = max(p2p, mx[a] - mn[a]);
  float mean[3] = {sum[0] / n, sum[1] / n, sum[2] / n};
  float drift = 0;
  if (napRefValid)
    drift = max(fabsf(mean[0] - napRefX), max(fabsf(mean[1] - napRefY), fabsf(mean[2] - napRefZ)));
  napRefX = mean[0];
  napRefY = mean[1];
  napRefZ = mean[2];
  napRefValid = true;
  Serial.printf("[NAP] p2p=%.3f drift=%.3f still=%d\n", p2p, drift, napStillCount);
  return p2p > NAP_MOVE_P2P_G || drift > NAP_MOVE_DRIFT_G;
}
// §: POMODORO state
long POMO_WORK_SEC = 25 * 60; // スマホ設定で変更される(orbitCfgApply)
long POMO_BREAK_SEC = 5 * 60;
RTC_DATA_ATTR bool pomoRunning = false;
RTC_DATA_ATTR uint8_t pomoPhase = 0;              // 0=作業(赤LED), 1=休憩(緑LED)
RTC_DATA_ATTR long pomoRemainSec = POMO_WORK_SEC; // 停止中の残り秒数
RTC_DATA_ATTR long pomoEndSec = 0;                // 実行中の終了時刻 (0:00からの秒数)
// §: スマホ設定(Wi-Fi)
#include "OrbitConfig.h"
// スリープ中の転倒で起こすべきか(設定オン・マナーでない時だけ)
bool sleepFallPending()
{
  return sleepFallSeen && orbitCfg.fallDetect && !isManner;
}
#include "OrbitSlide.h" // BLEスライド送り(キーボード)
static_assert(SETTING == ORBIT_MENU_N, "OrbitConfig.h の OC_MENU_NAMES と MENU_LIST が一致していません");
// §: MENU order
const int MENU_COUNT = sizeof(MenusName) / sizeof(MenusName[0]); // SETTINGを含む全メニュー数
// スマホ設定でオンになっているメニューか。SETTINGは常にオン。
bool isMenuEnabled(int menu)
{
  return menu == SETTING || ((orbitCfg.menuMask >> menu) & 1);
}
// 表示するメニューの数(オンのもの + SETTING)。
int menuCount()
{
  int n = 0;
  for (int m = 0; m < MENU_COUNT; m++)
  {
    if (isMenuEnabled(m))
      n++;
  }
  return n;
}
// 使用中(オン/実行中)のメニューか。
bool isMenuInUse(int menu)
{
  switch (menu)
  {
  case MANNER:
    return isManner;
  case ANTI_NAP:
    return isAntiNapEnabled;
  case POMODORO:
    return pomoRunning;
  case SETTING:
    return orbitApActive;
  case BLUETOOTH:
    return orbitBleActive;
  default:
    return false;
  }
}
// 表示順 pos 番目のメニュー番号を返す。使用中のものを先頭に(設定画面で決めた順のまま)、残りを設定の順で並べる。
// オフにしたメニューは出さない。SETTINGは常に最後。
// 並びは状態から毎回計算するので、使用を止めれば自然に元の位置へ戻る。
int menuOrderAt(int pos)
{
  int n = 0;
  for (int pass = 0; pass < 2; pass++)
  {
    for (int i = 0; i < MENU_COUNT; i++)
    {
      int m = (i < ORBIT_MENU_N) ? orbitCfg.order[i] : i;
      if (!isMenuEnabled(m))
        continue;
      if (isMenuInUse(m) == (pass == 0))
      {
        if (n == pos)
          return m;
        n++;
      }
    }
  }
  return SETTING;
}
// メニュー番号が表示順で何番目か。
int menuPosOf(int menu)
{
  for (int pos = 0; pos < menuCount(); pos++)
  {
    if (menuOrderAt(pos) == menu)
      return pos;
  }
  return 0;
}
// 実行中は終了時刻までの残り秒数、停止中は保持している残り秒数を返す。
long pomodoroRemainingSec()
{
  if (!pomoRunning)
  {
    return pomoRemainSec;
  }
  long nowSec = hour * 3600L + minute * 60L + second;
  long diff = pomoEndSec - nowSec;
  if (diff < -43200)
    diff += 86400;
  else if (diff > 43200)
    diff -= 86400;
  long total = pomoPhase == 0 ? POMO_WORK_SEC : POMO_BREAK_SEC;
  return diff > total ? total : diff;
}

// §: Deep sleep
// スリープ中にRTCタイマーで起こす秒数を決めてセットする。
// ポモドーロ実行中はフェーズ終了、ANTI_NAP有効時(allowAntiNap)は10秒周期のうち早い方。
void armWakeTimerForSleep(bool allowAntiNap)
{
  long armSec = 0;
  if (pomoRunning)
  {
    armSec = pomodoroRemainingSec();
    if (armSec < 1)
      armSec = 1;
  }
  if (allowAntiNap && isAntiNapEnabled && (armSec == 0 || armSec > 10))
  {
    armSec = 10;
  }
  if (armSec > 0)
  {
    rtcArmWakeTimer((uint16_t)armSec);
  }
}
// 姿勢が外れなかった(INT1がLOWのまま)ときはINT1を起床要因から外す。残すと即起床を繰り返す。
void enableDeepSleepWakeSources(bool poseReleased)
{
  uint64_t mask = BIT(SW_WAKE_PIN) | BIT(RTC_INT_PIN);
  if (poseReleased)
  {
    mask |= BIT(ACC_INT1_PIN);
    wristRetryArmed = false;
  }
  else
  {
    // 手首起床を外す代わりに、少し後に起き直して腕を上げたかを確かめる
    // (ポモドーロ等のタイマーより先に起きても、寝直す時に armWakeTimerForSleep で付け直される)
    rtcArmWakeTimer(WRIST_RETRY_SEC);
    wristRetryArmed = true;
  }
  // S3はext1起床。ディープスリープ中もプルアップを保持する(SW/INT1。RTC_INTは外部プルアップ)
  rtc_gpio_pullup_en((gpio_num_t)SW_WAKE_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)SW_WAKE_PIN);
  if (poseReleased)
  {
    rtc_gpio_pullup_en((gpio_num_t)ACC_INT1_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)ACC_INT1_PIN);
  }
  esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
}
// スイッチが押されたままだと、LOWで即起床してしまう。離されるまで待ってから眠る(固着対策で最大5秒)。
void waitForSwitchRelease()
{
  unsigned long start = millis();
  while (digitalRead(SW_PIN) == LOW && millis() - start < 5000)
    delay(10);
  delay(50); // 離した直後のチャタリングで起きないように
}
void fallDetectorStop();
void enterDeepSleepIfIdle()
{
  if (nowStatus != IDLE)
  {
    return;
  }

  Serial.println("Entering deep sleep");
  clearAllDisplay();
  digitalWrite(VIBES_PIN, LOW);
  waitForSwitchRelease();

  armWakeTimerForSleep(true);

  fallDetectorStop(); // 手首起床の判定は25Hz前提
  bma400InterruptOccurred = false;
  enableDeepSleepWakeSources(waitForWristPoseRelease());
  if (!wristFifoKeep)
    wristFifoRestart(); // スリープ中の動きを25Hzで溜め始める(起床時に腕を上げたか判定する)
  esp_deep_sleep_start();
}
void tapDetectorStop();
void enterDeepSleepIfMenuOrContent()
{
  if (nowStatus != MENU && nowStatus != CONTENT)
  {
    return;
  }
  if (nowStatus == CONTENT && (nowMenuIndex == COUNT || nowMenuIndex == BLUETOOTH))
  {
    tapDetectorStop(); // 手首起床の判定は25Hz前提なので戻してから眠る
  }
  if (orbitBleActive)
  {
    orbitBleStop();
  }

  Serial.println("Entering deep sleep from MENU/CONTENT");
  clearAllDisplay();
  digitalWrite(VIBES_PIN, LOW);
  waitForSwitchRelease();

  armWakeTimerForSleep(false);

  fallDetectorStop();
  bma400InterruptOccurred = false;
  enableDeepSleepWakeSources(waitForWristPoseRelease());
  if (!wristFifoKeep)
    wristFifoRestart(); // スリープ中の動きを25Hzで溜め始める(起床時に腕を上げたか判定する)
  esp_deep_sleep_start();
}
bool bma400SingleTapDetected()
{
  return false;
}
void setStatus(Status nextStatus)
{
  if (nowStatus != nextStatus)
  {
    nowStatus = nextStatus;
    lastStatusChangeMs = millis();
    if (nextStatus == IDLE)
    {
      lastIdleActivityMs = lastStatusChangeMs;
    }
  }
}

// §: LED Display
const bool ORBIT_WAKE_FLASH = false;     // 起床のたびにリングを一瞬光らせる(起床回数の確認用)
const uint8_t DEFAULT_BRIGHTNESS = 0xa0; // 0x00〜0xFF
uint8_t nowBrightness = DEFAULT_BRIGHTNESS;
uint8_t nowBrightnessCoron = DEFAULT_BRIGHTNESS - 0x10;

#define IS31FL3733_ADDR 0x50

// §: LED map
// 1. SW(1〜12) と CS(1〜16) から IS31FL3733 の内部PWMレジスタ番号（0x00〜0xBF）を計算するマクロ
#define IS_LED(sw, cs) (uint8_t)(((sw) - 1) * 16 + ((cs) - 1))
// 2. 配線マップ設定
// 【外周・秒針LED】(0秒〜59秒の60個)
// IS_LED(sw, cs) の順で設定
const uint8_t MAP_SECONDS[60] = {
    IS_LED(10, 4), IS_LED(9, 4), IS_LED(8, 4), IS_LED(7, 4), IS_LED(6, 4),   //  0〜 4秒
    IS_LED(5, 4), IS_LED(4, 4), IS_LED(3, 4), IS_LED(2, 4), IS_LED(1, 4),    //  5〜 9秒
    IS_LED(12, 3), IS_LED(11, 3), IS_LED(10, 3), IS_LED(9, 3), IS_LED(8, 3), // 10〜14秒
    IS_LED(7, 3), IS_LED(6, 3), IS_LED(5, 3), IS_LED(4, 3), IS_LED(3, 3),    // 15〜19秒
    IS_LED(2, 3), IS_LED(1, 3), IS_LED(12, 2), IS_LED(11, 2), IS_LED(10, 2), // 20〜24秒
    IS_LED(9, 2), IS_LED(8, 2), IS_LED(7, 2), IS_LED(6, 2), IS_LED(5, 2),    // 25〜29秒
    IS_LED(4, 2), IS_LED(3, 2), IS_LED(2, 2), IS_LED(1, 2), IS_LED(12, 1),   // 30〜34秒
    IS_LED(11, 1), IS_LED(10, 1), IS_LED(9, 1), IS_LED(8, 1), IS_LED(7, 1),  // 35〜39秒
    IS_LED(6, 1), IS_LED(5, 1), IS_LED(4, 1), IS_LED(3, 1), IS_LED(2, 1),    // 40〜44秒
    IS_LED(1, 1), IS_LED(12, 5), IS_LED(11, 5), IS_LED(10, 5), IS_LED(9, 5), // 45〜49秒
    IS_LED(8, 5), IS_LED(7, 5), IS_LED(6, 5), IS_LED(5, 5), IS_LED(4, 5),    // 50〜54秒
    IS_LED(3, 5), IS_LED(2, 5), IS_LED(1, 5), IS_LED(12, 4), IS_LED(11, 4)   // 55〜59秒
};
// 【7セグメントLED】(4桁 × 7セグメント A〜G = 28個)
// 各桁ごとに セグメント [A, B, C, D, E, F, G] の順番で接続先を指定
const uint8_t MAP_7SEG[4][7] = {
    // 1桁目（時の十の位）
    {IS_LED(1, 6), IS_LED(1, 8), IS_LED(1, 11), IS_LED(1, 12), IS_LED(1, 10), IS_LED(1, 7), IS_LED(1, 9)},
    // 2桁目（時の一の位）
    {IS_LED(2, 6), IS_LED(2, 8), IS_LED(2, 11), IS_LED(2, 12), IS_LED(2, 10), IS_LED(2, 7), IS_LED(2, 9)},
    // 3桁目（分の十の位）
    {IS_LED(3, 6), IS_LED(3, 8), IS_LED(3, 11), IS_LED(3, 12), IS_LED(3, 10), IS_LED(3, 7), IS_LED(3, 9)},
    // 4桁目（分の一の位）
    {IS_LED(4, 6), IS_LED(4, 8), IS_LED(4, 11), IS_LED(4, 12), IS_LED(4, 10), IS_LED(4, 7), IS_LED(4, 9)}};
// 【コロンLED】(2個: 上ドット, 下ドット)
const uint8_t MAP_COLON[2] = {
    IS_LED(5, 6), // 上ドット
    IS_LED(5, 7)  // 下ドット
};
// 【サブ機能LED】(2個)
const uint8_t MAP_SUB[2] = {
    IS_LED(5, 8), // サブLED 1
    IS_LED(5, 9)  // サブLED 2
};
// 3. 7セグの数字パターン定義（0〜9の A〜G 点灯パターン）
const uint8_t SEG_PATTERNS[11] = {
    0b00111111, // 0 (A,B,C,D,E,F)
    0b00000110, // 1 (B,C)
    0b01011011, // 2 (A,B,D,E,G)
    0b01001111, // 3 (A,B,C,D,G)
    0b01100110, // 4 (B,C,F,G)
    0b01101101, // 5 (A,C,D,F,G)
    0b01111101, // 6 (A,C,D,E,F,G)
    0b00000111, // 7 (A,B,C)
    0b01111111, // 8 (全点灯)
    0b01101111, // 9 (A,B,C,D,F,G)
    0b00000000  // 10 (A,B,C,D,F,G)
}; // 7セグメントLED用 アルファベット表示パターン (A〜Z)
// ビット配置: 0b0G FEDCBA
const uint8_t ALPHA_PATTERNS[26] = {
    0b01110111, // A (大文字)
    0b01111100, // b (小文字)
    0b00111001, // C (大文字)
    0b01011110, // d (小文字)
    0b01111001, // E (大文字)
    0b01110001, // F (大文字)
    0b00111101, // G (大文字)
    0b01110110, // H (大文字)
    0b00000110, // I (1と同じ)
    0b00001110, // J (大文字)
    0b01110101, // K (H風の代用)
    0b00111000, // L (大文字)
    0b00110111, // M (大文字風・中央線なし)
    0b01010100, // n (小文字)
    0b01011100, // o (小文字)
    0b01110011, // P (大文字)
    0b01100111, // q (小文字)
    0b01010000, // r (小文字)
    0b01101101, // S (5と同じ)
    0b01111000, // t (小文字)
    0b00111110, // U (大文字)
    0b00011100, // v (小文字u風)
    0b00101010, // W (2つのu風・形崩れあり)
    0b01110110, // X (Hで代用)
    0b01101110, // y (小文字)
    0b01011011  // Z (2と同じ)
};

// §: LED control
// 4. 制御関数群
uint8_t ledShadow[192] = {0};
// レジスタ書き込み（内部用）
// 現在選択中のページ。0xFF は不明（初期化直後など）。ページが同じなら ロック解除+ページ選択 を省いて書き込みを1回で済ませる。
uint8_t ledCurrentPage = 0xFF;
// 送信に失敗した場合はキャッシュを破棄し、次回の書き込みでページ選択からやり直させる。
bool selectLedPage(uint8_t page)
{
  if (ledCurrentPage == page)
  {
    return true;
  }
  ledCurrentPage = 0xFF;
  Wire.beginTransmission(IS31FL3733_ADDR);
  Wire.write(0xFE);
  Wire.write(0xC5); // ロック解除
  if (Wire.endTransmission() != 0)
  {
    return false;
  }

  Wire.beginTransmission(IS31FL3733_ADDR);
  Wire.write(0xFD);
  Wire.write(page); // ページ選択
  if (Wire.endTransmission() != 0)
  {
    return false;
  }
  ledCurrentPage = page;
  return true;
}
bool writeRegister(uint8_t page, uint8_t reg, uint8_t data)
{
  if (!selectLedPage(page))
  {
    return false;
  }
  Wire.beginTransmission(IS31FL3733_ADDR);
  Wire.write(reg);
  Wire.write(data); // データ書き込み
  if (Wire.endTransmission() != 0)
  {
    ledCurrentPage = 0xFF;
    return false;
  }
  return true;
}
// 連続レジスタを同じ値で一括書き込み（IS31FL3733はアドレス自動加算）
bool fillRegisters(uint8_t page, uint8_t startReg, uint16_t count, uint8_t data)
{
  if (!selectLedPage(page))
  {
    return false;
  }
  const uint8_t CHUNK = 32;
  for (uint16_t offset = 0; offset < count; offset += CHUNK)
  {
    uint8_t n = (count - offset) < CHUNK ? (count - offset) : CHUNK;
    Wire.beginTransmission(IS31FL3733_ADDR);
    Wire.write((uint8_t)(startReg + offset));
    for (uint8_t i = 0; i < n; i++)
    {
      Wire.write(data);
    }
    if (Wire.endTransmission() != 0)
    {
      ledCurrentPage = 0xFF;
      return false;
    }
  }
  return true;
}
// 単一LEDの明るさ設定
void setLedBrightness(uint8_t ledReg, uint8_t brightness)
{
  if (ledReg < 192)
  {
    ledShadow[ledReg] = brightness;
    writeRegister(0x01, ledReg, brightness);
  }
}
// ついていうるLEDをすべて消灯する（演出用の固定輝度も含む）
void clearLitLeds()
{
  for (uint8_t r = 0; r < 192; r++)
  {
    if (ledShadow[r] != 0)
    {
      ledShadow[r] = 0;
      writeRegister(0x01, r, 0x00);
    }
  }
}

// §: CdS brightness
// CdSの値から明るさを決める。ちらつき防止のため、複数回の平均 + なめらかな追従 + 小さな変化は無視する。
// reset=true の時は平滑化をやり直す(起床直後など)
int cdsFiltered = -1;
uint8_t cdsBrightness(bool reset, uint8_t current)
{
  long sum = 0;
  for (int i = 0; i < 16; i++)
    sum += analogRead(CDS_PIN);
  int raw = sum / 16;
  if (reset || cdsFiltered < 0)
    cdsFiltered = raw;
  else
    cdsFiltered = (cdsFiltered * 7 + raw) / 8; // ゆっくり追従
  int target = 0xff;
  if (cdsFiltered > 2000)
    target = map(cdsFiltered, 2000, 4095, 0xff, 0x05);
  target = constrain(target, 0x05, 0xff);
  if (!reset && abs(target - (int)current) < 6)
    return current; // 小さな変化では更新しない(I2C書き込みとちらつきを減らす)
  return (uint8_t)target;
}

// CDSによる明るさ変更を、現在点灯中のLEDにも反映する。
// 消灯中のLEDや演出用の固定輝度は変更しない。
void updateLitLedBrightness(uint8_t oldBrightness, uint8_t newBrightness)
{
  uint8_t oldColonBrightness = oldBrightness > 0x10 ? oldBrightness - 0x10 : 0;
  uint8_t newColonBrightness = newBrightness > 0x10 ? newBrightness - 0x10 : 0;
  nowBrightnessCoron = newColonBrightness;

  if (oldBrightness == newBrightness)
    return;

  for (uint16_t r = 0; r < 192; r++)
  {
    // 0x00は消灯値。明るさ変更時に消灯LEDを点灯させない。
    if (oldBrightness != 0 && ledShadow[r] == oldBrightness)
    {
      ledShadow[r] = newBrightness;
      writeRegister(0x01, (uint8_t)r, newBrightness);
    }
    else if (oldColonBrightness != 0 && oldColonBrightness != oldBrightness &&
             ledShadow[r] == oldColonBrightness)
    {
      ledShadow[r] = newColonBrightness;
      writeRegister(0x01, (uint8_t)r, newColonBrightness);
    }
  }
}
// ICの初期化
void initDisplay()
{
  // 電源投入直後はIS31FL3733が応答しないことがあるので、全手順が成功するまでやり直す。
  for (int attempt = 0; attempt < 10; attempt++)
  {
    ledCurrentPage = 0xFF;
    bool ok = writeRegister(0x03, 0x11, 0x00); // リセット
    ledCurrentPage = 0xFF;                     // リセットでページ選択も初期化されるので再選択させる
    delay(10);
    ok = ok && writeRegister(0x03, 0x01, 0xFF); // GCC (全電流最大)
    // 全LEDのDot ON/OFFスイッチを有効化
    ok = ok && fillRegisters(0x00, 0x00, 0x18, 0xFF);
    // 全消灯
    ok = ok && fillRegisters(0x01, 0x00, 192, 0x00);
    memset(ledShadow, 0, sizeof(ledShadow));
    ok = ok && writeRegister(0x03, 0x00, 0x01); // ノーマルモード起動
    if (ok)
    {
      return;
    }
    Serial.println("IS31FL3733 init failed, retrying...");
    delay(20);
  }
}
// ① 外周（秒）の制御: 指定した秒（0〜59）のLEDを点灯させる
void setSecondLed(uint8_t sec, uint8_t brightness)
{
  if (sec < 60)
  {
    setLedBrightness(MAP_SECONDS[sec], brightness);
  }
}
// 外周をすべて消灯
void clearAllSeconds()
{
  for (int i = 0; i < 60; i++)
  {
    setLedBrightness(MAP_SECONDS[i], 0x00);
  }
}
// ② 7セグの制御: 指定した桁（0〜3）に数字（0〜9）を表示
void set7SegDigit(uint8_t digitIndex, uint8_t num, uint8_t brightness)
{
  if (digitIndex >= 4 || num > 10)
    return;

  uint8_t pattern = SEG_PATTERNS[num];
  for (uint8_t seg = 0; seg < 7; seg++)
  {
    bool isOn = (pattern >> seg) & 0x01;
    setLedBrightness(MAP_7SEG[digitIndex][seg], isOn ? brightness : 0x00);
  }
}
// ③ コロンの制御: ON / OFF
void setColon(bool enable, uint8_t brightness)
{
  uint8_t b = enable ? brightness : 0x00;
  setLedBrightness(MAP_COLON[0], b);
  setLedBrightness(MAP_COLON[1], b);
}
// ④ サブ機能LEDの制御: 0番または1番を ON / OFF
void setSubLed(uint8_t subIndex, bool enable, uint8_t brightness)
{
  if (subIndex < 2)
  {
    setLedBrightness(MAP_SUB[subIndex], enable ? brightness : 0x00);
  }
}
// ⑤ 時間の一括表示関数（時, 分, 秒）
void displayTime(uint8_t hour, uint8_t min, uint8_t sec, uint8_t brightness)
{
  // 7セグ更新
  if (hour / 10 != 0)
    set7SegDigit(0, hour / 10, brightness);
  set7SegDigit(1, hour % 10, brightness);
  if (min / 10 != 0)
    set7SegDigit(2, min / 10, brightness);
  set7SegDigit(3, min % 10, brightness);

  // 秒針更新（現在の秒だけ点灯）
  clearAllSeconds();
  setSecondLed(sec, brightness);

  // コロン点滅（偶数秒で点灯）
  lastChangeSecondMillis = nowMillis;
  setColon(true, nowBrightnessCoron);
}
void displayNumber4Digits(uint32_t value, uint8_t brightness)
{
  if (value > 9999)
  {
    value = 9999;
  }

  if (value / 1000 != 0)
    set7SegDigit(0, (value / 1000) % 10, brightness);
  if (value / 1000 != 0 || value / 100 != 0)
    set7SegDigit(1, (value / 100) % 10, brightness);
  if (value / 1000 != 0 || value / 100 != 0 || value / 10 != 0)
    set7SegDigit(2, (value / 10) % 10, brightness);
  set7SegDigit(3, value % 10, brightness);
}
// 画面の全LED（外周、7セグ、コロン、サブ）を一括で消灯する関数
void clearAllDisplay()
{
  fillRegisters(0x01, 0x00, 192, 0x00);
  memset(ledShadow, 0, sizeof(ledShadow));
}
// 外周（秒針）以外のLED（7セグ、コロン、サブLED）を一括で消灯する関数
void clearDisplayExceptSeconds()
{
  // 1. 7セグメントLED（4桁 × 7セグ）を消灯
  for (uint8_t digit = 0; digit < 4; digit++)
  {
    for (uint8_t seg = 0; seg < 7; seg++)
    {
      setLedBrightness(MAP_7SEG[digit][seg], 0x00);
    }
  }

  // 2. コロンLED（2個）を消灯
  setLedBrightness(MAP_COLON[0], 0x00);
  setLedBrightness(MAP_COLON[1], 0x00);

  // 3. サブ機能LED（2個）を消灯
  setLedBrightness(MAP_SUB[0], 0x00);
  setLedBrightness(MAP_SUB[1], 0x00);
}
// アスキー文字（'A'〜'Z', 'a'〜'z'）から7セグのビットパターンを取得する関数
uint8_t getAlphaPattern(char c)
{
  // 小文字は大文字に変換
  if (c >= 'a' && c <= 'z')
  {
    c = c - 'a' + 'A';
  }

  // A〜Zの範囲内なら配列から取得
  if (c >= 'A' && c <= 'Z')
  {
    return ALPHA_PATTERNS[c - 'A'];
  }

  // 記号は7セグの中央線（G）と下段（D）で表現
  if (c == '-')
  {
    return 0b01000000; // 中央線
  }
  if (c == '_')
  {
    return 0b00001000; // 下段
  }

  // 該当しない文字（スペースなど）は消灯(0x00)
  return 0x00;
}
// 7セグの指定した桁にアルファベットを表示する関数
void set7SegChar(uint8_t digitIndex, char c, uint8_t brightness)
{
  if (digitIndex >= 4)
    return;

  uint8_t pattern = getAlphaPattern(c);
  for (uint8_t seg = 0; seg < 7; seg++)
  {
    bool isOn = (pattern >> seg) & 0x01;
    setLedBrightness(MAP_7SEG[digitIndex][seg], isOn ? brightness : 0x00);
  }
}

// §: Text scroll
// テキストを流す
// コードをブロックしながら
void scrollText(const char *text, uint8_t brightness, uint16_t delayMs, bool scrollUntilEnd)
{
  int len = strlen(text);
  if (len == 0)
    return;

  // 最初の文字が右端（digit=3）に現れるのは、内部インデックスが -3 のとき
  // コンパイルエラーを防ぐため、ループカウンタは負の数（int）で管理します
  int startPos = -3;

  // 終了位置の計算
  // 流しきる(true)なら最後の文字が左端(digit=0)を過ぎるまで、そうでないなら右端(digit=3)に届くまで
  int endPos = scrollUntilEnd ? len : (len - 3);

  for (int i = startPos; i < endPos; i++)
  {
    clearDisplayExceptSeconds();

    for (uint8_t digit = 0; digit < 4; digit++)
    {
      // 表示する文字のインデックスを計算
      int charIndex = i + digit;

      // 文字列の範囲内（0 〜 len-1）であればその文字を表示、それ以外は空白
      if (charIndex >= 0 && charIndex < len)
      {
        set7SegChar(digit, text[charIndex], brightness);
      }
      else
      {
        set7SegChar(digit, ' ', brightness);
      }
    }

    delay(delayMs);
  }
}
// テキストを現在のRTCの秒数によって、切り替える。
// 引数を String 型に変更（呼び出し側の .c_str() は不要になります）
unsigned long menuScrollStartMs = 0; // 項目名スクロールの開始時刻（項目切替時にリセット）
void scrollTextBySecond(String text, uint8_t brightness, uint16_t second)
{
  // 🌟 ここで切り替わるスピードを「ミリ秒」で設定します
  // 例: 500  = 0.5秒ごとに1文字進む
  // 例: 250  = 0.25秒ごとに1文字進む（爆速）
  // 例: 1000 = 1秒ごとに1文字進む
  const unsigned long SCROLL_SPEED_MS = 250;

  // 1. 文字列の安全チェック
  int textLength = text.length();
  if (textLength == 0)
    return;

  // 2. Arduinoが起動してからの総ミリ秒数を取得
  unsigned long currentMillis = millis() - menuScrollStartMs;

  // 3. 割り算を使わず、現在のステップ数を計算（オーバーフロー対策）
  // 1ループ（文字数＋空白4つ）にかかる総時間を計算
  int totalLength = textLength + 4;
  unsigned long totalLoopTime = totalLength * SCROLL_SPEED_MS;

  // 現在のループ内での経過時間を計算（引き算ループで安全に処理）
  unsigned long timeInLoop = currentMillis;
  while (timeInLoop >= totalLoopTime)
  {
    timeInLoop -= totalLoopTime;
  }

  // 経過時間から、現在の文字の開始位置（0 〜 totalLength-1）を割り出す
  // 開始時は先頭文字が一番右の桁に出る位置（空白3つ分手前）から始める
  int startIndex = totalLength - 3;
  while (timeInLoop >= SCROLL_SPEED_MS)
  {
    timeInLoop -= SCROLL_SPEED_MS;
    startIndex++;
  }
  startIndex %= totalLength;

  // 4. ディスプレイ（4桁）に1文字ずつセット
  for (int i = 0; i < 4; i++)
  {
    int charIndex = startIndex + i;

    // 引き算ループで文字位置を配列内に安全に収める
    while (charIndex >= totalLength)
    {
      charIndex -= totalLength;
    }

    char c = ' '; // 基本はスペース
    if (charIndex < textLength)
    {
      c = text.charAt(charIndex); // 文字の範囲内ならその文字を抽出
    }

    // 7セグに安全にcharを渡す
    set7SegChar(i, c, brightness);
  }
}
// 外周を指定された項目数で分割し、そのうちの指定された項目を点灯する関数
void setSecondLedSegment(uint8_t totalSegments, uint8_t activeSegment, uint8_t brightness)
{
  // 境界を個別に計算し、割り切れない場合も全60個を隙間なく分割する。
  if (totalSegments == 0 || activeSegment >= totalSegments)
    return;

  uint8_t start = (uint16_t)activeSegment * 60 / totalSegments;
  uint8_t end = (uint16_t)(activeSegment + 1) * 60 / totalSegments;
  for (uint8_t i = start; i < end; i++)
  {
    setSecondLed(i, brightness);
  }
}
// §: Other
const uint8_t MOTOR_POWER = 255;
const uint8_t PRESS_FEEDBACK_POWER = 255; // 新モーターは立ち上がりが遅いので全開で駆動
const uint16_t PRESS_FEEDBACK_MS = 60;    // 24msでは回り始める前に止まっていた
bool hapticPulseActive = false;
uint16_t hapticPulseDurationMs = PRESS_FEEDBACK_MS;
unsigned long hapticPulseStartedMs = 0;
void startHapticPulse(uint8_t power, uint16_t durationMs);
void updateHaptic();
void pulseVibes(uint16_t ms = 50);

// §: Switch
// スイッチの押され方を判定する。短押し、ダブルタップ、長押しの3種類を判定する。
// スイッチの押され方を判定する関数
SwitchEvent checkSwitchEvent()
{

  static enum
  {
    IDLE,
    PRESSED,
    WAIT_SECOND_PRESS,
    WAIT_RELEASE // ダブルタップ確定後、指が離れるまで新しい押下として数えない
  } state = IDLE;

  static unsigned long pressStartTime = 0;
  static unsigned long releaseTime = 0;
  static bool longPressFired = false;
  static unsigned long releaseSeenMs = 0; // 離れたのを最初に見た時刻(0=押されている)
  static unsigned long lastLongMs = 0;    // 長押し継続中に次の長押しを出す基準時刻

  bool isPressed = (digitalRead(SW_PIN) == LOW); // LOWで押されていると仮定
  unsigned long now = millis();

  switch (state)
  {
  case IDLE:
    if (isPressed)
    {
      pressStartTime = now;
      longPressFired = false;
      state = PRESSED;
      startHapticPulse(PRESS_FEEDBACK_POWER, PRESS_FEEDBACK_MS);
    }
    break;

  case PRESSED:
    if (isPressed)
    {
      releaseSeenMs = 0; // 一瞬離れて戻った=チャタリング。押下継続として扱う
    }
    if (!isPressed)
    {
      // 離れた状態が RELEASE_DEBOUNCE_MS 続くまで「離した」と確定しない(長押し中の接点のバタつきで短押しになるのを防ぐ)
      if (releaseSeenMs == 0)
        releaseSeenMs = now;
      if (now - releaseSeenMs < RELEASE_DEBOUNCE_MS)
        break;
      releaseSeenMs = 0;
      // 短く離された場合 (チャタリング防止)
      if (now - pressStartTime >= CHATTERING_MS)
      {
        if (longPressFired)
        {
          state = IDLE;
        }
        else
        {
          // 単押しは離した瞬間に確定する(ダブルタップは使わない)
          state = IDLE;
          Serial.println("Single press detected");
          lastStatusChangeMs = nowMillis;
          return SW_SINGLE;
        }
      }
      else
      {
        state = IDLE; // 雑音として無視
      }
    }
    else if ((!longPressFired && now - pressStartTime >= LONG_PRESS_MS) ||
             (longPressFired && now - lastLongMs >= LONG_PRESS_MS))
    {
      // 離さず押し続けている間は LONG_PRESS_MS ごとに長押しを繰り返す(コンテンツ→メニュー→時計→スリープと続けて戻る)
      longPressFired = true;
      lastLongMs = now;
      Serial.println("Long press detected");
      lastStatusChangeMs = nowMillis;
      if (nowStatus != IDLE)
      {
        startHapticPulse(PRESS_FEEDBACK_POWER, PRESS_FEEDBACK_MS * 3 / 2);
      }
      return SW_LONG; // ★ 長押し検出！
    }
    break;

  case WAIT_RELEASE:
    if (!isPressed)
    {
      state = IDLE;
    }
    break;

  case WAIT_SECOND_PRESS:
    // 離された後、規定時間以内に再度押されたか判定
    if (isPressed)
    {
      if (now - releaseTime >= CHATTERING_MS)
      {
        state = WAIT_RELEASE;
        Serial.println("Double tap detected");
        lastStatusChangeMs = nowMillis;
        return SW_DOUBLE; // ★ ダブルタップ検出！
      }
    }
    else
    {
      // 再度押されずにダブルタップ受付時間を過ぎたら「短押し」確定
      if (now - releaseTime >= DOUBLE_TAP_MS)
      {
        state = IDLE;
        Serial.println("Single press detected");
        lastStatusChangeMs = nowMillis;
        return SW_SINGLE; // ★ 短押し検出！
      }
    }
    break;
  }
  return SW_NONE;
}

// §: Vibration
// モールス信号のドット/ダッシュ振動
void playVibe(bool isDot)
{
  if (!isManner)
  {
    if (isDot)
    {
      analogWrite(VIBES_PIN, DEFAULT_VIBE);
      delay(dotMs);
      analogWrite(VIBES_PIN, 0);
    }
    else
    {
      analogWrite(VIBES_PIN, DEFAULT_VIBE);
      delay(dotMs * 2);
      analogWrite(VIBES_PIN, 0);
    }
  }
}
// PWM settings
#define PWM_FREQ 20000
#define PWM_RESOLUTION 8
// 8-bit duty: 0-255
// loop()から定期的に呼び、入力処理を止めずに短い振動を出す。
void startHapticPulse(uint8_t power, uint16_t durationMs)
{
  if (isManner)
    return;

  ledcWrite(VIBES_PIN, power);
  hapticPulseStartedMs = millis();
  hapticPulseDurationMs = durationMs;
  hapticPulseActive = true;
}

void updateHaptic()
{
  if (hapticPulseActive && millis() - hapticPulseStartedMs >= hapticPulseDurationMs)
  {
    ledcWrite(VIBES_PIN, 0);
    hapticPulseActive = false;
  }
}
void pulseVibes(uint16_t ms)
{
  ledcWrite(VIBES_PIN, MOTOR_POWER);
  delay(ms);
  ledcWrite(VIBES_PIN, 0);
}

// §: Train
// Train
bool isStationBTrain = false;
// 駅A 平日(サンプル。実際の時刻表はスマホの設定画面から貼り付ける)
const uint16_t stationATable_weekday[] = {
    6 * 60 + 0,
    6 * 60 + 15,
    6 * 60 + 30,
    6 * 60 + 45,
    7 * 60 + 0,
    7 * 60 + 15,
    7 * 60 + 30,
    7 * 60 + 45,
    8 * 60 + 0,
    8 * 60 + 15,
    8 * 60 + 30,
    8 * 60 + 45,
    9 * 60 + 0,
    9 * 60 + 15,
    9 * 60 + 30,
    9 * 60 + 45,
    10 * 60 + 0,
    10 * 60 + 15,
    10 * 60 + 30,
    10 * 60 + 45,
    11 * 60 + 0,
    11 * 60 + 15,
    11 * 60 + 30,
    11 * 60 + 45,
    12 * 60 + 0,
    12 * 60 + 15,
    12 * 60 + 30,
    12 * 60 + 45,
    13 * 60 + 0,
    13 * 60 + 15,
    13 * 60 + 30,
    13 * 60 + 45,
    14 * 60 + 0,
    14 * 60 + 15,
    14 * 60 + 30,
    14 * 60 + 45,
    15 * 60 + 0,
    15 * 60 + 15,
    15 * 60 + 30,
    15 * 60 + 45,
    16 * 60 + 0,
    16 * 60 + 15,
    16 * 60 + 30,
    16 * 60 + 45,
    17 * 60 + 0,
    17 * 60 + 15,
    17 * 60 + 30,
    17 * 60 + 45,
    18 * 60 + 0,
    18 * 60 + 15,
    18 * 60 + 30,
    18 * 60 + 45,
    19 * 60 + 0,
    19 * 60 + 15,
    19 * 60 + 30,
    19 * 60 + 45,
    20 * 60 + 0,
    20 * 60 + 15,
    20 * 60 + 30,
    20 * 60 + 45,
    21 * 60 + 0,
    21 * 60 + 15,
    21 * 60 + 30,
    21 * 60 + 45,
    22 * 60 + 0,
    22 * 60 + 15,
    22 * 60 + 30,
    22 * 60 + 45,
    23 * 60 + 0,
    23 * 60 + 15,
    23 * 60 + 30,
    23 * 60 + 45,
};
// 駅A 休日(サンプル。実際の時刻表はスマホの設定画面から貼り付ける)
const uint16_t stationATable_weekend[] = {
    6 * 60 + 10,
    6 * 60 + 30,
    6 * 60 + 50,
    7 * 60 + 10,
    7 * 60 + 30,
    7 * 60 + 50,
    8 * 60 + 10,
    8 * 60 + 30,
    8 * 60 + 50,
    9 * 60 + 10,
    9 * 60 + 30,
    9 * 60 + 50,
    10 * 60 + 10,
    10 * 60 + 30,
    10 * 60 + 50,
    11 * 60 + 10,
    11 * 60 + 30,
    11 * 60 + 50,
    12 * 60 + 10,
    12 * 60 + 30,
    12 * 60 + 50,
    13 * 60 + 10,
    13 * 60 + 30,
    13 * 60 + 50,
    14 * 60 + 10,
    14 * 60 + 30,
    14 * 60 + 50,
    15 * 60 + 10,
    15 * 60 + 30,
    15 * 60 + 50,
    16 * 60 + 10,
    16 * 60 + 30,
    16 * 60 + 50,
    17 * 60 + 10,
    17 * 60 + 30,
    17 * 60 + 50,
    18 * 60 + 10,
    18 * 60 + 30,
    18 * 60 + 50,
    19 * 60 + 10,
    19 * 60 + 30,
    19 * 60 + 50,
    20 * 60 + 10,
    20 * 60 + 30,
    20 * 60 + 50,
    21 * 60 + 10,
    21 * 60 + 30,
    21 * 60 + 50,
    22 * 60 + 10,
    22 * 60 + 30,
    22 * 60 + 50,
    23 * 60 + 10,
    23 * 60 + 30,
};
// 駅B 平日(サンプル。実際の時刻表はスマホの設定画面から貼り付ける)
const uint16_t stationBTable_weekday[] = {
    6 * 60 + 5,
    6 * 60 + 20,
    6 * 60 + 35,
    6 * 60 + 50,
    7 * 60 + 5,
    7 * 60 + 20,
    7 * 60 + 35,
    7 * 60 + 50,
    8 * 60 + 5,
    8 * 60 + 20,
    8 * 60 + 35,
    8 * 60 + 50,
    9 * 60 + 5,
    9 * 60 + 20,
    9 * 60 + 35,
    9 * 60 + 50,
    10 * 60 + 5,
    10 * 60 + 20,
    10 * 60 + 35,
    10 * 60 + 50,
    11 * 60 + 5,
    11 * 60 + 20,
    11 * 60 + 35,
    11 * 60 + 50,
    12 * 60 + 5,
    12 * 60 + 20,
    12 * 60 + 35,
    12 * 60 + 50,
    13 * 60 + 5,
    13 * 60 + 20,
    13 * 60 + 35,
    13 * 60 + 50,
    14 * 60 + 5,
    14 * 60 + 20,
    14 * 60 + 35,
    14 * 60 + 50,
    15 * 60 + 5,
    15 * 60 + 20,
    15 * 60 + 35,
    15 * 60 + 50,
    16 * 60 + 5,
    16 * 60 + 20,
    16 * 60 + 35,
    16 * 60 + 50,
    17 * 60 + 5,
    17 * 60 + 20,
    17 * 60 + 35,
    17 * 60 + 50,
    18 * 60 + 5,
    18 * 60 + 20,
    18 * 60 + 35,
    18 * 60 + 50,
    19 * 60 + 5,
    19 * 60 + 20,
    19 * 60 + 35,
    19 * 60 + 50,
    20 * 60 + 5,
    20 * 60 + 20,
    20 * 60 + 35,
    20 * 60 + 50,
    21 * 60 + 5,
    21 * 60 + 20,
    21 * 60 + 35,
    21 * 60 + 50,
    22 * 60 + 5,
    22 * 60 + 20,
    22 * 60 + 35,
    22 * 60 + 50,
    23 * 60 + 5,
    23 * 60 + 20,
    23 * 60 + 35,
    23 * 60 + 50,
};
// 駅B 休日(サンプル。実際の時刻表はスマホの設定画面から貼り付ける)
const uint16_t stationBTable_weekend[] = {
    6 * 60 + 15,
    6 * 60 + 35,
    6 * 60 + 55,
    7 * 60 + 15,
    7 * 60 + 35,
    7 * 60 + 55,
    8 * 60 + 15,
    8 * 60 + 35,
    8 * 60 + 55,
    9 * 60 + 15,
    9 * 60 + 35,
    9 * 60 + 55,
    10 * 60 + 15,
    10 * 60 + 35,
    10 * 60 + 55,
    11 * 60 + 15,
    11 * 60 + 35,
    11 * 60 + 55,
    12 * 60 + 15,
    12 * 60 + 35,
    12 * 60 + 55,
    13 * 60 + 15,
    13 * 60 + 35,
    13 * 60 + 55,
    14 * 60 + 15,
    14 * 60 + 35,
    14 * 60 + 55,
    15 * 60 + 15,
    15 * 60 + 35,
    15 * 60 + 55,
    16 * 60 + 15,
    16 * 60 + 35,
    16 * 60 + 55,
    17 * 60 + 15,
    17 * 60 + 35,
    17 * 60 + 55,
    18 * 60 + 15,
    18 * 60 + 35,
    18 * 60 + 55,
    19 * 60 + 15,
    19 * 60 + 35,
    19 * 60 + 55,
    20 * 60 + 15,
    20 * 60 + 35,
    20 * 60 + 55,
    21 * 60 + 15,
    21 * 60 + 35,
    21 * 60 + 55,
    22 * 60 + 15,
    22 * 60 + 35,
    22 * 60 + 55,
    23 * 60 + 15,
    23 * 60 + 35,
};
// 次の電車の時刻を返す
String getNextTrain(const uint16_t table[], int size)
{
  int now = hour * 60 + minute;

  for (int i = 0; i < size; i++)
  {
    if (table[i] >= now)
    {

      int h = table[i] / 60;
      int m = table[i] % 60;

      char buf[6];
      sprintf(buf, "%02d:%02d", h, m);

      return String(buf);
    }
  }

  return "--:--";
}
// 指定された電車時刻を文字列として返す
String getTrainStr(const uint16_t table)
{

  if (table == -1)
    return "--:--";

  int h = table / 60;
  int m = table % 60;

  char buf[6];
  sprintf(buf, "%02d:%02d", h, m);

  return String(buf);
}
// 次の電車までの残り時間を返す
int getNextTrainCountdown(const uint16_t table[], int size)
{
  // if (size <= 0)
  //   return 0;

  int nowSec = hour * 3600 + minute * 60 + second;

  for (int i = 0; i < size; i++)
  {

    int trainSec = table[i] * 60;

    if (trainSec >= nowSec)
    {

      int diff = trainSec - nowSec;

      int m = diff / 60;
      int s = diff % 60;

      char buf[8];
      sprintf(buf, "%02dm%02ds", m, s);

      return diff;
    }
  }

  return -1; // 本日の電車は終了
}

// §: POMODORO
long pomoLastShownSec = -1;
int pomoLastRing = -1;
int pomoLastSubState = -1; // bit0=赤, bit1=緑。-1は未描画

// 次のフェーズ(作業<->休憩)を開始して振動で知らせる。
void pomodoroAdvance()
{
  // 起床や描画の遅れが積もらないよう、直前の終了時刻を基準に次の終了時刻を決める。
  // 大きくずれている(10秒超過・3秒以上先・停止中)ときは現在時刻を基準にする。
  long overshoot = -pomodoroRemainingSec();
  bool onSchedule = pomoRunning && overshoot >= -3 && overshoot <= 10;
  long baseSec = onSchedule ? pomoEndSec : (hour * 3600L + minute * 60L + second);
  pomoPhase = pomoPhase == 0 ? 1 : 0;
  pomoRemainSec = pomoPhase == 0 ? POMO_WORK_SEC : POMO_BREAK_SEC;
  pomoEndSec = (baseSec + pomoRemainSec) % 86400;
  pomoRunning = true;
  Serial.printf("Pomodoro phase -> %s\n", pomoPhase == 0 ? "WORK" : "BREAK");
  if (!isManner)
  {
    for (int i = 0; i < 2; i++)
    {
      startHapticPulse(255, PRESS_FEEDBACK_MS * 5 / 2);
      while (hapticPulseActive)
        updateHaptic();
      delay(120);
    }
  }
}
void pomodoroToggle()
{
  if (pomoRunning)
  {
    long remain = pomodoroRemainingSec();
    pomoRemainSec = remain > 0 ? remain : 0;
    pomoRunning = false;
  }
  else
  {
    if (pomoRemainSec <= 0)
    {
      pomoRemainSec = pomoPhase == 0 ? POMO_WORK_SEC : POMO_BREAK_SEC;
    }
    long nowSec = hour * 3600L + minute * 60L + second;
    pomoEndSec = (nowSec + pomoRemainSec) % 86400;
    pomoRunning = true;
  }
}
// 残り時間を mm:ss、外周を経過位置、サブLEDをフェーズ(赤=作業/緑=休憩)で表示する。
void renderPomodoro(bool force)
{
  if (force)
  {
    pomoLastShownSec = -1;
    pomoLastRing = -1;
    pomoLastSubState = -1;
  }
  long remain = pomodoroRemainingSec();
  if (remain < 0)
    remain = 0;

  if (remain != pomoLastShownSec)
  {
    pomoLastShownSec = remain;
    long m = remain / 60;
    long s = remain % 60;
    set7SegDigit(0, (m / 10) % 10, nowBrightness);
    set7SegDigit(1, m % 10, nowBrightness);
    set7SegDigit(2, s / 10, nowBrightness);
    set7SegDigit(3, s % 10, nowBrightness);
    setColon(pomoRunning ? (s % 2 == 0) : true, nowBrightnessCoron);
  }

  // サブLED: 再生中は現在フェーズの色(赤=作業/緑=休憩)を点滅、停止中は赤と緑を両方点灯。
  bool blinkOn = (millis() / 500) % 2 == 0;
  bool redOn = pomoRunning ? (pomoPhase == 0 && blinkOn) : true;
  bool greenOn = pomoRunning ? (pomoPhase == 1 && blinkOn) : true;
  int subState = (redOn ? 1 : 0) | (greenOn ? 2 : 0);
  if (subState != pomoLastSubState)
  {
    pomoLastSubState = subState;
    setSubLed(1, redOn, nowBrightness);
    setSubLed(0, greenOn, nowBrightness);
  }

  // 外周は全点灯だと電流が大きいので、経過位置を示す1点だけ点灯する(時計の針のイメージ)。
  long total = pomoPhase == 0 ? POMO_WORK_SEC : POMO_BREAK_SEC;
  long elapsed = total - remain;
  int pos = (int)(elapsed * 60 / total);
  if (pos > 59)
    pos = 59;
  if (pos != pomoLastRing)
  {
    if (pomoLastRing >= 0)
    {
      setSecondLed(pomoLastRing, 0x00);
    }
    setSecondLed(pos, nowBrightness);
    pomoLastRing = pos;
  }
}
// どの状態からでもポモドーロのコンテンツ画面へ移る。
void gotoPomodoroContent()
{
  setStatus(CONTENT);
  nowMenuIndex = POMODORO;
  lastStatusChangeMs = millis();
  clearLitLeds();
  renderPomodoro(true);
}

// GAMEの粒子物理パラメータ（必要に応じて調整できます）
// §: GAME
const uint8_t GAME_PARTICLE_COUNT = 60;
const float GAME_GRAVITY = 10.0f;    // 円盤上で重力方向へ寄る強さ
const float GAME_SWIPE_BOOST = 0.8f; // 急に動かした時の追加加速度
const float GAME_FRICTION = 0.8f;    // 粒の速度の残り方
const float GAME_ACCEL_DEADBAND = 0.04f;
const float GAME_RING_RADIUS = 1.0f;  // 円盤の半径。0.7前後で中心寄り
const float GAME_MAX_ACCEL = 2.0f;    // センサー入力の上限（g）
const float GAME_ACCEL_FILTER = 0.5f; // 傾き入力のなめらかさ
float gameParticleX = 0.0f;           // 円盤上の粒のX座標（-1〜1）
float gameParticleY = 0.0f;           // 円盤上の粒のY座標（-1〜1）
float gameParticleVX = 0.0f;
float gameParticleVY = 0.0f;
float gameFilteredAccelX = 0.0f;
float gameFilteredAccelY = 0.0f;
float gamePreviousAccelX = 0.0f;
float gamePreviousAccelY = 0.0f;
unsigned long gameLastUpdateMs = 0;
bool gameHasAccelSample = false;

// 追加ミニ機能
uint8_t rouletteLed = 0;
const uint8_t ROULETTE_START_STRIDE = 8;   // 初速で1回に進めるLED数
const uint16_t ROULETTE_LAP_TIME_MS = 300; // 初速でリングを1周する時間
const uint8_t ROULETTE_DECEL_STEPS = 8;    // 停止までに減速させる更新回数
uint8_t rouletteStride = ROULETTE_START_STRIDE;
uint8_t rouletteWinner = 0;
uint8_t roulettePhase = 0; // 0=停止, 1=加速/減速して抽選
unsigned long rouletteNextStepMs = 0;
uint8_t rouletteStepsLeft = 0;

// センサー軸の向きと重みを調整する関数。必要ならX/Yの符号を反転。
void gameGravityFromAccel(float accelX, float accelY, float &gravityX, float &gravityY)
{
  const float accelXWeight = 1.0f;
  const float accelYWeight = 1.0f;
  gravityX = accelX * accelXWeight;
  gravityY = accelY * accelYWeight;
  if (gravityX > GAME_MAX_ACCEL)
    gravityX = GAME_MAX_ACCEL;
  if (gravityX < -GAME_MAX_ACCEL)
    gravityX = -GAME_MAX_ACCEL;
  if (gravityY > GAME_MAX_ACCEL)
    gravityY = GAME_MAX_ACCEL;
  if (gravityY < -GAME_MAX_ACCEL)
    gravityY = -GAME_MAX_ACCEL;
}

void resetGameParticle()
{
  gameParticleX = 0.0f;
  gameParticleY = 0.0f;
  gameParticleVX = 0.0f;
  gameParticleVY = 0.0f;
  gameFilteredAccelX = 0.0f;
  gameFilteredAccelY = 0.0f;
  gamePreviousAccelX = 0.0f;
  gamePreviousAccelY = 0.0f;
  gameLastUpdateMs = millis();
  gameHasAccelSample = false;
}

void updateGameParticle(float accelX, float accelY, unsigned long now)
{
  float dt = (now - gameLastUpdateMs) / 1000.0f;
  gameLastUpdateMs = now;
  if (dt > 0.1f)
    dt = 0.1f;
  if (dt <= 0.0f)
    return;

  float rawGravityX, rawGravityY;
  gameGravityFromAccel(accelX, accelY, rawGravityX, rawGravityY);
  gameFilteredAccelX += (rawGravityX - gameFilteredAccelX) * GAME_ACCEL_FILTER;
  gameFilteredAccelY += (rawGravityY - gameFilteredAccelY) * GAME_ACCEL_FILTER;
  if (fabsf(gameFilteredAccelX) < GAME_ACCEL_DEADBAND)
    gameFilteredAccelX = 0.0f;
  if (fabsf(gameFilteredAccelY) < GAME_ACCEL_DEADBAND)
    gameFilteredAccelY = 0.0f;
  float swipeX = gameHasAccelSample ? gameFilteredAccelX - gamePreviousAccelX : 0.0f;
  float swipeY = gameHasAccelSample ? gameFilteredAccelY - gamePreviousAccelY : 0.0f;
  gamePreviousAccelX = gameFilteredAccelX;
  gamePreviousAccelY = gameFilteredAccelY;
  gameHasAccelSample = true;

  // 円盤上のXY平面で重力と勢いを積分し、円の外へ出ないように反射させる。
  gameParticleVX += (gameFilteredAccelX * GAME_GRAVITY + swipeX * GAME_SWIPE_BOOST) * dt;
  gameParticleVY += (gameFilteredAccelY * GAME_GRAVITY + swipeY * GAME_SWIPE_BOOST) * dt;
  float friction = powf(GAME_FRICTION, dt * 30.0f);
  gameParticleVX *= friction;
  gameParticleVY *= friction;
  gameParticleX += gameParticleVX * dt;
  gameParticleY += gameParticleVY * dt;

  float radiusSquared = gameParticleX * gameParticleX + gameParticleY * gameParticleY;
  if (radiusSquared > GAME_RING_RADIUS * GAME_RING_RADIUS)
  {
    float radius = sqrtf(radiusSquared);
    float normalX = gameParticleX / radius;
    float normalY = gameParticleY / radius;
    gameParticleX = normalX * GAME_RING_RADIUS;
    gameParticleY = normalY * GAME_RING_RADIUS;
    float outwardSpeed = gameParticleVX * normalX + gameParticleVY * normalY;
    if (outwardSpeed > 0.0f)
    {
      gameParticleVX -= 1.6f * outwardSpeed * normalX;
      gameParticleVY -= 1.6f * outwardSpeed * normalY;
    }
  }
}

void renderGameParticle()
{
  clearAllSeconds();
  // 円盤上のXY位置を角度に変換して、対応する外周LEDを点灯。
  float angle = atan2f(gameParticleY, gameParticleX);
  if (angle < 0.0f)
    angle += 6.2831853f;
  uint8_t particle = (uint8_t)(angle * GAME_PARTICLE_COUNT / 6.2831853f + 0.5f) % GAME_PARTICLE_COUNT;
  setSecondLed(particle, nowBrightness);
  // 直前の位置を弱く残して移動方向が分かるようにする。
  uint8_t trail = particle == 0 ? GAME_PARTICLE_COUNT - 1 : particle - 1;
  uint8_t trailBrightness = nowBrightness / 3;
  setSecondLed(trail, trailBrightness);
}

// §: COUNT (指のタップでカウント)
// #define TAP_LOG_MODE // 有効にすると加速度(100Hz)をCSVで流し続けるタップ実測モードで起動する
// 親指と人差し指をつまむ衝撃を、Z軸(画面に垂直)のハイパス後の大きさのピークで検出する。
// 実測(tapdata/)で、タップはZ軸が主、腕振り・歩き・タイピングはX/Y軸が主だったためZだけを見る。
// 係数0.85は、ほぼ1サンプル間の差分(=ゆっくりした腕の動きを通さない)。
const float TAP_THRESHOLD_G = 0.24f;           // ハイパス後のZの大きさがこれを超えたらタップ
const float TAP_MAX_G = 0.60f;                 // これを超える大きな衝撃は腕の動き(タップは約0.2〜0.5g)。タップとして数えない
const unsigned long TAP_BIG_QUIET_MS = 600;    // 大きな動きの後は、揺れが収まるまでこの間タップを無視する
const float TAP_LP_ALPHA = 0.85f;              // 重力成分を追うローパスの係数
const unsigned long TAP_REFRACTORY_MS = 150;   // 連続検出の最短間隔
const unsigned long TAP_HAPTIC_BLANK_MS = 180; // 振動中・直後は振動を誤検出しないよう無視する
const uint32_t COUNT_MAX = 9999;
const uint16_t COUNT_TAP_HAPTIC_MS = 60;  // タップごとの振動
const uint16_t COUNT_TEN_HAPTIC_MS = 150; // 10の倍数での振動(タップごとより長い)
uint32_t tapCount = 0;
bool tapCountDown = false; // false=カウントアップ, true=カウントダウン
float tapLpX = 0, tapLpY = 0, tapLpZ = 0;
bool tapLpReady = false;
bool tapAbove = false;
unsigned long tapLastMs = 0;
unsigned long tapBlankUntilMs = 0;
unsigned long tapBigMs = 0; // 直近に大きな動きを見た時刻(0=なし)

// COUNTに入った時に呼ぶ。手首起床用の25Hzから、タップが見える100Hzに上げる。
void tapDetectorStart()
{
  accelerometer.setODR(BMA400_ODR_100HZ);
  tapLpReady = false;
  tapAbove = false;
  tapLastMs = 0;
  tapBlankUntilMs = 0;
  tapBigMs = 0;
}
// COUNTを出る時・眠る前に呼ぶ。手首起床の判定(25Hz前提)を壊さないよう戻す。
void tapDetectorStop()
{
  accelerometer.setODR(fallDetectOn ? BMA400_ODR_100HZ : BMA400_ODR_25HZ); // 転倒検知中は100Hzのまま
}
// 毎ループ呼ぶ。タップを検出した瞬間だけ true。
bool tapDetected(unsigned long now)
{
  if (accelerometer.getSensorData() != BMA400_OK)
    return false;
  float x = accelerometer.data.accelX;
  float y = accelerometer.data.accelY;
  float z = accelerometer.data.accelZ;
  if (!tapLpReady)
  {
    tapLpX = x;
    tapLpY = y;
    tapLpZ = z;
    tapLpReady = true;
    return false;
  }
  float hx = x - tapLpX, hy = y - tapLpY, hz = z - tapLpZ;
  tapLpX += hx * TAP_LP_ALPHA;
  tapLpY += hy * TAP_LP_ALPHA;
  tapLpZ += hz * TAP_LP_ALPHA;
  float mag = fabsf(hz);

  if (hapticPulseActive)
    tapBlankUntilMs = now + TAP_HAPTIC_BLANK_MS;
  if (mag > TAP_MAX_G)
    tapBigMs = now;
  bool above = mag > TAP_THRESHOLD_G;
  bool fire = above && mag <= TAP_MAX_G && !tapAbove && (long)(now - tapBlankUntilMs) >= 0 &&
              (tapLastMs == 0 || now - tapLastMs >= TAP_REFRACTORY_MS) &&
              (tapBigMs == 0 || now - tapBigMs >= TAP_BIG_QUIET_MS);
  tapAbove = above;
  if (fire)
    tapLastMs = now;
  return fire;
}
void renderCount()
{
  uint32_t v = tapCount > COUNT_MAX ? COUNT_MAX : tapCount;
  uint32_t div = 1000;
  for (uint8_t d = 0; d < 4; d++, div /= 10)
  {
    if (d < 3 && v < div)
      set7SegChar(d, ' ', nowBrightness); // 先頭の0は出さない
    else
      set7SegDigit(d, (v / div) % 10, nowBrightness);
  }
  clearAllSeconds();
  for (uint8_t i = 0; i < v % 60; i++)
  {
    setSecondLed(i, nowBrightness);
  }
  setSubLed(0, !tapCountDown, nowBrightness);
  setSubLed(1, tapCountDown, nowBrightness);
}
void countStart()
{
  tapCount = 0; // 入り直すたびに0から
  tapCountDown = orbitCfg.countDown;
  tapDetectorStart();
  renderCount();
}
// CONTENT中に毎ループ呼ぶ。短押しでアップ/ダウン切替、タップで増減、10の倍数で振動。
void countUpdate(SwitchEvent swEvent, unsigned long now)
{
  bool changed = false;
  if (swEvent == SW_SINGLE)
  {
    tapCountDown = !tapCountDown;
    changed = true;
  }
  if (tapDetected(now))
  {
    if (tapCountDown)
    {
      if (tapCount > 0)
        tapCount--;
    }
    else if (tapCount < COUNT_MAX)
    {
      tapCount++;
    }
    lastStatusChangeMs = now; // タップも操作とみなして無操作スリープを延ばす
    changed = true;
    // タップごとに短く、10の倍数ではそれより長く振動する(振動中は tapDetected が無視する)
    if (tapCount % 10 == 0 && (tapCountDown || tapCount > 0))
      startHapticPulse(255, COUNT_TEN_HAPTIC_MS);
    else
      startHapticPulse(PRESS_FEEDBACK_POWER, COUNT_TAP_HAPTIC_MS);
  }
  if (changed)
    renderCount();
}

// §: Knock
// ---- 強く叩く(ノック)の検出。COUNTの指先タップ(約0.2〜0.5g)とは別物 ----
// 反対の手の平や拳で時計をトンと叩いた時の、1g以上の鋭い衝撃だけを拾う。
// 腕を動かした後や、動かしている最中の衝撃は無視する。実測データ(tapdata/)では、
// 指先タップ・歩行・スイング・タイピングなどで誤検出なしだった(ノック自体の実測はまだ)。
const float KNOCK_MIN_G = 0.3f;                  // ハイパス後のZがこれを超えたらノック候補(さらに敏感に: 0.6→0.3。指先タップは約0.2〜0.5g。誤反応が出るなら0.4程度へ戻す)
const float KNOCK_QUIET_G = 0.15f;               // これ以下なら「静か」
const unsigned long KNOCK_PRE_QUIET_MS = 250;    // 候補の直前、この間ずっと静かだったこと(腕が止まっている)。誤動作対策で100→250
const unsigned long KNOCK_SETTLE_IGNORE_MS = 60; // 衝撃直後の揺れ(余韻)として無視する時間
const float KNOCK_SETTLE_MAX_G = 0.5f;           // その後これを超えたら腕の動きなので取り消す。誤動作対策で0.8→0.5
const unsigned long KNOCK_CONFIRM_MS = 250;      // 候補からこの時間おさまっていたら確定。誤動作対策で160→250
const unsigned long KNOCK_REFRACTORY_MS = 500;   // 連続検出の最短間隔
const uint16_t SLIDE_KNOCK_HAPTIC_MS = 150;      // 叩いて進めた時の振動(指先タップの60msより長く強く)
float knockLpZ = 0;
bool knockLpReady = false;
unsigned long knockLastActiveMs = 0;
unsigned long knockCandidateMs = 0; // 0=候補なし
unsigned long knockLastFireMs = 0;
void knockDetectorReset()
{
  knockLpReady = false;
  knockLastActiveMs = 0;
  knockCandidateMs = 0;
  knockLastFireMs = 0;
}
// 毎ループ呼ぶ。ノックを確定した瞬間だけ true。tapDetectorStart() で100Hzにしてから使う。
bool knockDetected(unsigned long now)
{
  if (accelerometer.getSensorData() != BMA400_OK)
    return false;
  float z = accelerometer.data.accelZ;
  if (!knockLpReady)
  {
    knockLpZ = z;
    knockLpReady = true;
    knockLastActiveMs = now;
    return false;
  }
  float h = z - knockLpZ;
  knockLpZ += h * TAP_LP_ALPHA;
  float mag = fabsf(h);

  bool fire = false;
  if (knockCandidateMs != 0)
  {
    unsigned long since = now - knockCandidateMs;
    if (since >= KNOCK_SETTLE_IGNORE_MS && mag > KNOCK_SETTLE_MAX_G)
    {
      knockCandidateMs = 0; // おさまらない = 腕の動き
    }
    else if (since >= KNOCK_CONFIRM_MS)
    {
      knockCandidateMs = 0;
      knockLastFireMs = now;
      fire = true;
    }
    if (since >= KNOCK_SETTLE_IGNORE_MS && mag > KNOCK_QUIET_G)
      knockLastActiveMs = now;
  }
  else
  {
    bool blank = hapticPulseActive || (knockLastFireMs != 0 && now - knockLastFireMs < KNOCK_REFRACTORY_MS);
    if (mag > KNOCK_MIN_G && !blank && now - knockLastActiveMs >= KNOCK_PRE_QUIET_MS)
      knockCandidateMs = now;
    else if (mag > KNOCK_QUIET_G)
      knockLastActiveMs = now;
  }
  return fire;
}

// §: SLIDE
// スライド送り: 単押し=次、エンコーダー右=次/左=前、時計を強く叩く=次。長押しで戻る(共通)
unsigned long SLIDE_SLEEP_MS = 10UL * 60UL * 1000UL; // 無操作でこの時間たつとスリープ
unsigned long slideLastActivityMs = 0;
int slideShownState = -1; // 画面に出した状態(0:待機 1:接続中 2:使える)。-1は未描画
// ---- サブメニュー: SLIDEに入るとまず用途を選ぶ。選んだ後の長押しで選択画面へ、選択画面で長押しでMENUへ ----
enum SlideMode
{
  SLIDE_MODE_SELECT, // 用途の選択画面
  SLIDE_MODE_SLIDE,  // スライド送り(→/←キー + 持ち時間タイマー)
  SLIDE_MODE_SHORT,  // ショート動画のスクロール(↓/↑キー)
  SLIDE_MODE_SHUTTER // スマホのカメラのシャッター(音量下げキー)
};
SlideMode slideMode = SLIDE_MODE_SELECT;
const uint8_t SLIDE_SUB_N = 3; // サブメニューの項目数。外周リングをこの数で等分して選択位置を示す
uint8_t slideSelCursor = 0;    // 選択画面で指している項目(0:SLID 1:SHRT 2:SHUT)
// ---- 発表の持ち時間タイマー(設定の slideMin が1以上の時だけ) ----
bool slideTimerRunning = false; // 最初にスライドを送った瞬間から計り始める
unsigned long slideTimerStartMs = 0;
uint8_t slideTimerAlerts = 0; // 鳴らした合図(bit0:5分 bit1:1分 bit2:終了)
long slideTimerShownKey = -1;
int slideTimerRing = -1;
int slideTimerSubState = -1;
void slideTimerReset()
{
  slideTimerRunning = false;
  slideTimerAlerts = 0;
  slideTimerShownKey = -1;
  slideTimerRing = -1;
  slideTimerSubState = -1;
}
void slideTimerAlert(uint16_t ms)
{
  if (!isManner)
    startHapticPulse(255, ms);
}
void renderSlideTimer()
{
  unsigned long now = nowMillis;
  long total = (long)orbitCfg.slideMin * 60;
  long elapsed = slideTimerRunning ? (long)((now - slideTimerStartMs) / 1000UL) : 0;
  long remain = total - elapsed;
  if (remain < 0)
    remain = 0;

  if (slideTimerRunning)
  {
    if (remain <= 300 && total > 600 && !(slideTimerAlerts & 1))
    {
      slideTimerAlerts |= 1;
      slideTimerAlert(300);
    }
    if (remain <= 60 && total > 120 && !(slideTimerAlerts & 2))
    {
      slideTimerAlerts |= 2;
      slideTimerAlert(500);
    }
    if (remain == 0 && !(slideTimerAlerts & 4))
    {
      slideTimerAlerts |= 4;
      slideTimerAlert(1200);
    }
  }

  // 時間切れ後は表示を点滅させる
  bool blinkOff = (remain == 0 && slideTimerRunning && ((now / 500) % 2 == 1));
  long key = remain * 4 + (blinkOff ? 1 : 0) + (slideTimerRunning && !blinkOff && (elapsed % 2 == 0) ? 2 : 0);
  if (key != slideTimerShownKey)
  {
    slideTimerShownKey = key;
    uint8_t b = blinkOff ? 0x00 : nowBrightness;
    long m = remain / 60, sec = remain % 60;
    set7SegDigit(0, (m / 10) % 10, b);
    set7SegDigit(1, m % 10, b);
    set7SegDigit(2, sec / 10, b);
    set7SegDigit(3, sec % 10, b);
    setColon(slideTimerRunning ? (elapsed % 2 == 0 && !blinkOff) : true, nowBrightnessCoron);
  }

  // サブLED: BLEの状態(緑=使える / 赤=待機・接続中)
  int sub = orbitBleReady() ? 2 : 1;
  if (sub != slideTimerSubState)
  {
    slideTimerSubState = sub;
    setSubLed(0, sub == 2, nowBrightness);
    setSubLed(1, sub == 1, nowBrightness);
  }

  // 外周は経過位置の1点だけ点灯(ポモドーロと同じ)
  int pos = (int)(elapsed * 60 / (total > 0 ? total : 1));
  if (pos > 59)
    pos = 59;
  if (pos != slideTimerRing)
  {
    if (slideTimerRing >= 0)
      setSecondLed(slideTimerRing, 0x00);
    setSecondLed(pos, nowBrightness);
    slideTimerRing = pos;
  }
}
// ---- シャッターのセルフタイマー: ダイヤルで秒数を選び、押すとカウントダウン→シャッター ----
const uint8_t SHUT_DELAYS[] = {0, 1, 2, 3, 5, 10, 15, 30}; // 0=すぐ切る
const uint8_t SHUT_DELAY_MAX = 30;
const uint8_t SHUT_DELAY_N = sizeof(SHUT_DELAYS) / sizeof(SHUT_DELAYS[0]);
uint8_t shutDelayIdx = 3;           // 選んでいる秒数の位置(初期3秒)
bool shutCounting = false;          // カウントダウン中
unsigned long shutFireAtMs = 0;     // シャッターを切る時刻
long shutShownKey = -1;             // 画面に出した内容(再描画の判定用)
unsigned long shutFlashUntilMs = 0; // 撮影直後の「SHUT」表示を出す期限
void shutterReset()
{
  shutCounting = false;
  shutShownKey = -1;
  shutFlashUntilMs = 0;
}
void shutterFire(unsigned long now)
{
  shutCounting = false;
  shutShownKey = -1;
  if (!orbitShutter())
    Serial.println("[SLIDE] 未接続のため送れません");
  else
    shutFlashUntilMs = now + 800;
  if (!isManner)
    startHapticPulse(255, 150);
}
// 押された時: カウントダウン中なら中止、そうでなければ開始(0秒ならすぐ切る)
void shutterTrigger(unsigned long now)
{
  if (shutCounting)
  {
    shutCounting = false;
    shutShownKey = -1;
    Serial.println("[SHUTTER] cancel");
    return;
  }
  uint8_t sec = SHUT_DELAYS[shutDelayIdx];
  if (sec == 0)
  {
    shutterFire(now);
    return;
  }
  shutCounting = true;
  shutFireAtMs = now + (unsigned long)sec * 1000UL;
  shutShownKey = -1;
  Serial.printf("[SHUTTER] %d秒後に撮影\n", sec);
}
// つながっている時だけ呼ぶ。待機中は "T  5"(秒数)、カウント中は残り秒を大きく出す。外周は残り時間ぶんだけ点灯
void shutterRender(unsigned long now)
{
  long remainMs = shutCounting ? (long)(shutFireAtMs - now) : 0;
  if (shutCounting && remainMs <= 0)
  {
    shutterFire(now);
    return;
  }
  long remainSec = shutCounting ? (remainMs + 999) / 1000 : 0;
  bool flash = !shutCounting && (long)(shutFlashUntilMs - now) > 0;
  long key = shutCounting ? 1000 + remainSec * 10 + (((remainMs % 1000) < 500) ? 1 : 0)
                          : (flash ? 500 : SHUT_DELAYS[shutDelayIdx]);
  if (key == shutShownKey)
    return;
  shutShownKey = key;
  clearLitLeds();
  if (flash)
  {
    const char *t = "SHUT";
    for (uint8_t i = 0; i < 4; i++)
      set7SegChar(i, t[i], nowBrightness);
  }
  else if (shutCounting)
  {
    set7SegDigit(2, (remainSec / 10) % 10, nowBrightness);
    set7SegDigit(3, remainSec % 10, nowBrightness);
    if (remainSec < 10)
      set7SegChar(2, ' ', nowBrightness);
    setColon(((remainMs % 1000) < 500), nowBrightnessCoron); // 半秒ごとに点滅
    int total = SHUT_DELAYS[shutDelayIdx];
    int lit = (int)(remainMs * 60 / ((long)total * 1000));
    if (lit > 59)
      lit = 59;
    for (int i = 0; i <= lit; i++)
      setSecondLed(i, nowBrightness);
  }
  else
  {
    uint8_t sec = SHUT_DELAYS[shutDelayIdx];
    set7SegChar(0, 'T', nowBrightness);
    set7SegDigit(2, (sec / 10) % 10, nowBrightness);
    set7SegDigit(3, sec % 10, nowBrightness);
    if (sec < 10)
      set7SegChar(2, ' ', nowBrightness);
    // 外周は秒数の大きさに比例して点灯(最大秒数で一周)
    int lit = (int)sec * 60 / SHUT_DELAY_MAX;
    for (int i = 0; i < lit; i++)
      setSecondLed(i, nowBrightness);
  }
  setSubLed(0, !shutCounting, nowBrightness);
  setSubLed(1, shutCounting, nowBrightness);
}
const char *slideSelName() { return slideSelCursor == 0 ? "SLIDE" : slideSelCursor == 1 ? "SHORT"
                                                                                        : "SHUTTER"; }
void renderSlide();
void renderSlideSelect()
{
  clearLitLeds();
  // 外周リングをサブメニューの項目数で分割し、選んでいる項目の区間を点灯(MENUと同じ見せ方)
  setSecondLedSegment(SLIDE_SUB_N, slideSelCursor, nowBrightness);
  scrollTextBySecond(slideSelName(), nowBrightness, second);
  // サブLED: BLEの状態(緑=つながって使える / 赤=待機・接続中)。選んでいる項目は外周リングで示す
  bool ready = orbitBleReady();
  setSubLed(0, ready, nowBrightness);
  setSubLed(1, !ready, nowBrightness);
}
// 選択画面へ(SLIDEに入った時、動作中の長押し)。BLEはつないだまま
void slideEnterSelect()
{
  slideMode = SLIDE_MODE_SELECT;
  menuScrollStartMs = millis(); // 文字のスクロールを頭から
  slideTimerReset();
  shutterReset();
  slideShownState = -1;
  renderSlideSelect();
}
void slideEnterMode(uint8_t m)
{
  slideMode = (SlideMode)m;
  clearLitLeds(); // 選択画面のリング表示を消す
  slideShownState = -1;
  slideTimerReset();
  shutterReset();
  renderSlide();
}
void renderSlide()
{
  if (slideMode == SLIDE_MODE_SLIDE && orbitCfg.slideMin > 0)
  {
    renderSlideTimer();
    return;
  }
  int state = orbitBleReady() ? 2 : (orbitBleConnected() ? 1 : 0); // 0:待機 1:接続中(まだ使えない) 2:使える
  if (slideMode == SLIDE_MODE_SHUTTER && state == 2)
  {
    if (slideShownState != 2)
    {
      slideShownState = 2;
      shutShownKey = -1;
    }
    shutterRender(nowMillis);
    return;
  }
  if (slideMode == SLIDE_MODE_SHUTTER && shutCounting)
    shutterReset(); // 切断されたらカウントを止める
  if (state == slideShownState)
    return;
  slideShownState = state;
  clearDisplayExceptSeconds();
  if (state == 1)
  {
    set7SegChar(0, 'P', nowBrightness);
    set7SegChar(1, 'A', nowBrightness);
    set7SegChar(2, 'I', nowBrightness);
    set7SegChar(3, 'R', nowBrightness);
    setSubLed(0, false, nowBrightness);
    setSubLed(1, true, nowBrightness);
  }
  else if (state == 2 && slideMode == SLIDE_MODE_SHORT)
  {
    set7SegChar(0, 'S', nowBrightness);
    set7SegChar(1, 'H', nowBrightness);
    set7SegChar(2, 'R', nowBrightness);
    set7SegChar(3, 'T', nowBrightness);
    setSubLed(0, true, nowBrightness);
    setSubLed(1, false, nowBrightness);
  }
  else if (state == 2)
  {
    set7SegChar(2, 'O', nowBrightness);
    set7SegChar(3, 'K', nowBrightness);
    setSubLed(0, true, nowBrightness);
    setSubLed(1, false, nowBrightness);
  }
  else
  {
    set7SegChar(1, 'B', nowBrightness);
    set7SegChar(2, 'L', nowBrightness);
    set7SegChar(3, 'E', nowBrightness);
    setSubLed(0, false, nowBrightness);
    setSubLed(1, true, nowBrightness);
  }
}
void slideSend(bool next, unsigned long now)
{
  slideLastActivityMs = now;
  lastStatusChangeMs = now;
  if (slideMode == SLIDE_MODE_SHUTTER)
  {
    if (orbitBleReady())
      shutterTrigger(now);
    else
      Serial.println("[SLIDE] 未接続のため送れません");
    return;
  }
  bool isShort = slideMode == SLIDE_MODE_SHORT;
  if (!isShort && orbitCfg.slideMin > 0 && !slideTimerRunning)
  {
    slideTimerRunning = true; // 最初に送った瞬間から持ち時間を計り始める
    slideTimerStartMs = now;
  }
  bool ok = isShort ? (next ? orbitShortNext() : orbitShortPrev())
                    : (next ? orbitSlideNext() : orbitSlidePrev());
  if (!ok)
    Serial.println("[SLIDE] 未接続のため送れません");
}
void slideUpdate(SwitchEvent swEvent, int encSteps, unsigned long now)
{
  if (slideMode == SLIDE_MODE_SELECT)
  {
    if (encSteps != 0)
    {
      slideSelCursor = (slideSelCursor + (encSteps > 0 ? 1 : SLIDE_SUB_N - 1)) % SLIDE_SUB_N;
      slideLastActivityMs = now;
      lastStatusChangeMs = now;
      menuScrollStartMs = millis();
      renderSlideSelect();
    }
    if (swEvent == SW_SINGLE)
    {
      slideLastActivityMs = now;
      lastStatusChangeMs = now;
      knockDetectorReset();
      slideEnterMode(slideSelCursor == 0 ? SLIDE_MODE_SLIDE : slideSelCursor == 1 ? SLIDE_MODE_SHORT
                                                                                  : SLIDE_MODE_SHUTTER);
      return;
    }
    scrollTextBySecond(slideSelName(), nowBrightness, second); // 毎ループ、文字を流す
    static int selSubShown = -1;
    int selSub = orbitBleReady() ? 1 : 0;
    if (selSub != selSubShown)
    {
      selSubShown = selSub;
      setSubLed(0, selSub == 1, nowBrightness);
      setSubLed(1, selSub == 0, nowBrightness);
    }
    return;
  }
  if (swEvent == SW_SINGLE)
    slideSend(true, now);
  if (slideMode == SLIDE_MODE_SHUTTER)
  {
    // ダイヤル=セルフタイマーの秒数を選ぶ(カウント中は無視。押して中止してから変える)
    if (encSteps != 0 && !shutCounting)
    {
      shutDelayIdx = (shutDelayIdx + (encSteps > 0 ? 1 : SHUT_DELAY_N - 1)) % SHUT_DELAY_N;
      shutShownKey = -1;
      slideLastActivityMs = now;
      lastStatusChangeMs = now;
    }
  }
  else
  {
    for (int i = 0; i < abs(encSteps) && i < 5; i++)
      slideSend(encSteps > 0, now);
  }
  if (shutCounting)
  {
    slideLastActivityMs = now; // 数えている間は眠らせない
    lastStatusChangeMs = now;
  }
  if (knockDetected(now))
  {
    slideSend(true, now);
    startHapticPulse(255, SLIDE_KNOCK_HAPTIC_MS); // 叩いて進めた合図。振動中は knockDetected が無視する
  }
  renderSlide();
}

// §: SOS
// 画面に「SOS」を出し、ライトでモールス信号の SOS (・・・ーーー・・・) を繰り返す。長押しで戻る(共通)
// delayを使わず、経過時間で点滅を進めるので、長押しはいつでも効く
const uint16_t SOS_UNIT_MS = 200; // モールスの1単位(ドット1つ)の長さ
// 偶数番目=点灯、奇数番目=消灯。単位数。S(・・・) 文字間3 O(ーーー) 文字間3 S(・・・) 語間7
// §: Light(調光)
// lightLevel: 0=OFF、1〜LIGHT_MAX_LEVEL=明るさ。LEDはlow:ONなのでPWMのデューティは反転して出す。
const int LIGHT_MAX_LEVEL = 16;
int lightLevel = LIGHT_MAX_LEVEL;
bool lightPwmAttached = false;
void lightApply()
{
  isLedOn = lightLevel > 0;
  if (!isLedOn)
  {
    if (lightPwmAttached)
    {
      ledcDetach(LED_PIN);
      lightPwmAttached = false;
    }
    pinMode(LED_PIN, INPUT_PULLUP); // OFF
    return;
  }
  if (!lightPwmAttached)
  {
    lightPwmAttached = ledcAttach(LED_PIN, 5000, 8);
  }
  // 2乗カーブ: 暗い側を細かく、明るい側を粗く(目の感じ方に合わせる)
  uint32_t duty = (uint32_t)lightLevel * lightLevel * 255 / (LIGHT_MAX_LEVEL * LIGHT_MAX_LEVEL);
  if (duty < 1)
    duty = 1;
  ledcWrite(LED_PIN, 255 - duty);
}
void lightRender()
{
  clearLitLeds();
  if (lightLevel == 0)
  {
    set7SegChar(1, 'O', nowBrightness);
    set7SegChar(2, 'F', nowBrightness);
    set7SegChar(3, 'F', nowBrightness);
    setSubLed(0, false, nowBrightness);
    setSubLed(1, true, nowBrightness);
    return;
  }
  set7SegChar(1, 'L', nowBrightness);
  if (lightLevel >= 10)
    set7SegDigit(2, lightLevel / 10, nowBrightness);
  set7SegDigit(3, lightLevel % 10, nowBrightness);
  for (int i = 0; i < lightLevel * 60 / LIGHT_MAX_LEVEL; i++)
  {
    setSecondLed(i, nowBrightness);
  }
  setSubLed(0, true, nowBrightness);
  setSubLed(1, false, nowBrightness);
}

const uint8_t SOS_SEQ[] = {1, 1, 1, 1, 1, 3, 3, 1, 3, 1, 3, 3, 1, 1, 1, 1, 1, 7};
const int SOS_SEQ_N = sizeof(SOS_SEQ) / sizeof(SOS_SEQ[0]);
int sosSeqIndex = 0;
unsigned long sosStepStartMs = 0;
void sosLight(bool on)
{
  if (lightPwmAttached)
  {
    ledcDetach(LED_PIN);
    lightPwmAttached = false;
  }
  isLedOn = false; // ライトコンテンツの状態とは別に直接駆動するので、調光状態は破棄
  lightLevel = 0;
  if (on)
  {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); // low:ON
  }
  else
  {
    pinMode(LED_PIN, INPUT_PULLUP); // OFF
  }
  setSubLed(0, on, nowBrightness);
  setSubLed(1, false, nowBrightness);
}
void sosStart(unsigned long now)
{
  clearLitLeds();
  clearDisplayExceptSeconds();
  set7SegChar(1, 'S', nowBrightness);
  set7SegChar(2, 'O', nowBrightness);
  set7SegChar(3, 'S', nowBrightness);
  sosSeqIndex = 0;
  sosStepStartMs = now;
  sosLight(true);
}
void sosStop()
{
  sosLight(false);
}
void sosUpdate(unsigned long now)
{
  lastStatusChangeMs = now; // SOS中は眠らせない
  if (now - sosStepStartMs >= (unsigned long)SOS_SEQ[sosSeqIndex] * SOS_UNIT_MS)
  {
    sosSeqIndex = (sosSeqIndex + 1) % SOS_SEQ_N;
    sosStepStartMs = now;
    sosLight(sosSeqIndex % 2 == 0);
  }
}

// §: FALL
// 転倒検知: 衝撃を受けたら、カウントダウンを出して、触らなければSOSを自動で始める。
// 起きている間は100Hzで見る。ディープスリープ中は §: Sleep fall (FIFO) で起床時に判定する。マナー中・GAME/COUNT/SLIDE/SOS中は見ない。
// 反応しやすさ優先: 小さめの衝撃でも拾い、動いていても(投げた後に転がっても)取り消さない。
// 衝撃の強さ(±4gレンジの合成加速度のピーク)で猶予秒数を変える: 強いほど短い。
const float FALL_FREEFALL_G = 0.5f;                                                   // これ未満=ほぼ無重力(落下中)
const unsigned long FALL_FREEFALL_WIN_MS = 400;                                       // 落下のあと、この間に衝撃が来たら転倒らしい
const unsigned long FALL_FREEFALL_MIN_MS = 250;                                       // 無重力がこれ以上続いて初めて「落下」とみなす(机に置く程度の一瞬は除外)
unsigned long fallLowStartMs = 0;                                                     // 無重力が始まった時刻(0=なし)
const float FALL_IMPACT_AFTER_FREEFALL_G = 5.5f;                                      // 落下のあとの衝撃の閾値(机に置く程度=1.5〜2.5gで誤検知したので引き上げ)
const float FALL_IMPACT_G = 5.5f;                                                     // 落下を見ていなくても、これ以上の衝撃なら候補(机に置く程度は除外)
const unsigned long FALL_SETTLE_MS = 800;                                             // 衝撃のあと、ピークを追う時間
const float FALL_STRONG_G = 5.0f;                                                     // 強い衝撃
const float FALL_MID_G = 3.5f;                                                        // 中くらいの衝撃
const uint8_t FALL_COUNT_STRONG_S = 5, FALL_COUNT_MID_S = 10, FALL_COUNT_WEAK_S = 15; // カウントダウン秒
const unsigned long FALL_COOLDOWN_MS = 5000;                                          // キャンセル後は、この間再検知しない
enum FallState
{
  FALL_WATCH,
  FALL_SETTLE,
  FALL_ALERT
};
FallState fallState = FALL_WATCH;
bool fallSensorOk = false;
unsigned long fallLowMs = 0; // 直近に無重力を見た時刻(0=なし)
unsigned long fallImpactMs = 0;
unsigned long fallIgnoreUntilMs = 0;
float fallPeakG = 0;
unsigned long fallAlertStartMs = 0, fallAlertTotalMs = 0;
int fallShownSec = -1;

// 設定や加速度センサーの状態に合わせて、100Hz監視を入り切りする
void fallDetectorApply()
{
  bool want = fallSensorOk && orbitCfg.fallDetect;
  if (want && !fallDetectOn)
  {
    accelerometer.setODR(BMA400_ODR_100HZ);
    fallDetectOn = true;
    fallState = FALL_WATCH;
    fallLowMs = 0;
  }
  else if (!want && fallDetectOn)
  {
    fallDetectorStop();
  }
}
// 眠る前・オフにした時。手首起床の判定(25Hz前提)に戻す
void fallDetectorStop()
{
  if (!fallDetectOn)
    return;
  fallDetectOn = false;
  fallState = FALL_WATCH;
  accelerometer.setODR(BMA400_ODR_25HZ);
}
void fallCancel(unsigned long now)
{
  fallState = FALL_WATCH;
  fallLowMs = 0;
  fallIgnoreUntilMs = now + FALL_COOLDOWN_MS;
  analogWrite(VIBES_PIN, 0);
  clearLitLeds();
  clearDisplayExceptSeconds();
  nowMenuIndex = 0;
  setStatus(IDLE);
  lastMinute = -1; // 時計を描き直す
  lastSecond = -1;
  lastStatusChangeMs = now;
}
void fallRenderAlert(unsigned long now)
{
  unsigned long elapsed = now - fallAlertStartMs;
  int remain = (int)((fallAlertTotalMs - elapsed + 999) / 1000);
  if (remain < 0)
    remain = 0;
  if (remain == fallShownSec)
    return;
  fallShownSec = remain;
  clearLitLeds();
  clearDisplayExceptSeconds();
  set7SegChar(0, 'F', nowBrightness);
  if (remain >= 10)
    set7SegDigit(2, remain / 10, nowBrightness);
  set7SegDigit(3, remain % 10, nowBrightness);
  int lit = (int)((fallAlertTotalMs - elapsed) * 60 / fallAlertTotalMs); // 残りの割合だけ秒リングを点灯
  for (int i = 0; i < lit && i < 60; i++)
    setSecondLed(i, nowBrightness);
  startHapticPulse(255, 150);
}
// カウントダウンを始める(衝撃のあと・スリープ中の転倒で起きた時)
void fallStartAlert(uint8_t sec, unsigned long now)
{
  fallAlertTotalMs = (unsigned long)sec * 1000UL;
  fallAlertStartMs = now;
  fallShownSec = -1;
  fallState = FALL_ALERT;
  lastStatusChangeMs = now;
  fallRenderAlert(now);
}
// 毎ループ呼ぶ。カウントダウン中(画面を占有している間)は true を返す。
bool fallUpdate(SwitchEvent swEvent, int encSteps, unsigned long now)
{
  if (!fallDetectOn)
    return false;
  bool operated = (swEvent != SW_NONE || encSteps != 0);
  if (isManner)
  {
    if (fallState == FALL_ALERT)
      fallCancel(now);
    fallState = FALL_WATCH;
    return false;
  }

  if (fallState == FALL_ALERT)
  {
    lastStatusChangeMs = now; // カウントダウン中は眠らせない
    if (operated)
    {
      fallCancel(now);
      return true; // この操作はキャンセルに使う
    }
    if (now - fallAlertStartMs >= fallAlertTotalMs)
    {
      // 時間切れ: SOSを自動で始める。長押しで止められる
      fallState = FALL_WATCH;
      fallIgnoreUntilMs = now + FALL_COOLDOWN_MS;
      nowMenuIndex = SOS;
      setStatus(CONTENT);
      sosStart(now);
      return true;
    }
    fallRenderAlert(now);
    return true;
  }

  if (accelerometer.getSensorData() != BMA400_OK)
    return false;
  float x = accelerometer.data.accelX, y = accelerometer.data.accelY, z = accelerometer.data.accelZ;
  float g = sqrtf(x * x + y * y + z * z);

  if (fallState == FALL_SETTLE)
  {
    lastStatusChangeMs = now; // 判定中は眠らせない
    if (g > fallPeakG)
      fallPeakG = g; // 衝撃の山は直後に来ることがあるので、少しの間ピークを追う
    if (now - fallImpactMs >= FALL_SETTLE_MS)
    {
      uint8_t sec = fallPeakG >= FALL_STRONG_G ? FALL_COUNT_STRONG_S
                    : fallPeakG >= FALL_MID_G  ? FALL_COUNT_MID_S
                                               : FALL_COUNT_WEAK_S;
      fallStartAlert(sec, now);
      return true;
    }
    return false;
  }

  // 監視中。GAME/COUNT/SLIDE/SOSは動かす・叩く前提なので見ない
  if (nowStatus == CONTENT && (nowMenuIndex == GAME || nowMenuIndex == COUNT || nowMenuIndex == BLUETOOTH || nowMenuIndex == SOS))
  {
    fallLowMs = 0;
    return false;
  }
  if (g < FALL_FREEFALL_G)
  {
    if (fallLowStartMs == 0)
      fallLowStartMs = now ? now : 1;
    if (now - fallLowStartMs >= FALL_FREEFALL_MIN_MS)
      fallLowMs = now ? now : 1;
  }
  else
    fallLowStartMs = 0;
  if ((long)(now - fallIgnoreUntilMs) < 0 || hapticPulseActive)
    return false;
  bool afterFreefall = fallLowMs != 0 && now - fallLowMs <= FALL_FREEFALL_WIN_MS;
  if (g >= (afterFreefall ? FALL_IMPACT_AFTER_FREEFALL_G : FALL_IMPACT_G))
  {
    fallState = FALL_SETTLE;
    fallImpactMs = now;
    fallPeakG = g;
    fallLowMs = 0;
    lastStatusChangeMs = now;
  }
  return false;
}

// §: SETTING apply
// スマホ設定の画面表示
void renderSettingMode()
{
  set7SegChar(0, 'S', nowBrightness);
  set7SegChar(1, 'E', nowBrightness);
  set7SegChar(2, 'T', nowBrightness);
  setSubLed(0, true, nowBrightness);
  setSubLed(1, false, nowBrightness);
}
// スマホ設定を本体の変数へ反映する。
// live=false(起動時): 電源投入直後(RTCメモリが初期化された時)だけ、ON/OFFの初期値を反映する。
//                     スリープ復帰では、本体で切り替えた状態を保つ。
// live=true(設定画面から): いま反映する。
void orbitCfgApply(bool live)
{
  IDLE_DEEP_SLEEP_MS = (unsigned long)orbitCfg.sleepIdle * 1000UL;
  MENU_DEEP_SLEEP_MS = (unsigned long)orbitCfg.sleepMenu * 1000UL;
  CONTENT_DEEP_SLEEP_MS = (unsigned long)orbitCfg.sleepContent * 1000UL;
  SLIDE_SLEEP_MS = (unsigned long)orbitCfg.sleepSlide * 1000UL;
  POMO_WORK_SEC = (long)orbitCfg.pomoWork * 60;
  POMO_BREAK_SEC = (long)orbitCfg.pomoBreak * 60;
  if (live && !pomoRunning)
  {
    pomoRemainSec = pomoPhase == 0 ? POMO_WORK_SEC : POMO_BREAK_SEC;
  }
  if (live || esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED)
  {
    isManner = orbitCfg.manner;
    isAntiNapEnabled = orbitCfg.antiNap;
    napReset();
    isStationBTrain = orbitCfg.stationB;
  }
  fallDetectorApply();
  Serial.printf("orbitCfg apply(live=%d): manner=%d antinap=%d countdown=%d stationB=%d work=%lds break=%lds mask=0x%03X\n",
                live, orbitCfg.manner, orbitCfg.antiNap, orbitCfg.countDown, orbitCfg.stationB,
                POMO_WORK_SEC, POMO_BREAK_SEC, orbitCfg.menuMask);
}

// 書き込み直したら(ビルドが変わったら)、スマホ設定を初期値に戻す。
// ただしRTCのずれの記録(drift/lsync)は本体の個体差なので残す。
void orbitCfgResetIfNewBuild()
{
  ocPrefs.begin("orbit_cfg", false);
  if (ocPrefs.getString("build", "") != FW_BUILD_ID)
  {
    int32_t drift = ocPrefs.getInt("drift", 0);
    uint32_t lsync = ocPrefs.getUInt("lsync", 0);
    ocPrefs.clear();
    ocPrefs.putInt("drift", drift);
    ocPrefs.putUInt("lsync", lsync);
    ocPrefs.putString("build", FW_BUILD_ID);
    Serial.println("orbit_cfg reset (new firmware)");
  }
  ocPrefs.end();
}

// §: 起動パターン
// 電源が入った時の外周LEDの演出。スマホの設定(orbitCfg.bootAnim)で選ぶ。終わったら呼び出し側がclearLitLedsする。
// 0=くるっと 1=すいせい 2=ぐるっと満タン 3=りょうがわ 4=ぱっ 5=なし
void playBootAnim(uint8_t pat)
{
  nowMillis = millis();
  rtcPrint();
  switch (pat)
  {
  case 1: // すいせい: 尾を引いて1周
  {
    const int TAIL = 6;
    for (int i = 0; i < 60 + TAIL; i++)
    {
      if (i < 60)
        setSecondLed(i, nowBrightness);
      for (int t = 1; t <= TAIL; t++)
      {
        int k = i - t;
        if (k >= 0 && k < 60)
          setSecondLed(k, (uint8_t)((int)nowBrightness * (TAIL - t) / TAIL));
      }
      delay(6);
    }
    break;
  }
  case 2: // ぐるっと満タン: 埋まって、同じ順に消える
    for (int i = 0; i < 60; i++)
    {
      setSecondLed(i, nowBrightness);
      delay(4);
    }
    delay(120);
    for (int i = 0; i < 60; i++)
    {
      setSecondLed(i, 0x00);
      delay(4);
    }
    break;
  case 3: // りょうがわ: 上から左右に分かれて下で出会う
    for (int i = 0; i <= 30; i++)
    {
      setSecondLed(i, nowBrightness);
      setSecondLed((60 - i) % 60, nowBrightness);
      delay(9);
      setSecondLed(i, 0x00);
      setSecondLed((60 - i) % 60, 0x00);
    }
    break;
  case 4: // ぱっ: 全体がふわっと明るくなって消える
    for (int step = 1; step <= 4; step++)
    {
      for (int i = 0; i < 60; i++)
        setSecondLed(i, (uint8_t)((int)nowBrightness * step / 4));
      delay(20);
    }
    for (int step = 3; step >= 0; step--)
    {
      for (int i = 0; i < 60; i++)
        setSecondLed(i, (uint8_t)((int)nowBrightness * step / 4));
      delay(20);
    }
    break;
  case 5: // なし
    break;
  default: // 0: くるっと(従来どおり)
    if (second < 30)
    {
      for (int i = 0; i < 60; i++)
      {
        setSecondLed(i, nowBrightness);
        delay(5);
        setSecondLed(i, 0x00);
      }
      nowMillis = millis();
      rtcPrint();
      for (int i = 0; i < second; i++)
      {
        setSecondLed(i, nowBrightness);
        delay(7);
        setSecondLed(i, 0x00);
      }
    }
    else
    {
      for (int i = 0; i < second; i++)
      {
        setSecondLed(i, nowBrightness);
        delay(5);
        setSecondLed(i, 0x00);
      }
    }
    break;
  }
}

// 設定画面で起動パターンを選んだ時のプレビュー。終わったら外周を消す
void orbitBootPreview(uint8_t pat)
{
  if (pat > 5)
    return;
  playBootAnim(pat);
  clearAllSeconds();
}

// ANTI_NAP用の振動。通常のハプティクス(全開60〜150ms)より強く、全開で長く・何度も鳴らして確実に起こす。
const uint8_t NAP_VIBE_POWER = 255;
const uint16_t NAP_VIBE_ON_MS = 1000;
const uint16_t NAP_VIBE_OFF_MS = 200;
const int NAP_VIBE_COUNT = 5;
void antiNapVibrate()
{
  for (int i = 0; i < NAP_VIBE_COUNT; i++)
  {
    ledcWrite(VIBES_PIN, NAP_VIBE_POWER);
    delay(NAP_VIBE_ON_MS);
    ledcWrite(VIBES_PIN, 0);
    delay(NAP_VIBE_OFF_MS);
  }
}

// §: Set up
void setup()
{
  orbitCfgResetIfNewBuild();
  orbitCfgLoad();       // スマホ設定を読み込む
  orbitCfgApply(false); // 電源投入直後だけ、マナーなどの初期値を反映
  pinMode(LED_PIN, INPUT_PULLUP);
  // digitalWrite(LED_PIN, HIGH);
  pinMode(VIBES_PIN, OUTPUT);
  digitalWrite(VIBES_PIN, LOW);
  pinMode(IS_EN_PIN, OUTPUT);
  digitalWrite(IS_EN_PIN, LOW); // PNP経由で ENピン有効化
  pinMode(SW_PIN, INPUT_PULLUP);
  encoderBegin();
  pinMode(RTC_INT_PIN, INPUT); // RV3028のINTはオープンドレイン、外部プルアップ前提
  digitalRead(CDS_PIN);
  delay(30); // IS31FL3733の電源立ち上がり待ち
  if (!ledcAttach(VIBES_PIN, PWM_FREQ, PWM_RESOLUTION))
  {
    Serial.println("PWM setup failed");
    while (true)
    {
      delay(1000);
    }
  }
  ledcWrite(VIBES_PIN, 0);

  // 手首の動きだけで起きたか(スイッチ・RTCは含まない)。静止確認が済むまでは振動させない。
  bool wokenByWristMotion = false;
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1)
  {
    uint64_t pins = esp_sleep_get_ext1_wakeup_status();
    wokenByWristMotion = (pins & (1ULL << ACC_INT1_PIN)) && !(pins & (1ULL << SW_WAKE_PIN)) && !(pins & (1ULL << RTC_INT_PIN));
  }

  // 起動時はloop()前なので、通常クリックの強さでモーターが回り始める長さだけ直接駆動する。
  if (!isManner && !wokenByWristMotion)
  {
    ledcWrite(VIBES_PIN, PRESS_FEEDBACK_POWER);
    delay(45);
    ledcWrite(VIBES_PIN, 0);
  }

  // Serial.begin(115200);

  Serial.println("=== BOOT ===");

  waitForRtcI2c();
  Wire.setClock(400000);

  initDisplay();

  // 裏でどれくらい起きているかが分かるように、起きたら外周(orbit ring)を一瞬だけ全点灯する。
  // 動きだけの空振り起床・RTC周期起床でも光る。不要になったら ORBIT_WAKE_FLASH を false に。
  if (ORBIT_WAKE_FLASH)
  {
    for (int i = 0; i < 60; i++)
      setSecondLed(i, 0x30);
    delay(40);
    clearAllSeconds();
  }

  // * RTC begin
  // rtcSetTime(2026, 10, 18, 21, 36, 2, 0); // 必要な場合だけ手動でRTC時刻を設定

  pinMode(ACC_INT1_PIN, INPUT); // スリープ前の姿勢解除待ちで読むため、早めに入力にしておく
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  bool wokenByRtcTimer = false;
  if (wakeupReason == ESP_SLEEP_WAKEUP_EXT1)
  {
    uint64_t wakeupPins = esp_sleep_get_ext1_wakeup_status();
    if (wakeupPins & (1ULL << RTC_INT_PIN))
    {
      wokenByRtcTimer = true;
    }
  }
  bool rtcTimerFired = rtcConsumeTimerFlag();
  lastMinute = -1; // 起動直後は必ず時刻を描画する
  lastSecond = -1; // 起動直後は必ず1回描画する
  // rtcPrint()は30ms以内の再読込をスキップするので、確実にRTCを読み直してから判定する
  nowMillis = millis();
  lastWireMillis = nowMillis - 1000;
  rtcPrint();
  bool pomoWake = false;
  if (wokenByRtcTimer && rtcTimerFired && pomoRunning && pomodoroRemainingSec() <= 2)
  {
    // ポモドーロの区切りで起きた: 振動して次のフェーズを開始し、ポモドーロ画面へ
    pomoWake = true;
    Serial.println("Woke by RTC timer - pomodoro phase end");
    pomodoroAdvance();
  }
  else if (wokenByRtcTimer && rtcTimerFired && wristRetryArmed && (wristRaiseInFifo() || sleepFallPending()))
  {
    // 動きが止まらず手首起床を外して寝た後の見直し起床で、その間に腕を上げていた(または転倒らしい動きがあった)
    Serial.println("Woke by RTC retry - wrist raised / fall");
    wristRetryArmed = false;
    if (!isManner)
    {
      ledcWrite(VIBES_PIN, PRESS_FEEDBACK_POWER);
      delay(45);
      ledcWrite(VIBES_PIN, 0);
    }
  }
  else if (wokenByRtcTimer && rtcTimerFired)
  {
    wristRetryArmed = false;
    // ANTI_NAPの周期起床、またはポモドーロ終了前の早起き。後者は振動せずそのまま寝直す。
    if (isAntiNapEnabled)
    {
      if (napDetectMovement())
      {
        napStillCount = 0; // 動いている=起きている
      }
      else if (++napStillCount >= NAP_STILL_LIMIT)
      {
        Serial.println("Woke by RTC timer - no movement, possible nap, vibrating");
        napStillCount = 0; // 動かないままなら、次の30秒後にもう一度
        antiNapVibrate();
      }
    }
    wristFifoKeep = true; // スリープ中の履歴を途切れさせない
    enterDeepSleepIfIdle();
    wristFifoKeep = false;
  }
  if (wokenByWristMotion)
  {
    if (!wristHoldConfirm())
    {
      Serial.println("Wrist motion without hold - back to sleep");
      configureBma400ActivityWakeup(); // beginI2Cで設定が戻っている可能性があるので入れ直す
      wristFifoKeep = true;
      enterDeepSleepIfIdle();
      wristFifoKeep = false;
    }
    else if (!isManner)
    {
      ledcWrite(VIBES_PIN, PRESS_FEEDBACK_POWER);
      delay(45);
      ledcWrite(VIBES_PIN, 0);
    }
  }
  if (!wokenByRtcTimer)
    napReset();              // 手首・スイッチで起きた=人が操作した
  resetDailyCountIfNewDay(); // スリープ中に日付が変わった場合、今回の歩数加算より先にリセット

#ifdef WRIST_CALIB_MODE
  {
    // * BMA400 begin
    bool bmaReady = false;
    for (int i = 0; i < 10 && !bmaReady; i++)
    {
      bmaReady = (accelerometer.beginI2C(BMA400_I2C_ADDRESS_DEFAULT) == BMA400_OK);
      if (!bmaReady)
      {
        Serial.println("BMA400 not found, retrying...");
        delay(500);
      }
    }
    if (bmaReady)
    {
      Serial.println("BMA400 connected");
      configureBma400ActivityWakeup();
    }

    // 手首を上げる姿勢の実測用。シリアルモニタ(115200)にX/Y/Zをg単位で流し続ける。
    Serial.begin(115200);
    delay(500);
    accelerometer.setODR(BMA400_ODR_100HZ);
    while (true)
    {
      printBma400Accel();
      delay(100);
    }
  }
#endif

#ifdef TAP_LOG_MODE
  {
    // タップ実測用。シリアルモニタ(115200)に ms,x,y,z(mg),sum を100Hzで流し続ける。CSVとして保存して解析に渡す。
    Serial.begin(115200);
    delay(500);
    bool bmaReady = false;
    for (int i = 0; i < 10 && !bmaReady; i++)
    {
      bmaReady = (accelerometer.beginI2C(BMA400_I2C_ADDRESS_DEFAULT) == BMA400_OK);
      if (!bmaReady)
        delay(500);
    }
    accelerometer.setRange(BMA400_RANGE_4G);
    accelerometer.setODR(BMA400_ODR_100HZ);
    Serial.println("ms,x,y,z,sum"); // x,y,z は mg(整数)。sum は先頭4列の和の下位16bit(破損行の除去用)
    unsigned long next = millis();
    while (true)
    {
      if (accelerometer.getSensorData() == BMA400_OK)
      {
        // 1行を1回で書いて、行の途中で欠けにくくする
        long ms = (long)millis();
        int x = (int)lroundf(accelerometer.data.accelX * 1000.0f);
        int y = (int)lroundf(accelerometer.data.accelY * 1000.0f);
        int z = (int)lroundf(accelerometer.data.accelZ * 1000.0f);
        char buf[48];
        int n = snprintf(buf, sizeof(buf), "%ld,%d,%d,%d,%ld\n", ms, x, y, z, (ms + x + y + z) & 0xFFFF);
        Serial.write((const uint8_t *)buf, n);
      }
      next += 10;
      while ((long)(millis() - next) < 0)
        ;
    }
  }
#endif

  // * 起動orbitサイクル（initDisplay()で全消灯済み）
  nowMillis = millis();
  rtcPrint();
  // analogWrite(VIBES_PIN, 0);
  nowBrightness = cdsBrightness(true, nowBrightness);
  // 明るさが変わった場合、点灯中のLEDにもすぐ反映する。
  updateLitLedBrightness(-1, nowBrightness);
  bool fallWake = sleepFallPending(); // スリープ中の転倒で起きた: すぐカウントダウンを出す
  if (!pomoWake && !fallWake)
    playBootAnim(orbitCfg.bootAnim);

  clearLitLeds();

  // 時間表示を急ぐため、センサー/RTC設定は点灯後に行う。
  rtcConfigureBackupSwitchover();
  bool bmaReady = false;
  for (int i = 0; i < 10 && !bmaReady; i++)
  {
    bmaReady = (accelerometer.beginI2C(BMA400_I2C_ADDRESS_DEFAULT) == BMA400_OK);
    if (!bmaReady)
    {
      Serial.println("BMA400 not found, retrying...");
      delay(500);
    }
  }
  if (bmaReady)
  {
    Serial.println("BMA400 connected");
    configureBma400ActivityWakeup();
    wristSeedDownRef(); // 下ろした判定の基準を起床時から持たせる
    syncAndResetStepCount(true);
    fallSensorOk = true;
    fallDetectorApply(); // 転倒検知がオンなら100Hzで監視を始める
    if (fallWake && fallDetectOn)
    {
      Serial.println("Woke by fall during sleep");
      fallStartAlert(FALL_COUNT_MID_S, millis()); // 衝撃の強さは分からないので中くらいの猶予
    }
  }
  sleepFallSeen = false;
  pinMode(ACC_INT1_PIN, INPUT);
  Serial.println("Setup complete");
  Serial.printf("Total steps after wake: %lu\n", (unsigned long)totalStepCount);
  lastIdleActivityMs = millis();
  if (pomoWake)
  {
    gotoPomodoroContent();
  }

  // while (true)
  // {
  //   displayTime(13, 54, 36, 0xFF);
  // } // Arduino-ESP32 Core 3.x API
}

// ON/OFF切替の目標状態。エンコーダのみ(左回し=ON/右回し=OFF)。ボタンでは切り替えない。
bool toggleWanted(SwitchEvent ev, int encSteps, bool current)
{
  if (encSteps < 0)
    return true;
  if (encSteps > 0)
    return false;
  return current;
}

// §: GAME submenu
// GAMEメニューの中で遊びを選ぶ。エンコーダーで選択、短押しで開始、長押しで選択画面へ戻る(選択画面での長押しはメニューへ)。
enum GameSub
{
  GAME_SEL, // 選択画面
  GAME_GRAV,
  GAME_SAFE,
  GAME_MOLE,
  GAME_HILO
};
uint8_t gameSub = GAME_SEL;
uint8_t gameSelIdx = 0; // GameSub - 1 (0=GRAV 1=SAFE 2=MOLE 3=HILO)
const uint8_t GAME_SUB_N = 4;
// 名前は4文字を超えてもよい(メニューと同じくスクロール表示する)
const char *const GAME_SUB_NAMES[GAME_SUB_N] = {"GRAVITY", "SAFE CRACKER", "WHACK A MOLE", "HIGH LOW"};

void gameRenderSelect()
{
  clearLitLeds();
  setSecondLedSegment(GAME_SUB_N, gameSelIdx, nowBrightness);
  menuScrollStartMs = millis(); // 項目名のスクロールを頭から
  scrollTextBySecond(GAME_SUB_NAMES[gameSelIdx], nowBrightness, 0);
}

// §: SAFE(金庫破り)
// 隠れた目標位置を、エンコーダーでリングのカーソルを回して探す。近いほど振動が速くなる。短押しで解錠を試す。
const uint8_t SAFE_HINT_RANGE = 15;            // この距離(LED数)以内で振動が始まる
const uint16_t SAFE_PULSE_MS = 60;             // 振動1回の長さ(これより短いと今のモーターは動かない)
const uint16_t SAFE_PULSE_GAP_MIN_MS = 60;     // 目標ちょうどの時の休み
const uint16_t SAFE_PULSE_GAP_PER_LED_MS = 50; // 1LED離れるごとに増やす休み
const unsigned long SAFE_PENALTY_MS = 3000;    // 外した時に足すタイム
uint8_t safeTarget = 0;
uint8_t safeCursor = 0;
bool safeWon = false;
unsigned long safeStartMs = 0;
unsigned long safePenaltyMs = 0;
unsigned long safeNextPulseMs = 0;
int safeShownSec = -1;

uint8_t safeDistance()
{
  int d = abs((int)safeCursor - (int)safeTarget);
  return (uint8_t)(d > 30 ? 60 - d : d);
}

void safeShowTime(unsigned long ms)
{
  displayNumber4Digits((uint32_t)(ms / 1000), nowBrightness);
}

void safeStart()
{
  clearLitLeds();
  safeTarget = esp_random() % 60;
  safeCursor = (safeTarget + 20 + esp_random() % 21) % 60; // 目標から20〜40個離れた所から始める
  safeWon = false;
  safeStartMs = millis();
  safePenaltyMs = 0;
  safeNextPulseMs = 0;
  safeShownSec = 0;
  setSecondLed(safeCursor, nowBrightness);
  safeShowTime(0);
}

void safeUpdate(int encSteps, SwitchEvent ev)
{
  if (safeWon)
  {
    if (ev == SW_SINGLE)
      safeStart(); // もう一度
    return;
  }

  if (encSteps != 0)
  {
    uint8_t previous = safeCursor;
    int c = ((int)safeCursor + encSteps) % 60;
    if (c < 0)
      c += 60;
    safeCursor = (uint8_t)c;
    setSecondLed(previous, 0);
    setSecondLed(safeCursor, nowBrightness);
  }

  unsigned long elapsed = nowMillis - safeStartMs + safePenaltyMs;
  if ((int)(elapsed / 1000) != safeShownSec)
  {
    safeShownSec = (int)(elapsed / 1000);
    safeShowTime(elapsed);
  }

  uint8_t d = safeDistance();
  if (d <= SAFE_HINT_RANGE && nowMillis >= safeNextPulseMs)
  {
    startHapticPulse(MOTOR_POWER, SAFE_PULSE_MS);
    safeNextPulseMs = nowMillis + SAFE_PULSE_MS + SAFE_PULSE_GAP_MIN_MS + (unsigned long)d * SAFE_PULSE_GAP_PER_LED_MS;
  }

  if (ev == SW_SINGLE)
  {
    if (d == 0)
    {
      // 解錠: 外周を一周塗って、かかったタイムを出す
      safeWon = true;
      uint8_t blinkBrightness = nowBrightness;
      for (uint8_t i = 0; i < 60; i++)
      {
        setSecondLed((safeTarget + i) % 60, blinkBrightness);
        delay(8);
      }
      safeShowTime(elapsed);
      pulseVibes(180);
    }
    else
    {
      // 外れ: タイムを加算してカーソルを点滅、長めに振動
      safePenaltyMs += SAFE_PENALTY_MS;
      elapsed += SAFE_PENALTY_MS;
      safeShownSec = (int)(elapsed / 1000);
      safeShowTime(elapsed);
      startHapticPulse(MOTOR_POWER, 250);
      setSecondLed(safeCursor, 0);
      delay(80);
      setSecondLed(safeCursor, nowBrightness);
    }
  }
}

// §: MOLE(モグラ叩き)
// リングのどこかに出て点滅するモグラを、エンコーダーでカーソルを合わせて短押しで叩く。ミス3回で終了。
const uint8_t MOLE_STEP = 2;                     // 1クリックで進むLED数
const uint8_t MOLE_HIT_TOL = 1;                  // カーソルは3LED幅(中心±1)。叩いた時にモグラがこの距離以内なら当たり
const uint8_t MOLE_LIVES = 3;                    // 残機。コロン・サブLED2個で表示する
const unsigned long MOLE_WINDOW_START_MS = 1300; // モグラが出ている時間(最初)
const unsigned long MOLE_WINDOW_MIN_MS = 450;    // 同(最短)
const unsigned long MOLE_WINDOW_SHRINK_MS = 40;  // 1匹叩くごとに短くする量
const unsigned long MOLE_GAP_MIN_MS = 300;       // 次のモグラまでの間(最短)
const unsigned long MOLE_GAP_RAND_MS = 400;      // 同(ばらつき)
const unsigned long MOLE_BLINK_MS = 100;
uint8_t moleCursor = 0;
uint8_t moleLed = 0;
uint8_t moleLives = 0;
uint16_t moleScore = 0;
bool moleActive = false;
bool moleOver = false;
bool moleBlinkOn = false;
unsigned long moleDeadlineMs = 0;
unsigned long moleSpawnMs = 0;
unsigned long moleBlinkMs = 0;
int moleDrawn[4] = {-1, -1, -1, -1};

uint8_t circDist(uint8_t a, uint8_t b)
{
  int d = abs((int)a - (int)b);
  return (uint8_t)(d > 30 ? 60 - d : d);
}

void moleShowLives()
{
  setColon(moleLives >= 1, nowBrightnessCoron);
  setSubLed(0, moleLives >= 2, nowBrightness);
  setSubLed(1, moleLives >= 3, nowBrightness);
}

// カーソル(暗め・常時点灯・3LED幅)とモグラ(明るく点滅)を描き直す。カーソルを先に描いて、モグラを上に重ねる。
// モグラがカーソルの中にいる(=当たる位置)時は、点滅が「明るい↔カーソルの暗さ」になる。
void moleRedraw()
{
  for (uint8_t i = 0; i < 4; i++)
  {
    if (moleDrawn[i] >= 0)
      setSecondLed((uint8_t)moleDrawn[i], 0);
    moleDrawn[i] = -1;
  }
  for (int8_t k = -1; k <= 1; k++)
  {
    uint8_t led = (moleCursor + 60 + k) % 60;
    setSecondLed(led, nowBrightness / 3 + 1);
    moleDrawn[k + 1] = led;
  }
  if (moleActive && moleBlinkOn)
  {
    setSecondLed(moleLed, nowBrightness);
    moleDrawn[3] = moleLed;
  }
}

void moleStart()
{
  clearLitLeds();
  moleDrawn[0] = moleDrawn[1] = moleDrawn[2] = moleDrawn[3] = -1;
  moleCursor = 0;
  moleLives = MOLE_LIVES;
  moleScore = 0;
  moleActive = false;
  moleOver = false;
  moleSpawnMs = millis() + 600;
  displayNumber4Digits(0, nowBrightness);
  moleShowLives();
  moleRedraw();
}

void moleMiss(unsigned long now)
{
  if (moleLives > 0)
    moleLives--;
  moleActive = false;
  startHapticPulse(MOTOR_POWER, 250);
  if (moleLives == 0)
  {
    moleOver = true;
    pulseVibes(250);
  }
  else
  {
    moleSpawnMs = now + MOLE_GAP_MIN_MS + esp_random() % MOLE_GAP_RAND_MS;
  }
  moleShowLives();
  moleRedraw();
}

void moleUpdate(int encSteps, SwitchEvent ev)
{
  if (moleOver)
  {
    if (ev == SW_SINGLE)
      moleStart(); // もう一度
    return;
  }
  unsigned long now = nowMillis;
  bool dirty = false;

  if (encSteps != 0)
  {
    int c = ((int)moleCursor + encSteps * MOLE_STEP) % 60;
    if (c < 0)
      c += 60;
    moleCursor = (uint8_t)c;
    dirty = true;
  }

  if (!moleActive)
  {
    if (now >= moleSpawnMs)
    {
      // カーソルから4〜15個離れた所に出す(左右ランダム)
      uint8_t off = 4 + esp_random() % 12;
      moleLed = (moleCursor + ((esp_random() & 1) ? off : 60 - off)) % 60;
      unsigned long window = MOLE_WINDOW_START_MS;
      unsigned long shrink = (unsigned long)moleScore * MOLE_WINDOW_SHRINK_MS;
      window = (shrink >= MOLE_WINDOW_START_MS - MOLE_WINDOW_MIN_MS) ? MOLE_WINDOW_MIN_MS : MOLE_WINDOW_START_MS - shrink;
      moleActive = true;
      moleBlinkOn = true;
      moleBlinkMs = now;
      moleDeadlineMs = now + window;
      dirty = true;
    }
  }
  else
  {
    if (ev == SW_SINGLE)
    {
      if (circDist(moleCursor, moleLed) <= MOLE_HIT_TOL)
      {
        moleScore++;
        moleActive = false;
        moleSpawnMs = now + MOLE_GAP_MIN_MS + esp_random() % MOLE_GAP_RAND_MS;
        startHapticPulse(MOTOR_POWER, 60);
        displayNumber4Digits(moleScore, nowBrightness);
        dirty = true;
      }
      else
      {
        moleMiss(now);
        return;
      }
    }
    else if (now >= moleDeadlineMs)
    {
      moleMiss(now);
      return;
    }
    else if (now - moleBlinkMs >= MOLE_BLINK_MS)
    {
      moleBlinkMs = now;
      moleBlinkOn = !moleBlinkOn;
      dirty = true;
    }
  }
  if (dirty)
    moleRedraw();
}

// §: HILO(ハイ&ロー)
// 7セグの数字(1〜13)より次が高いか低いかを、エンコーダー右=HIGH/左=LOWで選び、短押しでめくる。外れるまで連勝。
int8_t hiloGuess = 0; // 1=HIGH -1=LOW 0=未選択
uint8_t hiloNum = 1;
uint16_t hiloStreak = 0;
bool hiloOver = false;

void hiloShowNum(uint8_t n)
{
  // set7SegChar()は数字を表示できない(A〜Zのみ)ので、十の位はset7SegDigit()で出す
  if (n >= 10)
    set7SegDigit(2, (n / 10) % 10, nowBrightness);
  else
    set7SegChar(2, ' ', nowBrightness);
  set7SegDigit(3, n % 10, nowBrightness);
}

void hiloShowGuess()
{
  set7SegChar(0, hiloGuess > 0 ? 'H' : (hiloGuess < 0 ? 'L' : '-'), nowBrightness);
  set7SegChar(1, ' ', nowBrightness);
}

void hiloStart()
{
  clearLitLeds();
  hiloNum = 1 + esp_random() % 13;
  hiloStreak = 0;
  hiloGuess = 0;
  hiloOver = false;
  hiloShowGuess();
  hiloShowNum(hiloNum);
}

void hiloUpdate(int encSteps, SwitchEvent ev)
{
  if (hiloOver)
  {
    if (ev == SW_SINGLE)
      hiloStart(); // もう一度
    return;
  }
  if (encSteps != 0)
  {
    hiloGuess = (encSteps > 0) ? 1 : -1;
    hiloShowGuess();
  }
  if (ev != SW_SINGLE || hiloGuess == 0)
    return;

  // めくる演出(パラパラ変わって止まる)
  for (uint8_t i = 0; i < 10; i++)
  {
    hiloShowNum(1 + esp_random() % 13);
    delay(40 + i * 12);
  }
  uint8_t next = 1 + esp_random() % 13;
  hiloShowNum(next);

  if (next == hiloNum)
  {
    startHapticPulse(MOTOR_POWER, 60); // 同じ数字は引き分け
  }
  else if ((next > hiloNum) == (hiloGuess > 0))
  {
    hiloStreak++;
    if (hiloStreak <= 60)
      setSecondLed(hiloStreak - 1, nowBrightness);
    startHapticPulse(MOTOR_POWER, 60);
  }
  else
  {
    hiloOver = true;
    startHapticPulse(MOTOR_POWER, 250);
    clearDisplayExceptSeconds();
    // 終了画面: 左端'S'+連勝数(99まで)。数字が最後にめくった数と区別できるようにする
    uint8_t shown = hiloStreak > 99 ? 99 : hiloStreak;
    set7SegChar(0, 'S', nowBrightness);
    if (shown >= 10)
      set7SegDigit(2, shown / 10, nowBrightness);
    set7SegDigit(3, shown % 10, nowBrightness);
  }
  hiloNum = next;
  hiloGuess = 0;
  if (!hiloOver)
    hiloShowGuess();
}

// §: Encoder
ESP32Encoder encoder;
const int ENC_COUNTS_PER_DETENT = 4; // EC05E1220401: 1クリック=A/Bの1周期=フル4逓倍で4カウント
void encoderBegin()
{
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  encoder.attachFullQuad(ENC_A_PIN, ENC_B_PIN);
  encoder.clearCount();
}
// 前回からのクリック数を返す(正:右回し/負:左回し。向きが逆ならENC_A_PINとENC_B_PINを入れ替える)
int encoderReadDetents()
{
  static int64_t lastCount = 0;
  int64_t count = encoder.getCount();
  int detents = (int)((count - lastCount) / ENC_COUNTS_PER_DETENT);
  lastCount += (int64_t)detents * ENC_COUNTS_PER_DETENT;
  return detents;
}

// MENUで選択中の項目が「オン/オフ」を持つ場合、緑(オン)/赤(オフ)のサブLEDで示す。それ以外は消灯。
// 描画は変化したときだけ(I2C書き込みを減らす)。
void updateMenuStateLed()
{
  bool toggleable = nowMenuIndex == MANNER || nowMenuIndex == ANTI_NAP || nowMenuIndex == POMODORO ||
                    nowMenuIndex == SETTING || nowMenuIndex == BLUETOOTH;
  bool on = toggleable && isMenuInUse(nowMenuIndex);
  bool off = toggleable && !on;
  uint8_t g = on ? nowBrightness : 0;
  uint8_t r = off ? nowBrightness : 0;
  if ((ledShadow[MAP_SUB[0]] != 0) != on)
    setSubLed(0, on, g);
  if ((ledShadow[MAP_SUB[1]] != 0) != off)
    setSubLed(1, off, r);
}

// §: Wrist lowered
// 時計表示中に腕を下ろしたら true(すぐ眠らせる)。
// 画面を見る向き(Z<WRIST_RAISE_VIEW_Z_MAX)で止まっている間の向きを基準として覚え、そこから
// WRIST_DOWN_ANGLE_DEG 以上傾いた状態が WRIST_DOWN_MS 続いたら下ろしたとみなす。
// 腕を垂らす(画面が横向き)だけでなく、机や膝に手を戻した時の傾きでも消える。
// 一度も見る向きで止まっていなければ働かない(スイッチで起こした時に腕が横向きでも、すぐには消えない)。
const int WRIST_DOWN_STEADY_N = 6;
float wristDownEma[3];
float wristDownWin[WRIST_DOWN_STEADY_N][3];
int wristDownWinCount = 0;
unsigned long wristDownSince = 0;
unsigned long wristDownLastSample = 0;
unsigned long wristDownLastMoveMs = 0;
bool wristLoweredCheck(unsigned long now)
{
  if (nowStatus != IDLE || !fallSensorOk || orbitApActive || orbitBleActive || fallState != FALL_WATCH)
  {
    wristDownHasRef = false;
    wristDownWinCount = 0;
    wristDownSince = 0;
    return false;
  }
  if (now - wristDownLastSample < WRIST_DOWN_SAMPLE_MS)
    return false;
  wristDownLastSample = now;
  if (accelerometer.getSensorData() != BMA400_OK)
    return false;
  const float s[3] = {accelerometer.data.accelX, accelerometer.data.accelY, accelerometer.data.accelZ};
  if (wristDownWinCount > 0 &&
      (fabsf(s[0] - wristDownEma[0]) > WRIST_DOWN_MOVE_G || fabsf(s[1] - wristDownEma[1]) > WRIST_DOWN_MOVE_G ||
       fabsf(s[2] - wristDownEma[2]) > WRIST_DOWN_MOVE_G))
    wristDownLastMoveMs = now;
  bool movedRecently = wristDownLastMoveMs != 0 && now - wristDownLastMoveMs <= WRIST_DOWN_MOVE_WINDOW_MS;
  for (int ax = 0; ax < 3; ax++) // 細かい揺れをならす
    wristDownEma[ax] = wristDownWinCount == 0 ? s[ax] : wristDownEma[ax] * 0.7f + s[ax] * 0.3f;
  memcpy(wristDownWin[wristDownWinCount % WRIST_DOWN_STEADY_N], wristDownEma, sizeof(wristDownEma));
  wristDownWinCount++;
  const float *e = wristDownEma;
  float n = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
  if (n < 0.01f)
    return false;

  bool steady = wristDownWinCount >= WRIST_DOWN_STEADY_N;
  for (int ax = 0; ax < 3 && steady; ax++)
  {
    float lo = 99, hi = -99;
    for (int i = 0; i < WRIST_DOWN_STEADY_N; i++)
    {
      lo = min(lo, wristDownWin[i][ax]);
      hi = max(hi, wristDownWin[i][ax]);
    }
    steady = (hi - lo) < WRIST_DOWN_STEADY_G;
  }
  float c = 1.0f; // 基準との角度(cos)
  if (wristDownHasRef)
  {
    float rn = sqrtf(wristDownRef[0] * wristDownRef[0] + wristDownRef[1] * wristDownRef[1] + wristDownRef[2] * wristDownRef[2]);
    c = (e[0] * wristDownRef[0] + e[1] * wristDownRef[1] + e[2] * wristDownRef[2]) / (n * rn);
  }
  const float cosMoved = cosf(WRIST_DOWN_MOVED_ANGLE_DEG * PI / 180.0f);
  // 見る向きで止まっていれば基準を更新。ただし動いた直後に基準から大きく外れた所で止まった時(=机に置いた)は更新しない
  if (steady && e[2] / n < WRIST_RAISE_VIEW_Z_MAX && n > 0.85f && n < 1.15f &&
      (!wristDownHasRef || c > cosMoved || !movedRecently))
  {
    memcpy(wristDownRef, e, sizeof(wristDownRef)); // 見ている向きを基準にする
    wristDownHasRef = true;
    wristDownSince = 0;
    return false;
  }
  if (!wristDownHasRef)
    return false;
  bool lowered = c < cosf(WRIST_DOWN_ANGLE_DEG * PI / 180.0f) || (movedRecently && c < cosMoved);
  if (!lowered)
  {
    wristDownSince = 0;
    return false;
  }
  if (wristDownSince == 0)
    wristDownSince = now;
  return now - wristDownSince >= WRIST_DOWN_MS;
}

// §: loop()
void loop()
{
  // *毎ループ初期処理
  nowMillis = millis();
  updateHaptic();
  rtcPrint();
  resetDailyCountIfNewDay(); // 起きている間に日付が変わった場合
  SwitchEvent swEvent = checkSwitchEvent();
  int encSteps = encoderReadDetents();
  if (encSteps != 0)
  {
    lastStatusChangeMs = nowMillis; // 回している間はスリープしない
  }

  //* 明るさ調節
  uint8_t previousBrightness = nowBrightness;
  nowBrightness = cdsBrightness(false, nowBrightness);
  // 明るさが変わった場合、点灯中のLEDにもすぐ反映する。
  updateLitLedBrightness(previousBrightness, nowBrightness);

  //* スイッチの入力取得
  if (swEvent != SW_NONE)
  {
    lastIdleActivityMs = nowMillis;
  }

  //* 転倒検知(カウントダウン中は画面を占有する)
  if (fallUpdate(swEvent, encSteps, nowMillis))
  {
    delay(10);
    return;
  }

  //* ポモドーロのフェーズ終了(起きている間): 振動して次のフェーズへ、ポモドーロ画面を表示
  if (pomoRunning && pomodoroRemainingSec() <= 0)
  {
    pomodoroAdvance();
    gotoPomodoroContent();
  }

  //* ステータスによる処理
  switch (nowStatus)
  {

  case IDLE:

    // display 描画

    if (nowMillis - lastChangeSecondMillis > 500)
    {
      setColon(false, nowBrightnessCoron);
    }

    if (lastSecond != second)
    {
      lastSecond = second;
      setSecondLed(second - 1, 0x00);
      setSecondLed(second, nowBrightness);
      // コロン点滅（偶数秒で点灯）
      // setColon(second % 2 == 0, nowBrightness);

      lastChangeSecondMillis = nowMillis;
      setColon(true, nowBrightnessCoron);
    }
    if (lastMinute != minute)
    {
      lastMinute = minute;
      clearLitLeds();
      displayTime(hour, minute, second, nowBrightness);
      setSecondLed(second, nowBrightness);
    }
    if (SwitchEvent::SW_SINGLE == swEvent || SwitchEvent::SW_DOUBLE == swEvent || encSteps != 0)
    {
      setStatus(MENU);
      // 左(マイナス方向)に回して入ったら最後のコンテンツ、それ以外は先頭
      nowMenuIndex = menuOrderAt(encSteps < 0 ? menuCount() - 1 : 0);
      menuScrollStartMs = millis();
      lastStatusChangeMs = nowMillis;
      clearLitLeds();
      setSecondLedSegment(menuCount(), menuPosOf(nowMenuIndex), nowBrightness);
      // set7SegChar(0, MenusName[nowMenuIndex][0], nowBrightness); // 'S'
      // set7SegChar(1, MenusName[nowMenuIndex][1], nowBrightness); // 'T'
      // set7SegChar(2, MenusName[nowMenuIndex][2], nowBrightness); // 'E'
      // set7SegChar(3, MenusName[nowMenuIndex][3], nowBrightness); // 'P'

      scrollTextBySecond(MenusName[nowMenuIndex], nowBrightness, second);
    }
    if (SwitchEvent::SW_LONG == swEvent)
    {
      // 符号なし演算で「ちょうど無操作時間が満了した」状態にする(起動直後でも正しく働く)
      lastStatusChangeMs = nowMillis - IDLE_DEEP_SLEEP_MS;
      // スリープ移行の合図として長押し振動を出し、振動が終わったらすぐスリープへ
      startHapticPulse(PRESS_FEEDBACK_POWER, PRESS_FEEDBACK_MS * 3 / 2);
      while (hapticPulseActive)
      {
        updateHaptic();
        delay(1);
      };
    }
    break;
  case MENU:

    scrollTextBySecond(MenusName[nowMenuIndex], nowBrightness, second);
    updateMenuStateLed();

    if (encSteps != 0)
    {
      int menuStep = encSteps; // メニューはエンコーダーで選ぶ
      int menuPos = ((menuPosOf(nowMenuIndex) + menuStep) % menuCount() + menuCount()) % menuCount();
      nowMenuIndex = menuOrderAt(menuPos);
      menuScrollStartMs = millis();
      clearLitLeds();
      setSecondLedSegment(menuCount(), menuPosOf(nowMenuIndex), nowBrightness);
      // set7SegChar(0, MenusName[nowMenuIndex][0], nowBrightness); // 'S'
      // set7SegChar(1, MenusName[nowMenuIndex][1], nowBrightness); // 'T'
      // set7SegChar(2, MenusName[nowMenuIndex][2], nowBrightness); // 'E'
      // set7SegChar(3, MenusName[nowMenuIndex][3], nowBrightness); // 'P'
    }
    else if (SwitchEvent::SW_SINGLE == swEvent)
    {
      // 単押し 決定
      setStatus(CONTENT);
      clearLitLeds();
      syncAndResetStepCount(true);
      switch (nowMenuIndex)
      {
      case STEPS:
        // step
        displayNumber4Digits(totalStepCount, nowBrightness);
        for (int i = 0; i < totalStepCount % 60; i++)
        {
          setSecondLed(i, nowBrightness);
        }
        break;
      case DIET:
        // diet
        displayNumber4Digits(totalKcal, nowBrightness);
        break;
      case ANTI_NAP:
        // anti nap
        if (isAntiNapEnabled)
        {
          set7SegDigit(1, 10, nowBrightness); // 'S'
          set7SegChar(2, 'O', nowBrightness); // 'E'
          set7SegChar(3, 'N', nowBrightness); // 'P'
          setSubLed(0, true, nowBrightness);
          setSubLed(1, false, nowBrightness);
        }
        else
        {
          set7SegChar(1, 'O', nowBrightness); // 'T'
          set7SegChar(2, 'F', nowBrightness); // 'E'
          set7SegChar(3, 'F', nowBrightness); // 'P'
          setSubLed(0, false, nowBrightness);
          setSubLed(1, true, nowBrightness);
        }
        break;
      case LIGHT:
        if (!isLedOn)
          lightLevel = 0; // SOS等でLEDが消えている場合に表示とずれないように
        lightRender();
        break;
      case STOP_WATCH:
        // stop watch
        set7SegChar(0, 'S', nowBrightness); // 'S'
        set7SegChar(1, 'T', nowBrightness); // 'T'
        set7SegChar(2, 'A', nowBrightness); // 'A'
        set7SegChar(3, 'R', nowBrightness); // 'R
        setSubLed(0, false, nowBrightness);
        setSubLed(1, true, nowBrightness);
        break;
      case MORSE:
        nowMorseDotIndex = 0;
        nowMorseIndex = 0;
        isDoneNowMorse = true;
        break;
      case TRAIN:
        analogWrite(VIBES_PIN, 0);
        if (isStationBTrain)
        {
          scrollText("STA2", nowBrightness, 130, true);
        }
        else
        {
          scrollText("STA1", nowBrightness, 130, true);
        };
        clearLitLeds();
        break;
      case GAME:
        gameSub = GAME_SEL;
        gameRenderSelect();
        break;
      case ROULETTE:
        roulettePhase = 0;
        rouletteLed = 0;
        rouletteNextStepMs = millis();
        clearAllSeconds();
        setSecondLed(rouletteLed, nowBrightness);
        set7SegChar(0, 'G', nowBrightness);
        set7SegChar(1, 'O', nowBrightness);
        set7SegChar(2, ' ', nowBrightness);
        set7SegChar(3, ' ', nowBrightness);
        break;
      case MANNER:
        if (isManner)
        {
          set7SegDigit(1, 10, nowBrightness); // 'S'
          set7SegChar(2, 'O', nowBrightness); // 'E'
          set7SegChar(3, 'N', nowBrightness); // 'P'
          setSubLed(0, true, nowBrightness);
          setSubLed(1, false, nowBrightness);
        }
        else
        {
          set7SegChar(1, 'O', nowBrightness); // 'T'
          set7SegChar(2, 'F', nowBrightness); // 'E'
          set7SegChar(3, 'F', nowBrightness); // 'P'
          setSubLed(0, false, nowBrightness);
          setSubLed(1, true, nowBrightness);
        }
        break;
      case POMODORO:
        renderPomodoro(true);
        break;
      case COUNT:
        countStart();
        break;
      case SETTING:
        orbitApStart(); // スマホ設定用のWi-Fiを開始
        renderSettingMode();
        break;
      case SOS:
        sosStart(nowMillis);
        break;
      case UPLOAD:
        clearLitLeds();
        menuScrollStartMs = millis(); // "UPLOAD"を頭から流す(流し続けるのはupdate側)
        break;
      case BLUETOOTH:
        orbitBleStart(); // BLEキーボードとして広告開始
        tapDetectorStart();
        knockDetectorReset();
        slideLastActivityMs = nowMillis;
        slideEnterSelect(); // まず用途(スライド/ショート動画)を選ぶ
        break;
      }
    }
    else if (SwitchEvent::SW_LONG == swEvent)
    {
      // 長押し処理 戻る
      setStatus(IDLE);
      clearLitLeds();
      displayTime(hour, minute, second, nowBrightness);
      setSecondLed(second, nowBrightness);
      lastStatusChangeMs = nowMillis;
      nowMenuIndex = 0;
    }
    break;

  case CONTENT:
    switch (nowMenuIndex)
    {
    case STEPS:
      displayNumber4Digits(totalStepCount, nowBrightness);
      break;
    case DIET:
      displayNumber4Digits(totalKcal, nowBrightness);
      break;
    case ANTI_NAP:
      if (toggleWanted(swEvent, encSteps, isAntiNapEnabled) != isAntiNapEnabled)
      {
        isAntiNapEnabled = !isAntiNapEnabled;
        napReset();
        if (isAntiNapEnabled)
        {
          set7SegDigit(1, 10, nowBrightness); // 'S'
          set7SegChar(2, 'O', nowBrightness); // 'E'
          set7SegChar(3, 'N', nowBrightness); // 'P'
          setSubLed(0, true, nowBrightness);
          setSubLed(1, false, nowBrightness);
        }
        else
        {
          set7SegChar(1, 'O', nowBrightness); // 'T'
          set7SegChar(2, 'F', nowBrightness); // 'E'
          set7SegChar(3, 'F', nowBrightness); // 'P'
          setSubLed(0, false, nowBrightness);
          setSubLed(1, true, nowBrightness);
        }
      }
      break;
    case LIGHT:
      // ボタン: 最大/最小の即時切替(消灯中は最大)。エンコーダ: 調光(最小からさらに左でOFF)
      if (SwitchEvent::SW_SINGLE == swEvent)
      {
        lightLevel = (lightLevel == LIGHT_MAX_LEVEL) ? 1 : LIGHT_MAX_LEVEL;
        lightApply();
        lightRender();
      }
      else if (encSteps != 0)
      {
        lightLevel = constrain(lightLevel + encSteps, 0, LIGHT_MAX_LEVEL);
        lightApply();
        lightRender();
      }
      break;
    case STOP_WATCH:
      // stop watch

      if (isStopwatchRunning)
      {
        unsigned long elapsed = nowMillis - stopWatchStartMillis; // 経過ミリ秒
        unsigned long msPart = elapsed % 1000;                    // 秒未満のミリ秒（0～999）
        if (map(msPart, 0, 999, 0, 60) != lastStopWatchMsPart)
        {
          clearAllSeconds();
          lastStopWatchMsPart = map(msPart, 0, 999, 0, 60);
          setSecondLed(map(msPart, 0, 999, 0, 60), nowBrightness);
        }
        if (lastSecond != second)
        {
          clearDisplayExceptSeconds();

          displayTime(
              elapsed / 60000,            // 分
              (elapsed % 60000) / 1000,   // 秒
              map(msPart, 0, 999, 0, 60), // 秒未満の値を0～60に変換
              nowBrightness);
          lastSecond = second;

          // コロン点滅（偶数秒で点灯）
          setColon(second % 2 == 0, nowBrightness);

          setSubLed(0, true, nowBrightness);
        }
      }

      if (SwitchEvent::SW_SINGLE == swEvent)
      {
        if (isStopwatchRunning)
        {

          isStopwatchRunning = false;
          setSubLed(0, false, nowBrightness);
          setSubLed(1, true, nowBrightness);
        }
        else
        {
          // start
          isStopwatchRunning = true;
          setSubLed(0, true, nowBrightness);
          setSubLed(1, false, nowBrightness);
          stopWatchStartMillis = millis();
        }
      }
      break;
    case MORSE:
      // モールス
      if (isDoneNowMorse)
      {
        clearLitLeds();
        if (isdigit(MORSE_TEXT[nowMorseIndex]))
        {
          // 数字
          set7SegDigit(3, MORSE_TEXT[nowMorseIndex] - '0', nowBrightness);
        }
        else
        {
          set7SegChar(3, MORSE_TEXT[nowMorseIndex], nowBrightness);
        }
        isDoneNowMorse = false;
      }
      else
      {
        analogWrite(VIBES_PIN, DEFAULT_VIBE);

        if (MORSE_TABLE[nowMorseIndex][nowMorseDotIndex] == '.')
        {
          clearAllSeconds();
          for (int i = 0; i < 15; i++)
          {
            setSecondLed(i, nowBrightness);
          }
          playVibe(true);
        }
        else if (MORSE_TABLE[nowMorseIndex][nowMorseDotIndex] == '-')
        {
          for (int i = 0; i < 30; i++)
          {
            setSecondLed(i, nowBrightness);
          }
          playVibe(false);
        }
        nowMorseDotIndex++;
        delay(150);

        if (nowMorseDotIndex >= strlen(MORSE_TABLE[nowMorseIndex]))
        {
          // 次の文字へ
          nowMorseDotIndex = 0;
          nowMorseIndex++;
          isDoneNowMorse = true;
          clearLitLeds();
          delay(1000);
        }
      }

      break;

    case TRAIN:
      if (encSteps != 0) // エンコーダーで駅を切替(ボタンでは切り替えない)
      {
        isStationBTrain = !isStationBTrain;
        if (isStationBTrain)
        {
          scrollText("STA2", nowBrightness, 130, true);
        }
        else
        {
          scrollText("STA1", nowBrightness, 130, true);
        }
        clearLitLeds();
      }

      if (nowMillis - lastChangeSecondMillis > 500)
      {
        setColon(false, nowBrightnessCoron);
      }

      if (lastSecond != second)
      {
        lastSecond = second;

        // 1. 配列のポインタと、その要素数を同時に定義・取得する
        const uint16_t *trainTable;
        size_t trainTableSize;

        if (isStationBTrain)
        {
          if (weekday == 6 || weekday == 0)
          {
            trainTable = stationBTable_weekend;
            trainTableSize = sizeof(stationBTable_weekend) / sizeof(stationBTable_weekend[0]);
          }
          else
          {
            trainTable = stationBTable_weekday;
            trainTableSize = sizeof(stationBTable_weekday) / sizeof(stationBTable_weekday[0]);
          }
        }
        else
        {
          if (weekday == 6 || weekday == 0)
          {
            trainTable = stationATable_weekend;
            trainTableSize = sizeof(stationATable_weekend) / sizeof(stationATable_weekend[0]);
          }
          else
          {
            trainTable = stationATable_weekday;
            trainTableSize = sizeof(stationATable_weekday) / sizeof(stationATable_weekday[0]);
          }
        }
        // スマホ設定で時刻表を登録していれば、そちらを使う(0:駅A平日 1:駅A休日 2:駅B平日 3:駅B休日)
        {
          size_t customSize = 0;
          int slot = (isStationBTrain ? 2 : 0) + ((weekday == 6 || weekday == 0) ? 1 : 0);
          const uint16_t *custom = orbitCfgTimetable(slot, &customSize);
          if (custom)
          {
            trainTable = custom;
            trainTableSize = customSize;
          }
        }

        // 2. 正しい要素数を渡してカウントダウンを取得
        int temp = getNextTrainCountdown(trainTable, trainTableSize);
        if (temp < 0)
        {
          // 終電後は "--:--" を表示（全消去すると毎秒ちらつくので必要な所だけ上書き）
          clearAllSeconds();
          setSubLed(0, false, 0);
          setSubLed(1, false, 0);
          for (uint8_t i = 0; i < 4; i++)
          {
            set7SegChar(i, '-', nowBrightness);
          }
          setColon(true, nowBrightnessCoron);
          lastChangeSecondMillis = nowMillis;
          break;
        }
        int h = temp / 3600;
        int m = (temp % 3600) / 60;
        int s = temp % 60;
        // displayTime()は十の位が0だと桁を書き換えないので、前の表示("-"や10分台の"1")が残らないよう消しておく
        set7SegChar(0, ' ', 0);
        set7SegChar(2, ' ', 0);
        displayTime(m, s, 60 - s, nowBrightness);

        // 3. LEDの制御ロジックの整理 (if-else で排他にする)
        if (temp <= 60)
        {
          // 1分以下の場合：SubLed(1)を点灯
          setSubLed(1, true, nowBrightness);
        }
        else if (temp >= 5 * 60)
        {
          // 5分以上の場合：SubLed(0)を点灯
          setSubLed(0, true, nowBrightness);
        }
        else
        {
          // 1分超 〜 5分未満の場合 (元コードの矛盾していた部分の想定修正)
          // 必要に応じて、ここで片方を消したり両方制御してください
          setSubLed(0, true, nowBrightness);
          setSubLed(1, false, nowBrightness);
        }
      }

      break;
    case GAME:
      // 遊びの最中の長押しは選択画面へ戻る(ここで消費して、メニューへは戻らない)
      if (swEvent == SW_LONG && gameSub != GAME_SEL)
      {
        gameSub = GAME_SEL;
        gameRenderSelect();
        swEvent = SW_NONE;
        break;
      }
      if (gameSub == GAME_SEL)
      {
        scrollTextBySecond(GAME_SUB_NAMES[gameSelIdx], nowBrightness, second);
        if (encSteps != 0)
        {
          gameSelIdx = (encSteps > 0) ? (gameSelIdx + 1) % GAME_SUB_N : (gameSelIdx + GAME_SUB_N - 1) % GAME_SUB_N;
          gameRenderSelect();
        }
        if (swEvent == SW_SINGLE)
        {
          gameSub = gameSelIdx + 1;
          if (gameSub == GAME_GRAV)
          {
            clearLitLeds();
            resetGameParticle();
            renderGameParticle();
          }
          else if (gameSub == GAME_SAFE)
          {
            safeStart();
          }
          else if (gameSub == GAME_MOLE)
          {
            moleStart();
          }
          else
          {
            hiloStart();
          }
        }
        break;
      }
      if (gameSub == GAME_SAFE)
      {
        safeUpdate(encSteps, swEvent);
        break;
      }
      if (gameSub == GAME_MOLE)
      {
        moleUpdate(encSteps, swEvent);
        break;
      }
      if (gameSub == GAME_HILO)
      {
        hiloUpdate(encSteps, swEvent);
        break;
      }
      if (nowMillis - gameLastUpdateMs >= 30)
      {
        bool hasFreshAccel = false;
        if (accelerometer.getSensorData() == BMA400_OK)
        {
          updateGameParticle(accelerometer.data.accelX, accelerometer.data.accelY, nowMillis);
          hasFreshAccel = true;
        }
        else
        {
          // センサー未接続時も粒は慣性で動き続ける
          updateGameParticle(0.0f, 0.0f, nowMillis);
        }
        if (hasFreshAccel || fabsf(gameParticleVX) > 0.02f || fabsf(gameParticleVY) > 0.02f)
          renderGameParticle();
      }
      break;
    case ROULETTE:
      if (SwitchEvent::SW_SINGLE == swEvent)
      {
        if (roulettePhase == 0)
        {
          roulettePhase = 1;
          rouletteStepsLeft = 48 + (esp_random() % 25); // 48〜72回の更新
          rouletteStride = ROULETTE_START_STRIDE;
          rouletteNextStepMs = nowMillis;
          set7SegChar(0, ' ', nowBrightness);
          set7SegChar(1, ' ', nowBrightness);
          set7SegChar(2, ' ', nowBrightness);
          set7SegChar(3, ' ', nowBrightness);
        }
      }
      if (roulettePhase == 1 && nowMillis >= rouletteNextStepMs)
      {
        uint8_t previousLed = rouletteLed;
        rouletteLed = (rouletteLed + rouletteStride) % 60;
        setSecondLed(previousLed, 0);
        setSecondLed(rouletteLed, nowBrightness);
        if (rouletteStepsLeft > 0)
          rouletteStepsLeft--;
        if (rouletteStepsLeft < ROULETTE_DECEL_STEPS)
        {
          // 徐々に飛ばす数を減らして、終盤で自然に減速する。
          if (rouletteStride > 1)
          {
            rouletteStride = (rouletteStride + 1) / 2;
          }
        }
        // 初速では「60 LED × stride / lap time」で待ち時間を算出。
        // 減速中は同じ距離あたりの時間を増やし、指定した初速設定を基準にする。
        uint32_t stepIntervalMs = ((uint32_t)ROULETTE_LAP_TIME_MS * rouletteStride + 30) / 60;
        if (rouletteStepsLeft < ROULETTE_DECEL_STEPS)
          stepIntervalMs += (uint32_t)(ROULETTE_DECEL_STEPS - rouletteStepsLeft) * 35;
        rouletteNextStepMs = nowMillis + stepIntervalMs;
        if (rouletteStepsLeft == 0)
        {
          roulettePhase = 0;
          rouletteWinner = rouletteLed;
          displayNumber4Digits((uint32_t)rouletteWinner, nowBrightness);
          pulseVibes(180);
        }
      }
      break;

    case MANNER:

      if (toggleWanted(swEvent, encSteps, isManner) != isManner)
      {
        isManner = !isManner;
        if (isManner)
        {
          set7SegDigit(1, 10, nowBrightness); // 'S'
          set7SegChar(2, 'O', nowBrightness); // 'E'
          set7SegChar(3, 'N', nowBrightness); // 'P'
          setSubLed(0, true, nowBrightness);
          setSubLed(1, false, nowBrightness);
        }
        else
        {
          set7SegChar(1, 'O', nowBrightness); // 'T'
          set7SegChar(2, 'F', nowBrightness); // 'E'
          set7SegChar(3, 'F', nowBrightness); // 'P'
          setSubLed(0, false, nowBrightness);
          setSubLed(1, true, nowBrightness);
        }
      }
      break;

    case POMODORO:
      if (SwitchEvent::SW_SINGLE == swEvent)
      {
        pomodoroToggle();
      }
      renderPomodoro(false);
      break;
    case COUNT:
      countUpdate(swEvent, nowMillis);
      break;
    case BLUETOOTH:
      slideUpdate(swEvent, encSteps, nowMillis);
      break;
    case SOS:
      sosUpdate(nowMillis);
      break;
    case UPLOAD:
      lastStatusChangeMs = nowMillis; // UPLOAD中は眠らせない(長押しでMENUへ戻れば通常に戻る)
      scrollTextBySecond("READY", nowBrightness, second);
      break;
    case SETTING:
      if (!orbitApActive && lastSecond != second)
      {
        // 無操作でWi-Fiが止まった。表示を消して、長押しで戻れることだけ残す
        lastSecond = second;
        clearLitLeds();
        set7SegChar(1, 'O', nowBrightness);
        set7SegChar(2, 'F', nowBrightness);
        set7SegChar(3, 'F', nowBrightness);
      }
      break;
    };
    if (SwitchEvent::SW_LONG == swEvent)
    {
      // 長押し処理 戻る
      if (nowMenuIndex == COUNT)
        tapDetectorStop();
      if (nowMenuIndex == SETTING && orbitApActive)
        orbitApStop();
      if (nowMenuIndex == BLUETOOTH && slideMode != SLIDE_MODE_SELECT)
      {
        // 動作中の長押しは選択画面へ戻るだけ(BLEは切らない)。もう一度長押しでMENUへ
        slideEnterSelect();
        lastStatusChangeMs = nowMillis;
        swEvent = SW_NONE;
      }
      if (nowMenuIndex == BLUETOOTH && swEvent == SW_LONG)
      {
        orbitBleStop();
        tapDetectorStop();
      }
      if (nowMenuIndex == SOS)
        sosStop();
      if (swEvent == SW_LONG)
      {
        setStatus(MENU);
        menuScrollStartMs = millis();
        // MENUへ戻った時点から無操作スリープ時間を測り直す
        lastStatusChangeMs = nowMillis;
        clearLitLeds();
        setSecondLedSegment(menuCount(), menuPosOf(nowMenuIndex), nowBrightness);
        clearDisplayExceptSeconds();
        set7SegChar(0, MenusName[nowMenuIndex][0], nowBrightness); // 'S'
        set7SegChar(1, MenusName[nowMenuIndex][1], nowBrightness); // 'T'
        set7SegChar(2, MenusName[nowMenuIndex][2], nowBrightness); // 'E'
        set7SegChar(3, MenusName[nowMenuIndex][3], nowBrightness); // 'P'
      }
    }
  };

  //* Deep Sleepへの就寝
  orbitApLoop(); // スマホ設定のWi-Fi応答(開いていなければ何もしない)
  if (orbitApActive)
  {
    lastStatusChangeMs = nowMillis; // 設定中はスリープさせない
  }
  if (orbitBleActive && (long)(nowMillis - slideLastActivityMs) < (long)SLIDE_SLEEP_MS)
  {
    lastStatusChangeMs = nowMillis; // スライド送り中は、発表の合間(質疑など)でも眠らせない
  }
  if (wristLoweredCheck(nowMillis))
  {
    Serial.println("Wrist lowered - sleep");
    lastStatusChangeMs = nowMillis - IDLE_DEEP_SLEEP_MS; // 下の判定でそのままスリープへ
  }
  if (nowStatus == IDLE && (nowMillis - lastStatusChangeMs >= IDLE_DEEP_SLEEP_MS))
  {

    lastSleepTime = hour * 3600 + minute * 60 + second;
    lastDay = day;
    enterDeepSleepIfIdle();
  }
  else if ((nowStatus == MENU && (long)(nowMillis - lastStatusChangeMs) >= (long)MENU_DEEP_SLEEP_MS) ||
           (nowStatus == CONTENT && (long)(nowMillis - lastStatusChangeMs) >= (long)CONTENT_DEEP_SLEEP_MS))
  {
    lastSleepTime = hour * 3600 + minute * 60 + second;
    lastDay = day;
    enterDeepSleepIfMenuOrContent();
  }

  delay(10);
}
