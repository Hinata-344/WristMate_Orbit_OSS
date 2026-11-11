// WristMate Orbit - スマホ設定(Wi-Fi) 本体組み込み版
// WristMate_Config/WristMate_Config.ino と同じ設定画面を、本体のメニュー「SETTING」から開く。
// 設定値はPreferences(名前空間 "orbit_cfg")に保存し、本体は起動時とメニュー描画時に読む。
// 本体側で orbitCfgApply(bool live) を定義すること。

#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <time.h>
#include <algorithm>

const char *OC_AP_SSID = "WristMate-Orbit";
constexpr char OC_AP_PASS[] = "orbit1234"; // WPA2は8〜63文字。7文字以下だとWi-Fiが出なくなる
static_assert(sizeof(OC_AP_PASS) - 1 >= 8 && sizeof(OC_AP_PASS) - 1 <= 63, "OC_AP_PASS は8〜63文字にしてください");
const unsigned long OC_AP_TIMEOUT_MS = 5UL * 60UL * 1000UL; // 無操作で自動停止 (省電力)

WebServer ocServer(80);
DNSServer ocDns;
Preferences ocPrefs;

// ---- コンテンツ (本体の MENU_LIST と同じ並びにすること) ----
const char *OC_MENU_NAMES[] = {"TRAIN", "STEPS", "DIET", "MANNER", "ANTI_NAP", "LIGHT",
                               "STOP_WATCH", "COUNT", "POMODORO", "ROULETTE", "GAME", "MORSE", "SOS", "BLUETOOTH", "UPLOAD"};
const int ORBIT_MENU_N = sizeof(OC_MENU_NAMES) / sizeof(OC_MENU_NAMES[0]);

// ---- 時刻表 ----
const int OC_TT_SLOTS = 4;
const int OC_TT_MAX = 300;                // 1枠あたりの最大本数
const uint16_t OC_TT_LIMIT_MIN = 28 * 60; // 27:59まで (深夜便用)
const char *OC_TT_DEFAULT_LABEL[OC_TT_SLOTS] = {"駅A 平日", "駅A 休日", "駅B 平日", "駅B 休日"};

// ---- 設定値 ----
struct OrbitCfg
{
  uint8_t brightness;          // LED輝度 0-255
  uint16_t pomoWork;           // ポモドーロ作業(分)
  uint16_t pomoBreak;          // ポモドーロ休憩(分)
  int8_t tzHours;              // タイムゾーン
  char name[32];               // 表示名
  bool manner;                 // マナーモード(バイブなし)
  bool antiNap;                // 居眠り防止
  bool countDown;              // カウンターをカウントダウンにする
  bool stationB;               // 電車の初期表示を駅Bにする
  bool fallDetect;             // 転倒検知(倒れて動かなければ自動でSOS)
  uint8_t slideMin;            // スライド送りの持ち時間(分) 0=表示しない
  uint8_t bootAnim;            // 起動パターン (0=くるっと 1=すいせい 2=ぐるっと満タン 3=りょうがわ 4=ぱっ 5=なし)
  uint16_t sleepIdle;    // スリープまでの無操作時間(秒): 時計表示中
  uint16_t sleepMenu;    // メニュー中
  uint16_t sleepContent; // コンテンツ中
  uint16_t sleepSlide;   // スライド送り中
  uint8_t order[ORBIT_MENU_N]; // 表示順 (MENU_LISTの番号)
  uint16_t menuMask;           // 使うメニュー
  char ttLabel[OC_TT_SLOTS][32];
  int32_t driftMsPerDay;
  uint32_t lastSync;
} orbitCfg;

// 時刻表はRAMに持つ(本体が毎秒参照するため)。本数0なら本体内蔵の時刻表を使う
uint16_t ocTT[OC_TT_SLOTS][OC_TT_MAX];
int ocTTn[OC_TT_SLOTS] = {};

int ocLoadTimetable(int slot, uint16_t *buf);

unsigned long ocLastAccess = 0;
bool orbitApActive = false;

// UTF-8の途中で切れた末尾を落とす
void ocUtf8Trim(char *s)
{
  size_t len = strlen(s);
  size_t i = len;
  while (i > 0 && ((uint8_t)s[i - 1] & 0xC0) == 0x80)
    i--; // 継続バイトを戻って、最後の文字の先頭バイトを探す
  if (i == 0)
    return;
  uint8_t c = s[i - 1];
  size_t need = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3
                              : (c >= 0xC0)   ? 2
                                              : 1;
  if (len - (i - 1) < need)
    s[i - 1] = 0; // 最後の文字が途中で切れているので落とす
}

void ocCopyText(char *dst, size_t size, const String &src)
{
  strlcpy(dst, src.c_str(), size);
  ocUtf8Trim(dst);
}

void ocDefaultOrder()
{
  for (int i = 0; i < ORBIT_MENU_N; i++)
    orbitCfg.order[i] = i;
}

// "3,1,0,..." を検証して order に入れる。MENU_N個の並べ替えでなければ false
bool ocParseOrder(const String &s, uint8_t *out)
{
  bool seen[ORBIT_MENU_N] = {};
  int n = 0;
  int pos = 0;
  while (pos <= (int)s.length())
  {
    int c = s.indexOf(',', pos);
    if (c < 0)
      c = s.length();
    String tok = s.substring(pos, c);
    tok.trim();
    if (tok.length() == 0)
      return false;
    int v = tok.toInt();
    if (v < 0 || v >= ORBIT_MENU_N || seen[v] || n >= ORBIT_MENU_N)
      return false;
    seen[v] = true;
    out[n++] = v;
    pos = c + 1;
  }
  return n == ORBIT_MENU_N;
}

void orbitCfgLoad()
{
  ocPrefs.begin("orbit_cfg", true);
  orbitCfg.brightness = ocPrefs.getUChar("bright", 128);
  orbitCfg.pomoWork = ocPrefs.getUShort("pwork", 25);
  orbitCfg.pomoBreak = ocPrefs.getUShort("pbreak", 5);
  orbitCfg.tzHours = ocPrefs.getChar("tz", 9);
  ocCopyText(orbitCfg.name, sizeof(orbitCfg.name), ocPrefs.getString("name", "Orbit"));
  orbitCfg.manner = ocPrefs.getBool("manner", false);
  orbitCfg.antiNap = ocPrefs.getBool("antinap", false);
  orbitCfg.countDown = ocPrefs.getBool("countdown", false);
  orbitCfg.stationB = ocPrefs.getBool("stationB", false);
  orbitCfg.fallDetect = ocPrefs.getBool("fall", true);
  orbitCfg.slideMin = constrain(ocPrefs.getUChar("slmin", 0), 0, 99);
  orbitCfg.bootAnim = constrain(ocPrefs.getUChar("boot", 0), 0, 5);
  orbitCfg.sleepIdle = constrain(ocPrefs.getUShort("sl_idle", 8), 3, 300);
  orbitCfg.sleepMenu = constrain(ocPrefs.getUShort("sl_menu", 16), 5, 600);
  orbitCfg.sleepContent = constrain(ocPrefs.getUShort("sl_cont", 20), 5, 900);
  orbitCfg.sleepSlide = constrain(ocPrefs.getUShort("sl_slide", 600), 30, 3600);
  ocDefaultOrder();
  uint8_t tmp[ORBIT_MENU_N];
  if (ocParseOrder(ocPrefs.getString("morder", ""), tmp))
    memcpy(orbitCfg.order, tmp, sizeof(tmp));
  orbitCfg.menuMask = ocPrefs.getUShort("men", (1u << ORBIT_MENU_N) - 1);
  // メニューが増える前(14個)に保存した設定は、並びを保ったまま新しい項目を末尾に足してオンにする
  {
    const int OC_MENU_N_OLD = 14;
    if (ORBIT_MENU_N > OC_MENU_N_OLD && ocPrefs.getUChar("mn", OC_MENU_N_OLD) < ORBIT_MENU_N)
    {
      String saved = ocPrefs.getString("morder", "");
      if (saved.length() > 0 && !ocParseOrder(saved, tmp))
      {
        for (int i = OC_MENU_N_OLD; i < ORBIT_MENU_N; i++)
          saved += "," + String(i);
        if (ocParseOrder(saved, tmp))
          memcpy(orbitCfg.order, tmp, sizeof(tmp));
      }
      for (int i = OC_MENU_N_OLD; i < ORBIT_MENU_N; i++)
        orbitCfg.menuMask |= (1u << i);
    }
  }
  for (int i = 0; i < OC_TT_SLOTS; i++)
  {
    char key[8];
    snprintf(key, sizeof(key), "tl%d", i);
    ocCopyText(orbitCfg.ttLabel[i], sizeof(orbitCfg.ttLabel[i]), ocPrefs.getString(key, OC_TT_DEFAULT_LABEL[i]));
  }
  orbitCfg.driftMsPerDay = ocPrefs.getInt("drift", 0);
  orbitCfg.lastSync = ocPrefs.getUInt("lsync", 0);
  ocPrefs.end();
  for (int i = 0; i < OC_TT_SLOTS; i++)
    ocTTn[i] = ocLoadTimetable(i, ocTT[i]);
}

void ocSaveGeneral()
{
  ocPrefs.begin("orbit_cfg", false);
  ocPrefs.putUChar("bright", orbitCfg.brightness);
  ocPrefs.putUShort("pwork", orbitCfg.pomoWork);
  ocPrefs.putUShort("pbreak", orbitCfg.pomoBreak);
  ocPrefs.putChar("tz", orbitCfg.tzHours);
  ocPrefs.putString("name", orbitCfg.name);
  ocPrefs.putBool("manner", orbitCfg.manner);
  ocPrefs.putBool("antinap", orbitCfg.antiNap);
  ocPrefs.putBool("countdown", orbitCfg.countDown);
  ocPrefs.putBool("stationB", orbitCfg.stationB);
  ocPrefs.putBool("fall", orbitCfg.fallDetect);
  ocPrefs.putUChar("boot", orbitCfg.bootAnim);
  ocPrefs.putUChar("slmin", orbitCfg.slideMin);
  ocPrefs.putUShort("sl_idle", orbitCfg.sleepIdle);
  ocPrefs.putUShort("sl_menu", orbitCfg.sleepMenu);
  ocPrefs.putUShort("sl_cont", orbitCfg.sleepContent);
  ocPrefs.putUShort("sl_slide", orbitCfg.sleepSlide);
  ocPrefs.end();
}

void ocSaveMenu()
{
  String o;
  for (int i = 0; i < ORBIT_MENU_N; i++)
  {
    if (i)
      o += ',';
    o += orbitCfg.order[i];
  }
  ocPrefs.begin("orbit_cfg", false);
  ocPrefs.putString("morder", o);
  ocPrefs.putUShort("men", orbitCfg.menuMask);
  ocPrefs.putUChar("mn", ORBIT_MENU_N);
  ocPrefs.end();
}

// 設定が変わった時に本体へ反映する処理。本体(WristMate_Orbit.ino)側で定義する。
// live=true: 設定画面からの変更(いま反映) / false: 起動時(電源投入直後だけ初期値を反映)
void orbitCfgApply(bool live);

// ---- 時刻表の読み書き ----
// 保存済みの時刻表を読む。戻り値は本数
int ocLoadTimetable(int slot, uint16_t *buf)
{
  char key[8];
  snprintf(key, sizeof(key), "tt%d", slot);
  ocPrefs.begin("orbit_cfg", true);
  size_t len = ocPrefs.getBytesLength(key);
  int n = 0;
  if (len > 0 && len % 2 == 0 && len <= OC_TT_MAX * 2)
  {
    ocPrefs.getBytes(key, buf, len);
    n = len / 2;
  }
  ocPrefs.end();
  return n;
}

void ocSaveTimetable(int slot, const uint16_t *buf, int n)
{
  char key[8];
  snprintf(key, sizeof(key), "tt%d", slot);
  ocPrefs.begin("orbit_cfg", false);
  if (n == 0)
    ocPrefs.remove(key); // 空 = 内蔵テーブルを使う
  else
    ocPrefs.putBytes(key, buf, n * 2);
  ocPrefs.end();
}

// "5:37 6:05, 07:10" のような文字列を分に変換して昇順・重複なしにする。
// 失敗したら bad に問題の文字列を入れて -1
int ocParseTimes(const String &s, uint16_t *out, String &bad)
{
  int n = 0;
  int i = 0;
  int len = s.length();
  while (i < len)
  {
    char c = s[i];
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ',' || c == ';')
    {
      i++;
      continue;
    }
    int j = i;
    while (j < len && !(s[j] == ' ' || s[j] == '\n' || s[j] == '\r' || s[j] == '\t' || s[j] == ',' || s[j] == ';'))
      j++;
    String tok = s.substring(i, j);
    i = j;
    int colon = tok.indexOf(':');
    bool ok = colon > 0 && colon <= 2 && tok.length() - colon - 1 == 2;
    int h = 0, m = 0;
    if (ok)
    {
      for (int k = 0; k < (int)tok.length(); k++)
        if (k != colon && !isdigit((unsigned char)tok[k]))
          ok = false;
      h = tok.substring(0, colon).toInt();
      m = tok.substring(colon + 1).toInt();
      if (m > 59 || h * 60 + m >= OC_TT_LIMIT_MIN)
        ok = false;
    }
    if (!ok)
    {
      bad = tok;
      return -1;
    }
    if (n >= OC_TT_MAX)
    {
      bad = "本数が多すぎます(最大" + String(OC_TT_MAX) + ")";
      return -1;
    }
    out[n++] = h * 60 + m;
  }
  std::sort(out, out + n);
  n = std::unique(out, out + n) - out;
  return n;
}

String ocTimesToString(const uint16_t *t, int n)
{
  String o;
  char b[8];
  for (int i = 0; i < n; i++)
  {
    snprintf(b, sizeof(b), "%02d:%02d", t[i] / 60, t[i] % 60);
    if (i)
      o += ' ';
    o += b;
  }
  return o;
}

// ---- RV3028 時刻の読み書き ----

// 0x00(秒)〜0x06(年)を一括で読む。失敗したら false
bool ocRtcReadTime(struct tm &t)
{
  uint8_t r[7];
  Wire.beginTransmission(RV3028_ADDR);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(RV3028_ADDR, 7) != 7)
    return false;
  for (int i = 0; i < 7; i++)
    r[i] = Wire.read();
  memset(&t, 0, sizeof(t));
  t.tm_sec = bcdToDec(r[0] & 0x7F);
  t.tm_min = bcdToDec(r[1] & 0x7F);
  t.tm_hour = bcdToDec(r[2] & 0x3F);
  t.tm_mday = bcdToDec(r[4] & 0x3F);
  t.tm_mon = bcdToDec(r[5] & 0x1F) - 1;
  t.tm_year = bcdToDec(r[6]) + 100;
  return true;
}

// 本体の rtcSetTime() と同じ並び・曜日表現 (月=1〜日=7)
bool ocRtcWriteTime(const struct tm &t)
{
  Wire.beginTransmission(RV3028_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.write(decToBcd(t.tm_sec));
  Wire.write(decToBcd(t.tm_min));
  Wire.write(decToBcd(t.tm_hour));
  Wire.write((uint8_t)(t.tm_wday == 0 ? 7 : t.tm_wday));
  Wire.write(decToBcd(t.tm_mday));
  Wire.write(decToBcd(t.tm_mon + 1));
  Wire.write(decToBcd(t.tm_year - 100));
  return Wire.endTransmission() == 0;
}

// RTCの時計の数字を「TZ未設定(UTC)の秒数」として返す。読めなければ false
bool ocRtcEpoch(long &ep)
{
  struct tm t;
  if (!ocRtcReadTime(t))
    return false;
  ep = (long)mktime(&t);
  return true;
}

// TZ未設定(UTC)前提で、時計の数字をそのまま秒数に変換して加減算する
bool ocRtcAdjust(long delta)
{
  struct tm t;
  if (!ocRtcReadTime(t))
    return false;
  time_t e = mktime(&t) + delta;
  gmtime_r(&e, &t);
  return ocRtcWriteTime(t);
}

String ocRtcTimeString()
{
  struct tm t;
  if (!ocRtcReadTime(t))
    return "";
  char b[24];
  snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1,
           t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  return b;
}

void ocSaveSyncInfo(uint32_t phoneEp)
{
  orbitCfg.lastSync = phoneEp;
  ocPrefs.begin("orbit_cfg", false);
  ocPrefs.putUInt("lsync", phoneEp);
  ocPrefs.end();
}

String ocJsonEscape(const char *s)
{
  String o;
  for (; *s; s++)
  {
    switch (*s)
    {
    case '"':
      o += "\\\"";
      break;
    case '\\':
      o += "\\\\";
      break;
    case '\n':
      o += "\\n";
      break;
    case '\r':
      break;
    case '\t':
      o += ' ';
      break;
    default:
      o += *s;
    }
  }
  return o;
}

// ---- ページ (静的HTML。値は /config.json を読んで JS が描画する) ----
const char OC_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="ja"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Orbit設定</title><style>
html,body{margin:0;height:100%;overflow:hidden;background:#111}
body{font-family:-apple-system,sans-serif;color:#eee}
#wrap{position:fixed;top:0;left:0;right:0;bottom:0;overflow-y:auto;-webkit-overflow-scrolling:touch;overscroll-behavior:contain}
#in{max-width:480px;margin:0 auto;padding:16px 16px 64px}
h1{font-size:20px}h2{font-size:17px;margin:36px 0 4px;padding-top:12px;border-top:1px solid #345}
label{display:block;margin:14px 0 4px;font-size:14px;color:#9ab}
.chk{display:flex;align-items:center;gap:10px;color:#eee;font-size:16px;margin:12px 0}
input[type=text],input[type=number],textarea,select{width:100%;box-sizing:border-box;padding:10px;font-size:16px;border-radius:8px;border:1px solid #345;background:#1b2530;color:#eee;font-family:inherit}
textarea{min-height:110px;font-variant-numeric:tabular-nums}
input[type=range]{width:100%}
input[type=checkbox]{width:22px;height:22px}
button{width:100%;margin-top:16px;padding:13px;font-size:16px;border:0;border-radius:10px;background:#2d8cff;color:#fff}
button.sub{background:#2a3544}
.msg{margin-top:8px;min-height:1.2em;color:#7fd;font-size:14px}.err{color:#f88}
.note{font-size:12px;color:#789;margin-top:6px;line-height:1.5}
.row{display:flex;align-items:center;gap:8px;padding:8px 10px;margin:6px 0;border-radius:10px;background:#1b2530}
.row.off{opacity:.45}.row .nm{flex:1;font-size:16px}.row .en{font-size:12px;color:#789}
.row button{width:44px;margin:0;padding:8px 0;font-size:16px}
.row input[type=checkbox]{margin:0 4px}
.big{font-size:20px;font-variant-numeric:tabular-nums}
#kbclose{display:none;position:fixed;top:8px;right:8px;width:auto;margin:0;padding:8px 14px;font-size:14px;border-radius:20px;background:#444;z-index:9}
.tt{border:1px solid #345;border-radius:12px;padding:4px 12px 12px;margin:14px 0}
</style></head><body><div id=wrap><div id=in><h1>WristMate Orbit 設定</h1><button id=kbclose type=button>キーボードを閉じる ✕</button><div id=app>読み込み中…</div></div></div>
<script>
const $=id=>document.getElementById(id);
const JA={TRAIN:'電車',STEPS:'歩数',DIET:'ダイエット',MANNER:'マナーモード',ANTI_NAP:'居眠り防止',LIGHT:'ライト',
STOP_WATCH:'ストップウォッチ',COUNT:'カウンター',POMODORO:'ポモドーロ',ROULETTE:'ルーレット',GAME:'ゲーム',MORSE:'モールス',BLUETOOTH:'Bluetooth(スライド・ショート・シャッター)',SOS:'SOS(ライトでモールス)',UPLOAD:'アップロード(この中はスリープしない)'};
let C=null,order=[],mask=0,rtcT=null;
function el(t,p,...k){const e=document.createElement(t);Object.assign(e,p||{});k.forEach(c=>e.append(c));return e}
function msg(id,t,bad){const m=$(id);m.textContent=t;m.className='msg'+(bad?' err':'')}
async function post(u,d,id){
 try{const r=await fetch(u,{method:'POST',body:new URLSearchParams(d)});
  const t=await r.text();msg(id,r.ok?'保存しました':'エラー: '+t,!r.ok);return r.ok}
 catch(e){msg(id,'通信エラー',true);return false}}
function phoneEp(){return Math.floor(Date.now()/1000)-new Date().getTimezoneOffset()*60}
function fmtSigned(s){return (s>0?'+':'')+s+'秒'}

function build(){
 const a=$('app');a.innerHTML='';
 // 基本
 a.innerHTML=`<h2 style="border:0;margin-top:20px">基本</h2>
 <label>表示ラベル</label><input type=text id=devname maxlength=10>
 <label>LED輝度: <span id=bv></span></label><input type=range id=bright min=0 max=255 oninput="bv.textContent=this.value">
 <label>ポモドーロ 作業(分)</label><input type=number id=pwork min=1 max=180>
 <label>ポモドーロ 休憩(分)</label><input type=number id=pbreak min=1 max=60>
 <label>タイムゾーン (UTCからの時間差)</label><input type=number id=tz min=-12 max=14>
 <h2>ON / OFF</h2>
 <label class=chk><input type=checkbox id=manner> マナーモード(バイブなし)</label>
 <label class=chk><input type=checkbox id=antinap> 居眠り防止</label>
 <label class=chk><input type=checkbox id=countdown> カウンターをカウントダウンにする</label>
 <label class=chk><input type=checkbox id=stationB> 電車は駅Bを先に表示</label>
 <label class=chk><input type=checkbox id=fall> 転倒検知(衝撃のあとカウントダウン→自動でSOS。マナー中は無効)</label>
 <h2>起動パターン</h2>
 <label>電源が入った時の光り方</label><select id=bootanim>
 <option value=0>くるっと(1つの光が回る)</option>
 <option value=1>すいせい(尾を引いて回る)</option>
 <option value=2>ぐるっと満タン(埋まって消える)</option>
 <option value=3>りょうがわ(左右から回る)</option>
 <option value=4>ぱっ(全体がふわっと光る)</option>
 <option value=5>なし(すぐ時計)</option></select>
 <h2>スライド送り</h2>
 <label>発表の持ち時間 (0〜99分。0=表示しない)</label><input type=number id=slmin min=0 max=99>
 <div class=note>SLIDEの画面に「あと何分何秒か」を表示します。最初にスライドを送った瞬間から計り始めます。残り5分・1分・時間切れで振動します(マナー中は振動なし)。</div>
 <h2>スリープまでの時間(無操作の秒数)</h2>
 <label>時計表示中 (3〜300秒)</label><input type=number id=sidle min=3 max=300>
 <label>メニュー中 (5〜600秒)</label><input type=number id=smenu min=5 max=600>
 <label>コンテンツ中 (5〜900秒)</label><input type=number id=scont min=5 max=900>
 <label>スライド送り中 (30〜3600秒)</label><input type=number id=sslide min=30 max=3600>
 <button id=sg>基本とON/OFFを保存</button><div class=msg id=m1></div>
 <h2>コンテンツ</h2><div class=note>▲▼で並び替え、チェックを外すとメニューに出ません(最低1つは必要)。</div>
 <div id=menu></div><button id=sm>コンテンツを保存</button><div class=msg id=m2></div>
 <h2>電車の時刻表</h2><div class=note>「5:37 6:05 7:10」のように、スペースか改行で区切って入力。順番は自動で整えます。空欄で保存すると本体の内蔵時刻表に戻ります。27:59まで入力できます。</div>
 <div id=tts></div>
 <h2>時間キャリブレーション</h2>
 <label>本体の時刻 (RV3028)</label><div class="big" id=rt>--</div>
 <label>スマホとのずれ</label><div class="big" id=diff>--</div>
 <div class=note id=driftnote></div>
 <button id=rfix>ずれを補正する(ドリフトも記録)</button>
 <button class=sub id=rsync>スマホの時刻に合わせる</button>
 <label>秒数で補正 (±秒。例: -3 で3秒戻す)</label>
 <input type=number id=rsec value=1 step=1 min=-86400 max=86400>
 <button class=sub id=radj>補正する</button><div class=msg id=m3></div>`;
 $('devname').value=C.name;$('bright').value=C.bright;$('bv').textContent=C.bright;
 $('bootanim').value=C.boot;$('slmin').value=C.slmin;$('sidle').value=C.sidle;$('smenu').value=C.smenu;$('scont').value=C.scont;$('sslide').value=C.sslide;
 $('pwork').value=C.pwork;$('pbreak').value=C.pbreak;$('tz').value=C.tz;
 ['manner','antinap','countdown','stationB','fall'].forEach(k=>$(k).checked=!!C[k]);
 $('bootanim').onchange=()=>{fetch('/bootprev',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'n='+$('bootanim').value}).catch(()=>{})};
 $('sg').onclick=()=>{const d={name:$('devname').value,bright:$('bright').value,pwork:$('pwork').value,
  pbreak:$('pbreak').value,tz:$('tz').value,sidle:$('sidle').value,smenu:$('smenu').value,
  scont:$('scont').value,sslide:$('sslide').value,boot:$('bootanim').value,slmin:$('slmin').value};
  ['manner','antinap','countdown','stationB','fall'].forEach(k=>d[k]=$(k).checked?'1':'0');
  post('/save',d,'m1')};
 order=C.order.slice();mask=C.mask;drawMenu();
 $('sm').onclick=async()=>{
  if(!(mask&((1<<order.length)-1))){msg('m2','最低1つはオンにしてください',true);return}
  post('/menu',{order:order.join(','),mask:mask},'m2')};
 C.tt.forEach((t,i)=>{
  const box=el('div',{className:'tt'});
  const lb=el('input',{type:'text',maxLength:10,value:t.label});
  const ta=el('textarea',{value:t.times,placeholder:'空欄 = 内蔵の時刻表'});
  const cnt=el('div',{className:'note'});
  const upd=()=>{const n=ta.value.split(/[\s,;]+/).filter(x=>x).length;cnt.textContent=n?n+'本':'内蔵の時刻表を使います'};
  ta.oninput=upd;upd();
  const m=el('div',{className:'msg',id:'mt'+i});
  const b=el('button',{textContent:'この時刻表を保存'});
  b.onclick=async()=>{
   const r=await fetch('/train',{method:'POST',body:new URLSearchParams({slot:i,label:lb.value,times:ta.value})});
   const j=await r.text();
   if(r.ok){ta.value=j;upd();msg('mt'+i,'保存しました')}else msg('mt'+i,'エラー: '+j,true)};
  box.append(el('label',{textContent:'枠'+(i+1)+' のラベル'}),lb,el('label',{textContent:'発車時刻'}),ta,cnt,b,m);
  $('tts').append(box)});
 $('rsync').onclick=()=>{const n=new Date();
  post('/rtc/sync',{y:n.getFullYear(),mo:n.getMonth()+1,d:n.getDate(),h:n.getHours(),mi:n.getMinutes(),
   s:n.getSeconds(),ep:phoneEp()},'m3').then(rget)};
 $('radj').onclick=()=>{const v=parseInt($('rsec').value);if(!v){msg('m3','0以外を入力',true);return}
  post('/rtc/adjust',{sec:v},'m3').then(rget)};
 $('rfix').onclick=async()=>{
  if(!rtcT){msg('m3','RTCの時刻が読めません',true);return}
  const now=phoneEp(),diff=rtcT-now;
  const d={sec:-diff,ep:now};
  const days=C.lsync?(now-C.lsync)/86400:0;
  if(days>=0.25){d.drift=Math.round(diff*1000/days)}
  const ok=await post('/rtc/adjust',d,'m3');
  if(ok){C.lsync=now;if('drift' in d)C.drift=d.drift;note();rget()}};
 note();rget();
}
function drawMenu(){
 const box=$('menu');box.innerHTML='';
 order.forEach((m,pos)=>{
  const on=!!(mask&(1<<m));
  const row=el('div',{className:'row'+(on?'':' off')});
  const up=el('button',{className:'sub',textContent:'▲',disabled:pos==0});
  const dn=el('button',{className:'sub',textContent:'▼',disabled:pos==order.length-1});
  const cb=el('input',{type:'checkbox',checked:on});
  up.onclick=()=>{[order[pos-1],order[pos]]=[order[pos],order[pos-1]];drawMenu()};
  dn.onclick=()=>{[order[pos+1],order[pos]]=[order[pos],order[pos+1]];drawMenu()};
  cb.onchange=()=>{mask=cb.checked?(mask|(1<<m)):(mask&~(1<<m));drawMenu()};
  row.append(el('span',{className:'nm',textContent:(pos+1)+'. '+(JA[C.names[m]]||C.names[m])}),
   el('span',{className:'en',textContent:C.names[m]}),cb,up,dn);
  box.append(row)})}
function note(){
 const n=$('driftnote');if(!n)return;
 let t='ずれを補正すると、前回合わせた時からの進み/遅れを「ドリフト」として記録します(6時間以上あいた時)。';
 if(C.lsync){const d=(phoneEp()-C.lsync)/86400;t+=' 前回合わせてから '+d.toFixed(1)+' 日。'}
 if(C.drift)t+=' 記録済みドリフト: '+(C.drift/1000).toFixed(2)+' 秒/日。';
 n.textContent=t}
async function rget(){try{
 const j=await (await fetch('/rtc')).json();
 if(!j.time){$('rt').textContent='RTC応答なし';$('diff').textContent='--';rtcT=null;return}
 rtcT=j.ep;if($('rt').textContent!=j.time)$('rt').textContent=j.time;
 const d=j.ep-phoneEp();$('diff').textContent=fmtSigned(d)+(d>0?'(本体が進み)':d<0?'(本体が遅れ)':'');
}catch(e){}}
const TXT='input:not([type=checkbox]):not([type=range]),textarea';
function noAuto(){document.querySelectorAll('input,textarea').forEach(e=>{
 e.autocomplete='off';e.setAttribute('autocorrect','off');e.autocapitalize='off';e.spellcheck=false;
 if(e.tagName=='INPUT'&&e.type!='checkbox'&&e.type!='range')e.enterKeyHint='done';
 if(e.id=='devname'||e.type=='text')e.name='orbit_'+(e.id||'f'+Math.random().toString(36).slice(2,6));})}
const kb=$('kbclose');
// iOSはblur()だけだとフォーカスが戻ってキーボードが再表示されることがあるので、一瞬readOnlyにして確実に外す
let lastTxt=null;
function closeKb(){const a=document.activeElement&&document.activeElement.matches&&document.activeElement.matches(TXT)?document.activeElement:lastTxt;
 const W=$('wrap'),y=W.scrollTop;
 if(a){a.readOnly=true;a.blur()}
 // キーボードが引っ込む時にiOSが勝手にスクロールを動かすので、位置を戻す
 [0,80,250,600].forEach(t=>setTimeout(()=>{if(Math.abs(W.scrollTop-y)>2)W.scrollTop=y},t));
 // キャプティブポータル(iOSの簡易ブラウザ)ではblurだけで閉じないので、フォーカスを非入力要素へ移す
 if(document.activeElement&&document.activeElement.blur)document.activeElement.blur();
 if(window.getSelection)window.getSelection().removeAllRanges();kb.style.display='none'}
// 普段は全入力欄をreadOnlyにしておき、ユーザーがタップした欄だけ編集可にする(勝手なフォーカス→キーボード表示を防ぐ)
function lockAll(){document.querySelectorAll(TXT).forEach(e=>{if(document.activeElement!==e)e.readOnly=true})}
document.addEventListener('click',e=>{const t=e.target;if(t.matches&&t.matches(TXT)&&t.readOnly){t.readOnly=false;t.focus()}},true);
document.addEventListener('focusout',e=>{const t=e.target;if(t.matches&&t.matches(TXT))setTimeout(()=>{if(document.activeElement!==t)t.readOnly=true},50)});
document.addEventListener('focusin',e=>{if(e.target.matches(TXT)){lastTxt=e.target;kb.style.display='block'}});
document.addEventListener('focusout',()=>setTimeout(()=>{const a=document.activeElement;
 if(!(a&&a.matches&&a.matches(TXT)))kb.style.display='none'},80));
kb.addEventListener('touchstart',e=>{e.preventDefault();closeKb()},{passive:false});
kb.onmousedown=e=>{e.preventDefault();closeKb()};
kb.addEventListener('click',e=>{e.preventDefault();closeKb()});
document.addEventListener('touchstart',e=>{const a=document.activeElement;
 if(a&&a.matches&&a.matches(TXT)&&!e.target.closest('input,textarea,button,label'))closeKb()},{passive:true});
document.addEventListener('keydown',e=>{if(e.key=='Enter'&&e.target.tagName=='INPUT')closeKb()});
// 簡易ブラウザ(キャプティブポータル)でページが再読み込み/再構築されても、スクロール位置を戻す
let lastScr=0;
// 簡易ブラウザはフォーカスが変わる(キーボードを閉じる・ボタンを押す)と勝手に一番上へ飛ばすので、
// 指が動いていないのに一気に先頭へ飛んだら元の位置へ戻す(指でのスクロールは少しずつ動くので区別できる)
let goodY=0,lastMove=0;
// ボタンにフォーカスが移ること自体を止めて、飛ぶきっかけを減らす(clickは普通に発生する)
document.addEventListener('mousedown',e=>{if(e.target.closest('button'))e.preventDefault()});
$('wrap').addEventListener('touchmove',()=>{lastMove=Date.now()},{passive:true});
$('wrap').addEventListener('scroll',()=>{const W=$('wrap'),y=W.scrollTop;lastScr=Date.now();
 if(y<5&&goodY>100&&Date.now()-lastMove>300&&goodY-y>W.clientHeight*0.5){W.scrollTop=goodY;return}
 goodY=y;try{sessionStorage.setItem('oy',String(y))}catch(e){}},{passive:true});
function restoreY(){try{const y=+sessionStorage.getItem('oy')||0;if(y>0)[0,100,400].forEach(t=>setTimeout(()=>{$('wrap').scrollTop=y},t))}catch(e){}}
(async()=>{try{C=await (await fetch('/config.json')).json();build();noAuto();lockAll();restoreY();setInterval(()=>{if(Date.now()-lastScr>1500)rget()},1000)}
catch(e){$('app').textContent='読み込みに失敗しました。再読み込みしてください'}})();
</script></body></html>)HTML";

void ocHandleRoot()
{
  ocLastAccess = millis();
  ocServer.send_P(200, "text/html; charset=utf-8", OC_PAGE);
}

// 現在値をJSONで返す (ページの描画と、他アプリから使う用)
void ocHandleJson()
{
  ocLastAccess = millis();
  String j = "{\"name\":\"" + ocJsonEscape(orbitCfg.name) + "\",\"bright\":" + orbitCfg.brightness +
             ",\"pwork\":" + orbitCfg.pomoWork + ",\"pbreak\":" + orbitCfg.pomoBreak + ",\"tz\":" + orbitCfg.tzHours +
             ",\"manner\":" + (orbitCfg.manner ? "true" : "false") +
             ",\"antinap\":" + (orbitCfg.antiNap ? "true" : "false") +
             ",\"countdown\":" + (orbitCfg.countDown ? "true" : "false") +
             ",\"stationB\":" + (orbitCfg.stationB ? "true" : "false") +
             ",\"fall\":" + (orbitCfg.fallDetect ? "true" : "false") +
             ",\"boot\":" + orbitCfg.bootAnim + ",\"slmin\":" + orbitCfg.slideMin +
             ",\"sidle\":" + orbitCfg.sleepIdle + ",\"smenu\":" + orbitCfg.sleepMenu +
             ",\"scont\":" + orbitCfg.sleepContent + ",\"sslide\":" + orbitCfg.sleepSlide +
             ",\"mask\":" + orbitCfg.menuMask + ",\"drift\":" + orbitCfg.driftMsPerDay +
             ",\"lsync\":" + orbitCfg.lastSync + ",\"names\":[";
  for (int i = 0; i < ORBIT_MENU_N; i++)
    j += String(i ? "," : "") + "\"" + OC_MENU_NAMES[i] + "\"";
  j += "],\"order\":[";
  for (int i = 0; i < ORBIT_MENU_N; i++)
    j += String(i ? "," : "") + String(orbitCfg.order[i]);
  j += "],\"tt\":[";
  for (int i = 0; i < OC_TT_SLOTS; i++)
  {
    j += String(i ? "," : "") + "{\"label\":\"" + ocJsonEscape(orbitCfg.ttLabel[i]) + "\",\"times\":\"" +
         ocTimesToString(ocTT[i], ocTTn[i]) + "\"}";
  }
  j += "]}";
  ocServer.send(200, "application/json", j);
}

void ocHandleSave()
{
  ocLastAccess = millis();
  if (ocServer.hasArg("bright"))
    orbitCfg.brightness = constrain(ocServer.arg("bright").toInt(), 0, 255);
  if (ocServer.hasArg("pwork"))
    orbitCfg.pomoWork = constrain(ocServer.arg("pwork").toInt(), 1, 180);
  if (ocServer.hasArg("pbreak"))
    orbitCfg.pomoBreak = constrain(ocServer.arg("pbreak").toInt(), 1, 60);
  if (ocServer.hasArg("tz"))
    orbitCfg.tzHours = constrain(ocServer.arg("tz").toInt(), -12, 14);
  if (ocServer.hasArg("name"))
    ocCopyText(orbitCfg.name, sizeof(orbitCfg.name), ocServer.arg("name"));
  if (ocServer.hasArg("manner"))
    orbitCfg.manner = ocServer.arg("manner") == "1";
  if (ocServer.hasArg("antinap"))
    orbitCfg.antiNap = ocServer.arg("antinap") == "1";
  if (ocServer.hasArg("countdown"))
    orbitCfg.countDown = ocServer.arg("countdown") == "1";
  if (ocServer.hasArg("stationB"))
    orbitCfg.stationB = ocServer.arg("stationB") == "1";
  if (ocServer.hasArg("fall"))
    orbitCfg.fallDetect = ocServer.arg("fall") == "1";
  if (ocServer.hasArg("slmin"))
    orbitCfg.slideMin = constrain(ocServer.arg("slmin").toInt(), 0, 99);
  if (ocServer.hasArg("boot"))
    orbitCfg.bootAnim = constrain(ocServer.arg("boot").toInt(), 0, 5);
  if (ocServer.hasArg("sidle"))
    orbitCfg.sleepIdle = constrain(ocServer.arg("sidle").toInt(), 3, 300);
  if (ocServer.hasArg("smenu"))
    orbitCfg.sleepMenu = constrain(ocServer.arg("smenu").toInt(), 5, 600);
  if (ocServer.hasArg("scont"))
    orbitCfg.sleepContent = constrain(ocServer.arg("scont").toInt(), 5, 900);
  if (ocServer.hasArg("sslide"))
    orbitCfg.sleepSlide = constrain(ocServer.arg("sslide").toInt(), 30, 3600);
  ocSaveGeneral();
  orbitCfgApply(true);
  ocServer.send(200, "text/plain", "ok");
}

// 起動パターンのプレビュー: 本体で選んだ演出を1回光らせる
void ocHandleBootPreview()
{
  ocLastAccess = millis();
  orbitBootPreview((uint8_t)constrain(ocServer.arg("n").toInt(), 0, 5));
  ocServer.send(200, "text/plain", "ok");
}

void ocHandleMenu()
{
  ocLastAccess = millis();
  uint8_t tmp[ORBIT_MENU_N];
  long mask = ocServer.arg("mask").toInt();
  if (!ocParseOrder(ocServer.arg("order"), tmp))
  {
    ocServer.send(400, "text/plain", "並び順が不正です");
    return;
  }
  mask &= (1L << ORBIT_MENU_N) - 1;
  if (mask == 0)
  {
    ocServer.send(400, "text/plain", "最低1つはオンにしてください");
    return;
  }
  memcpy(orbitCfg.order, tmp, sizeof(tmp));
  orbitCfg.menuMask = mask;
  ocSaveMenu();
  orbitCfgApply(true);
  ocServer.send(200, "text/plain", "ok");
}

// 時刻表1枠の保存。整えた結果の文字列を返す
void ocHandleTrain()
{
  ocLastAccess = millis();
  int slot = ocServer.arg("slot").toInt();
  if (slot < 0 || slot >= OC_TT_SLOTS)
  {
    ocServer.send(400, "text/plain", "slot");
    return;
  }
  uint16_t buf[OC_TT_MAX];
  String bad;
  int n = ocParseTimes(ocServer.arg("times"), buf, bad);
  if (n < 0)
  {
    ocServer.send(400, "text/plain", "読めない時刻: " + bad);
    return;
  }
  ocSaveTimetable(slot, buf, n);
  memcpy(ocTT[slot], buf, n * sizeof(uint16_t));
  ocTTn[slot] = n;
  ocCopyText(orbitCfg.ttLabel[slot], sizeof(orbitCfg.ttLabel[slot]), ocServer.arg("label"));
  char key[8];
  snprintf(key, sizeof(key), "tl%d", slot);
  ocPrefs.begin("orbit_cfg", false);
  ocPrefs.putString(key, orbitCfg.ttLabel[slot]);
  ocPrefs.end();
  Serial.printf("timetable %d saved: %d entries\n", slot, n);
  ocServer.send(200, "text/plain", ocTimesToString(buf, n));
}

void ocHandleRtcGet()
{
  ocLastAccess = millis();
  long ep;
  if (!ocRtcEpoch(ep))
  {
    ocServer.send(200, "application/json", "{\"time\":\"\"}");
    return;
  }
  ocServer.send(200, "application/json", "{\"time\":\"" + ocRtcTimeString() + "\",\"ep\":" + ep + "}");
}

// スマホの現在時刻(ローカル)をそのままRTCへ書く
void ocHandleRtcSync()
{
  ocLastAccess = millis();
  struct tm t = {};
  t.tm_year = ocServer.arg("y").toInt() - 1900;
  t.tm_mon = ocServer.arg("mo").toInt() - 1;
  t.tm_mday = ocServer.arg("d").toInt();
  t.tm_hour = ocServer.arg("h").toInt();
  t.tm_min = ocServer.arg("mi").toInt();
  t.tm_sec = ocServer.arg("s").toInt();
  if (t.tm_year < 120 || t.tm_year > 199 || t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31)
  {
    ocServer.send(400, "text/plain", "bad time");
    return;
  }
  time_t e = mktime(&t); // 曜日を計算し直す (TZ未設定=UTCなので数字は変わらない)
  gmtime_r(&e, &t);
  if (!ocRtcWriteTime(t))
  {
    ocServer.send(500, "text/plain", "RTC write failed");
    return;
  }
  if (ocServer.hasArg("ep"))
    ocSaveSyncInfo((uint32_t)ocServer.arg("ep").toInt());
  Serial.printf("RTC sync: %s\n", ocRtcTimeString().c_str());
  ocServer.send(200, "text/plain", "ok");
}

// ±秒数でRTCを進める/戻す。ep があれば「いま合わせた」記録、drift があればドリフトも記録
void ocHandleRtcAdjust()
{
  ocLastAccess = millis();
  long sec = ocServer.arg("sec").toInt();
  if (sec < -86400 || sec > 86400)
  {
    ocServer.send(400, "text/plain", "range ±86400");
    return;
  }
  if (!ocRtcAdjust(sec))
  {
    ocServer.send(500, "text/plain", "RTC access failed");
    return;
  }
  if (ocServer.hasArg("ep"))
    ocSaveSyncInfo((uint32_t)ocServer.arg("ep").toInt());
  if (ocServer.hasArg("drift"))
  {
    orbitCfg.driftMsPerDay = constrain(ocServer.arg("drift").toInt(), -600000L, 600000L);
    ocPrefs.begin("orbit_cfg", false);
    ocPrefs.putInt("drift", orbitCfg.driftMsPerDay);
    ocPrefs.end();
  }
  Serial.printf("RTC adjust %+ld s -> %s (drift %ld ms/day)\n", sec, ocRtcTimeString().c_str(), (long)orbitCfg.driftMsPerDay);
  ocServer.send(200, "text/plain", "ok");
}

// キャプティブポータル: OSの接続確認URLもルートへ飛ばす
void ocHandleNotFound()
{
  ocServer.sendHeader("Location", "http://192.168.4.1/", true);
  ocServer.send(302, "text/plain", "");
}

void orbitApStart()
{
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(OC_AP_SSID, OC_AP_PASS, 6); // チャンネル6、SSIDは公開
  Serial.printf("softAP: %s\n", ok ? "OK" : "FAILED");
  ocDns.start(53, "*", WiFi.softAPIP());
  ocServer.on("/", ocHandleRoot);
  ocServer.on("/save", HTTP_POST, ocHandleSave);
  ocServer.on("/menu", HTTP_POST, ocHandleMenu);
  ocServer.on("/bootprev", HTTP_POST, ocHandleBootPreview);
  ocServer.on("/train", HTTP_POST, ocHandleTrain);
  ocServer.on("/config.json", ocHandleJson);
  ocServer.on("/rtc", ocHandleRtcGet);
  ocServer.on("/rtc/sync", HTTP_POST, ocHandleRtcSync);
  ocServer.on("/rtc/adjust", HTTP_POST, ocHandleRtcAdjust);
  ocServer.onNotFound(ocHandleNotFound);
  ocServer.begin();
  ocLastAccess = millis();
  orbitApActive = true;
  Serial.printf("AP start: %s  http://%s\n", OC_AP_SSID, WiFi.softAPIP().toString().c_str());
}

void orbitApStop()
{
  ocServer.stop();
  ocDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  orbitApActive = false;
  Serial.println("AP stop");
}

// 本体のloop()から毎回呼ぶ。設定画面の応答と、無操作での自動停止
void orbitApLoop()
{
  if (!orbitApActive)
    return;
  ocDns.processNextRequest();
  ocServer.handleClient();
  if (millis() - ocLastAccess > OC_AP_TIMEOUT_MS && WiFi.softAPgetStationNum() == 0)
    orbitApStop();
}

// 時刻表を返す。設定画面で登録済みなら配列と本数、なければ nullptr(内蔵テーブルを使う)
const uint16_t *orbitCfgTimetable(int slot, size_t *n)
{
  if (slot < 0 || slot >= OC_TT_SLOTS || ocTTn[slot] <= 0)
    return nullptr;
  *n = ocTTn[slot];
  return ocTT[slot];
}

// 記録したドリフト(ミリ秒/日)を、日が変わるたびにRTCへ反映する。1秒に満たない分は次回に持ち越す
RTC_DATA_ATTR long ocDriftRemainderMs = 0;
void orbitApplyDailyDrift()
{
  if (orbitCfg.driftMsPerDay == 0)
    return;
  ocDriftRemainderMs += orbitCfg.driftMsPerDay;
  long sec = ocDriftRemainderMs / 1000;
  if (sec == 0)
    return;
  if (ocRtcAdjust(-sec))
  {
    ocDriftRemainderMs -= sec * 1000;
    Serial.printf("drift correction: %+ld s\n", -sec);
  }
}
