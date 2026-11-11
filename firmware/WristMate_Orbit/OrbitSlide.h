// WristMate Orbit - BLEスライド送り(キーボードとして接続) 本体組み込み版
// 公開ライブラリ HijelHID_BLEKeyboard (NimBLE-Arduino 2.x) を使う。
// WristMate_SlideHijel/WristMate_SlideHijel.ino で実機動作を確認した構成。
//
// 必要なライブラリ: NimBLE-Arduino 2.3.8以上 / HijelHID_BLEKeyboard 0.6.0
// PC/Mac/スマホのBluetooth設定で「WristMate」をペアリングすると、
// 右矢印(次のスライド)・左矢印(前のスライド)を送る。Keynote/PowerPoint/Googleスライドで使える。
// ペアリング情報はNVSに残る(再起動・ディープスリープ後も再接続できる)。
//
// 旧版(コア同梱BLEを使う自作 OrbitBleKeyboard)は _old_core_ble/ に退避してある。
// 旧版はペアリングは通るがキーがPCに届かない症状が出ていた。

#pragma once
// マウス(ボタン/X/Y/ホイール)のレポートを足した改造版を、スケッチ内 src/HijelHID_BLEKeyboard/ に同梱している。
// (インストール済みの公開ライブラリは使わない。本体への改変点は "WristMate追加" のコメントで探せる)
#include "src/HijelHID_BLEKeyboard/HijelHID_BLEKeyboard.h"

// 名前は15文字以内・空白なし(Apple機器で見えなくなる報告があるため)
HijelHID_BLEKeyboard obKeyboard("WristMate", "WristMate", 100);
bool obInitialized = false;
bool orbitBleActive = false; // SLIDEを開いている間 true(広告中または接続中)

// SLIDEに入った時に呼ぶ。広告を始めて、PCからペアリング/接続できるようにする
void orbitBleStart()
{
  orbitBleActive = true;
  if (!obInitialized)
  {
    obKeyboard.setLogLevel(HIDLogLevel::Normal); // 接続・ペアリングの様子をシリアルに出す
    obInitialized = true;
  }
  obKeyboard.begin(); // 初回はBLE初期化、2回目以降は広告の再開(動作中なら何もしない)
  Serial.println("[BLE] advertising as 'WristMate'");
}

// SLIDEを出る時・眠る前に呼ぶ。広告を止めて、接続中なら切る
void orbitBleStop()
{
  if (!obInitialized)
    return;
  orbitBleActive = false;
  obKeyboard.end();
  Serial.println("[BLE] stopped");
}

bool orbitBleConnected() { return obInitialized && obKeyboard.isConnected(); }
// 認証まで終わって送れる状態(isConnectedは認証前にもtrueになる)
bool orbitBleReady() { return obInitialized && obKeyboard.isPaired(); }

// キーを1回押して離す。送れたら true
bool orbitBleKey(uint8_t code)
{
  if (!orbitBleReady())
  {
    Serial.println("[BLE] キーを送れません (未接続)");
    return false;
  }
  Serial.printf("[BLE] キー送信 0x%02X\n", code);
  obKeyboard.tap(code);
  return true;
}

bool orbitSlideNext() { return orbitBleKey(KEY_RIGHT); }
bool orbitSlidePrev() { return orbitBleKey(KEY_LEFT); }

// ショート動画(iPhone等のスマホ)のスクロール。矢印キーはスマホアプリに効かないので、
// BLEマウスで「ボタンを押したまま上下にドラッグ」=スワイプを再現する。
// iPhoneは 設定 > アクセシビリティ > タッチ > AssistiveTouch をONにすると、マウスが使える。
// 次の動画=上へスワイプ(ポインタを上へ)、前の動画=下へスワイプ。
const int8_t SWIPE_STEP = 6;           // 1レポートあたりの移動量(小さいほどポインタの加速が効かず、動きが小さくなる)
const uint8_t SWIPE_STEPS = 20;        // レポート数(合計の移動量 = SWIPE_STEP x SWIPE_STEPS)
const uint16_t SWIPE_INTERVAL_MS = 8;  // レポート間隔
bool orbitShortSwipe(bool next)
{
  if (!orbitBleReady())
  {
    Serial.println("[BLE] スワイプを送れません (未接続)");
    return false;
  }
  int8_t dy = next ? -SWIPE_STEP : SWIPE_STEP;
  Serial.printf("[BLE] スワイプ %s\n", next ? "up(次)" : "down(前)");
  obKeyboard.mouseReport(0x01, 0, 0);   // 左ボタンを押す
  delay(SWIPE_INTERVAL_MS);
  for (uint8_t i = 0; i < SWIPE_STEPS; i++)
  {
    obKeyboard.mouseReport(0x01, 0, dy);
    delay(SWIPE_INTERVAL_MS);
  }
  obKeyboard.mouseReport(0x00, 0, 0);   // 離す
  // ポインタを元の位置へ戻す(ボタンなしで逆向きに同じだけ動かす)。同じ速さで動かすので加速も対称になる
  delay(SWIPE_INTERVAL_MS * 4);
  for (uint8_t i = 0; i < SWIPE_STEPS; i++)
  {
    obKeyboard.mouseReport(0x00, 0, -dy);
    delay(SWIPE_INTERVAL_MS);
  }
  return true;
}
bool orbitShortNext() { return orbitShortSwipe(true); }
bool orbitShortPrev() { return orbitShortSwipe(false); }

// スマホのカメラのシャッター。iPhone/Androidのカメラは音量ボタンでシャッターが切れるので、
// BLEキーボードの「音量下げ」(コンシューマ制御)を1回送る。
bool orbitShutter()
{
  if (!orbitBleReady())
  {
    Serial.println("[BLE] シャッターを送れません (未接続)");
    return false;
  }
  Serial.println("[BLE] シャッター (音量下げ)");
  obKeyboard.tap(MEDIA_VOLUME_DOWN);
  return true;
}
