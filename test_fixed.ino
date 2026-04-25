// ============================================================
//  PASS2ROOM — Smart Room Booking System
//  ESP32 Dev Module
//  Hardware: OLED SSD1306, LED Red(2) Green(4), Buzzer(15)
// ============================================================

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>

// ============================================================
//  WiFi CONFIG
// ============================================================
const char* ssid     = "IOT";
const char* password = "mfuiot2023";

// ============================================================
//  HARDWARE
// ============================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define LED_RED    2
#define LED_GREEN  4
#define BUZZER_PIN 15

// ============================================================
//  WEB SERVER
// ============================================================
WebServer server(80);

// ============================================================
//  BOOKING DATA
// ============================================================
#define MAX_BOOKINGS 3

struct Booking {
  String studentId;
  String name;
  String date;
  int    startHour;   // 8–21
  int    durationHrs; // 1–3
  String token;
  bool   isWinner;
  bool   used;
};

Booking bookings[MAX_BOOKINGS];
int     bookingCount   = 0;
bool    lotteryDone    = false;
String  winnerToken    = "";
int     winnerIndex    = -1;

// countdown
unsigned long lotteryStartMs = 0;
bool          countingDown   = false;
const unsigned long LOTTERY_WAIT_MS = 60000; // 1 นาที

// door state
bool          doorUnlocked = false;
unsigned long doorOpenedAt = 0;
const unsigned long DOOR_CLOSE_MS = 5000;

// ============================================================
//  HELPERS: OLED
// ============================================================
void oledClear() { display.clearDisplay(); }

void oledShow(const String& l1, const String& l2, const String& l3 = "") {
  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(l1);

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println(l2);

  if (l3.length()) {
    display.setTextSize(1);
    display.setCursor(0, 50);
    display.println(l3);
  }
  display.display();
}

void oledScrollText(const String& line1, const String& line2) {
  // แสดงข้อความปกติ
  oledShow(line1, line2);
}

// ============================================================
//  HELPERS: BUZZER patterns
// ============================================================
void beepShort() {
  digitalWrite(BUZZER_PIN, HIGH); delay(80);
  digitalWrite(BUZZER_PIN, LOW);
}

void beepDouble() {
  beepShort(); delay(100); beepShort();
}

void beepWin() {
  // เสียงดัง "ติ๊ด ติ๊ด ติ๊ดดด" (ผู้ชนะ)
  int notes[] = {80,60,80,60,200};
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER_PIN, LOW); delay(notes[i]);
    digitalWrite(BUZZER_PIN, HIGH);  delay(60);
  }
}

void beepError() {
  digitalWrite(BUZZER_PIN, LOW); delay(400);
  digitalWrite(BUZZER_PIN, HIGH);
}

// ============================================================
//  HELPERS: LED
// ============================================================
void ledLocked() {
  digitalWrite(LED_RED,   HIGH);   // RED ON  = locked
  digitalWrite(LED_GREEN, LOW);    // GREEN OFF
}

void ledUnlocked() {
  digitalWrite(LED_RED,   LOW);    // RED OFF
  digitalWrite(LED_GREEN, HIGH);   // GREEN ON = unlocked
}

void ledBothOff() {
  digitalWrite(LED_RED,   LOW);
  digitalWrite(LED_GREEN, LOW);
}

// ============================================================
//  TOKEN GENERATOR
// ============================================================
String generateToken(const String& studentId) {
  unsigned long r = esp_random();
  String token = "P2R-" + studentId.substring(0,4) + "-";
  const char* chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  for (int i = 0; i < 6; i++) {
    token += chars[(r >> i) & 31];
  }
  return token;
}

// ============================================================
//  BOOKING: check if studentId already booked same time slot
//  (เวลาชนกันอนุญาต แต่คนเดิมจองซ้ำไม่ได้)
// ============================================================
bool hasOverlap(int startHour, int dur, int skipIndex = -1) {
  int endHour = startHour + dur;
  for (int i = 0; i < bookingCount; i++) {
    if (i == skipIndex) continue;
    int s2 = bookings[i].startHour;
    int e2 = s2 + bookings[i].durationHrs;
    if (startHour < e2 && endHour > s2) return true;
  }
  return false;
}

// ตรวจว่ามีการจองที่เวลาชนกันอยู่ไหม (สำหรับเริ่ม countdown)
bool hasAnyOverlap() {
  for (int i = 0; i < bookingCount; i++) {
    for (int j = i + 1; j < bookingCount; j++) {
      int s1 = bookings[i].startHour, e1 = s1 + bookings[i].durationHrs;
      int s2 = bookings[j].startHour, e2 = s2 + bookings[j].durationHrs;
      if (s1 < e2 && e1 > s2) return true;
    }
  }
  return false;
}

// ============================================================
//  LOTTERY
// ============================================================
void runLottery() {
  if (bookingCount == 0 || lotteryDone) return;

  lotteryDone = true;
  countingDown = false;

  // สุ่มผู้ชนะ
  randomSeed(esp_random());
  winnerIndex = random(0, bookingCount);
  bookings[winnerIndex].isWinner = true;
  winnerToken = bookings[winnerIndex].token;

  String wName = bookings[winnerIndex].name;
  String wId   = bookings[winnerIndex].studentId;

  Serial.println("[LOTTERY] Winner: " + wId + " / " + wName);

  // OLED แสดงผู้ชนะ
  oledShow("** WINNER **", wName, wId);

  // เสียงแจ้งผู้ชนะ
  beepWin();

  // LED สีเขียวกระพริบ 3 ครั้ง
  for (int i = 0; i < 3; i++) {
    ledUnlocked(); delay(300);
    ledLocked();   delay(300);
  }
  ledLocked();

  delay(2000);
  oledShow("PASS2ROOM", "READY", "Waiting unlock...");
}

// ============================================================
//  DOOR UNLOCK
// ============================================================
void unlockDoor(const String& name) {
  doorUnlocked = true;
  doorOpenedAt = millis();

  ledUnlocked();
  oledShow("ACCESS OK", name, "WELCOME!");
  beepWin();

  Serial.println("[DOOR] Unlocked for: " + name);
}

void lockDoor() {
  doorUnlocked = false;
  ledLocked();
  oledShow("PASS2ROOM", "LOCKED", "Ready");
  Serial.println("[DOOR] Locked");
}

// ============================================================
//  WiFi
// ============================================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  oledShow("PASS2ROOM", "CONNECTING", ssid);
  Serial.print("WiFi connecting");
  unsigned long s = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis()-s > 20000) { oledShow("ERROR","NO WIFI",""); beepError(); return; }
    delay(500); Serial.print(".");
  }
  Serial.println("\nIP: " + WiFi.localIP().toString());
  oledShow("PASS2ROOM", "ONLINE", WiFi.localIP().toString());
  beepDouble();
}

void checkWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.disconnect(); WiFi.begin(ssid, password);
  unsigned long s = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis()-s > 15000) { oledShow("ERROR","NO WIFI",""); return; }
    delay(500);
  }
  oledShow("PASS2ROOM","RECONNECTED", WiFi.localIP().toString());
}

// ============================================================
//  API: POST /api/book
//  body JSON: {studentId, name, date, startHour, duration}
// ============================================================
void handleBook() {
  if (!server.hasArg("plain")) { server.send(400,"application/json","{\"error\":\"no_body\"}"); return; }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) { server.send(400,"application/json","{\"error\":\"bad_json\"}"); return; }

  // ตรวจว่าจองเต็มแล้วหรือ lottery เสร็จแล้ว
  if (lotteryDone) { server.send(400,"application/json","{\"error\":\"lottery_done\"}"); return; }
  if (bookingCount >= MAX_BOOKINGS) { server.send(400,"application/json","{\"error\":\"full\"}"); return; }

  String sId  = doc["studentId"] | "";
  String name = doc["name"]      | "";
  String date = doc["date"]      | "";
  int    sHr  = doc["startHour"] | 0;
  int    dur  = doc["duration"]  | 1;

  // validate
  if (sId.length() < 2 || name.length() < 1) { server.send(400,"application/json","{\"error\":\"missing_fields\"}"); return; }
  if (sHr < 8 || sHr > 21) { server.send(400,"application/json","{\"error\":\"time_out_of_range\"}"); return; }
  if (dur < 1 || dur > 3)  { server.send(400,"application/json","{\"error\":\"duration_invalid\"}"); return; }
  if (sHr + dur > 21)      { server.send(400,"application/json","{\"error\":\"exceeds_closing\"}"); return; }

  // ตรวจ Student ID ซ้ำ
  for (int i = 0; i < bookingCount; i++) {
    if (bookings[i].studentId == sId) { server.send(400,"application/json","{\"error\":\"already_booked\"}"); return; }
  }

  // *** ไม่บล็อกเวลาชนแล้ว — อนุญาตให้จองเวลาเดียวกันได้ ***
  // ถ้าเวลาชน ระบบจะเริ่มนับถอยหลังสุ่มผู้ชนะ

  // บันทึก
  String tok = generateToken(sId);
  bookings[bookingCount] = {sId, name, date, sHr, dur, tok, false, false};
  int idx = bookingCount;
  bookingCount++;

  // ตรวจว่ามีเวลาชนกันไหม
  bool overlap = hasAnyOverlap();

  // แจ้ง OLED
  if (overlap) {
    oledShow("TIME CLASH!", name, "Countdown start");
  } else {
    oledShow("BOOKED!", name, sId);
  }
  beepShort();
  delay(1500);
  oledShow("PASS2ROOM", String(bookingCount)+"/3 booked", overlap ? "Conflict!" : "Waiting...");

  Serial.println("[BOOK] " + sId + " " + name + " " + String(sHr) + ":00 +" + String(dur) + "h" + (overlap ? " [CONFLICT]" : ""));

  // Logic การสุ่ม:
  //   - ถ้าครบ 3 คน → สุ่มทันที
  //   - ถ้าเวลาชน และยังไม่นับถอยหลัง → เริ่ม countdown 60 วิ
  //   - ถ้าไม่ชน → รอ (ไม่ต้องสุ่ม)
  if (bookingCount >= MAX_BOOKINGS) {
    delay(500);
    runLottery();
  } else if (overlap && !countingDown) {
    // มีเวลาชน → เริ่มนับถอยหลัง (ไม่สุ่มทันที)
    countingDown   = true;
    lotteryStartMs = millis();
    Serial.println("[LOTTERY] Conflict detected — countdown started");
  }

  // ตอบกลับ
  StaticJsonDocument<200> res;
  res["status"]      = "booked";
  res["token"]       = tok;
  res["slot"]        = idx;
  res["count"]       = bookingCount;
  res["hasConflict"] = overlap;
  res["lotteryIn"]   = countingDown ? max(0UL, LOTTERY_WAIT_MS - (millis()-lotteryStartMs))/1000 : 0;
  String out; serializeJson(res, out);
  server.send(200, "application/json", out);
}

// ============================================================
//  API: GET /api/result?token=xxx
// ============================================================
void handleResult() {
  if (!server.hasArg("token")) { server.send(400,"application/json","{\"error\":\"no_token\"}"); return; }
  String tok = server.arg("token");

  StaticJsonDocument<300> res;
  res["lotteryDone"] = lotteryDone;
  res["count"]       = bookingCount;

  if (!lotteryDone) {
    unsigned long remaining = 0;
    if (countingDown) {
      unsigned long elapsed = millis() - lotteryStartMs;
      remaining = elapsed < LOTTERY_WAIT_MS ? (LOTTERY_WAIT_MS - elapsed) / 1000 : 0;
    }
    res["status"]    = "waiting";
    res["remaining"] = remaining;
    String out; serializeJson(res, out);
    server.send(200, "application/json", out);
    return;
  }

  // lottery เสร็จแล้ว
  bool found = false;
  for (int i = 0; i < bookingCount; i++) {
    if (bookings[i].token == tok) {
      found = true;
      res["status"]   = bookings[i].isWinner ? "winner" : "loser";
      res["name"]     = bookings[i].name;
      res["date"]     = bookings[i].date;
      res["start"]    = bookings[i].startHour;
      res["duration"] = bookings[i].durationHrs;
      break;
    }
  }
  if (!found) { res["status"] = "not_found"; }

  String out; serializeJson(res, out);
  server.send(200, "application/json", out);
}

// ============================================================
//  API: GET /api/status — สถานะระบบ (poll ทุก 3 วิ)
// ============================================================
void handleStatus() {
  StaticJsonDocument<200> res;
  res["count"]       = bookingCount;
  res["max"]         = MAX_BOOKINGS;
  res["lotteryDone"] = lotteryDone;
  res["doorOpen"]    = doorUnlocked;
  res["ip"]          = WiFi.localIP().toString();
  res["rssi"]        = WiFi.RSSI();

  unsigned long remaining = 0;
  if (countingDown && !lotteryDone) {
    unsigned long elapsed = millis() - lotteryStartMs;
    remaining = elapsed < LOTTERY_WAIT_MS ? (LOTTERY_WAIT_MS - elapsed) / 1000 : 0;
  }
  res["remaining"] = remaining;

  String out; serializeJson(res, out);
  server.send(200, "application/json", out);
}

// ============================================================
//  API: POST /api/unlock  body: {token}
// ============================================================
void handleUnlock() {
  if (!server.hasArg("plain")) { server.send(400,"application/json","{\"error\":\"no_body\"}"); return; }

  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400,"application/json","{\"error\":\"bad_json\"}"); return; }

  String tok = doc["token"] | "";

  if (!lotteryDone)        { server.send(403,"application/json","{\"error\":\"lottery_not_done\"}"); return; }
  if (tok != winnerToken)  { server.send(403,"application/json","{\"error\":\"invalid_token\"}"); beepError(); return; }
  if (doorUnlocked)        { server.send(200,"application/json","{\"status\":\"already_open\"}"); return; }

  String wName = bookings[winnerIndex].name;
  unlockDoor(wName);
  server.send(200,"application/json","{\"status\":\"unlocked\"}");
}

// ============================================================
//  HTML PAGE
// ============================================================
const char INDEX_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="th">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PASS2ROOM</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600&family=Noto+Sans+Thai:wght@300;400;600&display=swap');
*{margin:0;padding:0;box-sizing:border-box}
:root{
  --bg:#070a0f;--panel:#0c1018;--card:#111720;--border:#1a2535;
  --accent:#38bdf8;--green:#22d3a5;--red:#f43f5e;--amber:#fb923c;
  --purple:#a78bfa;--text:#dde8f0;--muted:#3d5a70;
  --mono:'IBM Plex Mono',monospace;--sans:'Noto Sans Thai',sans-serif;
}
html,body{height:100%;scroll-behavior:smooth}
body{background:var(--bg);color:var(--text);font-family:var(--sans);min-height:100vh;padding:16px 16px 40px;}

/* grid bg */
body::before{content:'';position:fixed;inset:0;
  background-image:linear-gradient(rgba(56,189,248,.03) 1px,transparent 1px),linear-gradient(90deg,rgba(56,189,248,.03) 1px,transparent 1px);
  background-size:36px 36px;pointer-events:none;z-index:0;}

.wrap{position:relative;z-index:1;max-width:460px;margin:0 auto;display:flex;flex-direction:column;gap:14px;}

/* ── topbar ── */
.topbar{display:flex;align-items:center;justify-content:space-between;
  background:var(--panel);border:1px solid var(--border);border-radius:14px;padding:12px 16px;}
.brand{display:flex;align-items:center;gap:10px;}
.brand-badge{width:36px;height:36px;border:1.5px solid var(--accent);border-radius:9px;
  display:grid;place-items:center;font-family:var(--mono);font-size:13px;font-weight:600;color:var(--accent);}
.brand-name{font-family:var(--mono);font-size:15px;font-weight:600;color:var(--accent);letter-spacing:.05em;}
.brand-sub{font-size:11px;color:var(--muted);margin-top:2px;}
.sys-pill{display:flex;align-items:center;gap:6px;font-family:var(--mono);font-size:11px;
  padding:4px 10px;border-radius:20px;border:1px solid rgba(34,211,165,.25);
  background:rgba(34,211,165,.07);color:var(--green);}
.sys-dot{width:6px;height:6px;border-radius:50%;background:var(--green);animation:blink 2s infinite;}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.25}}

/* ── counter ── */
.counter-row{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;}
.counter-cell{background:var(--panel);border:1px solid var(--border);border-radius:12px;
  padding:14px 10px;text-align:center;}
.counter-cell .num{font-family:var(--mono);font-size:26px;font-weight:600;line-height:1;}
.counter-cell .lbl{font-size:10px;color:var(--muted);margin-top:4px;letter-spacing:.06em;text-transform:uppercase;}
.num-count{color:var(--accent);}
.num-slots{color:var(--amber);}
.num-time{color:var(--purple);}

/* ── countdown ── */
.cdown-card{background:var(--panel);border:1px solid var(--border);border-radius:14px;padding:16px 20px;display:none;}
.cdown-card.visible{display:block;}
.cdown-top{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;}
.cdown-title{font-family:var(--mono);font-size:11px;color:var(--muted);letter-spacing:.07em;}
.cdown-num{font-family:var(--mono);font-size:20px;font-weight:600;color:var(--amber);}
.bar-track{height:4px;background:var(--border);border-radius:2px;overflow:hidden;}
.bar-fill{height:100%;background:linear-gradient(90deg,var(--amber),var(--accent));border-radius:2px;transition:width .5s linear;}

/* ── section ── */
.section{background:var(--panel);border:1px solid var(--border);border-radius:14px;overflow:hidden;}
.section-head{padding:12px 18px;border-bottom:1px solid var(--border);
  font-family:var(--mono);font-size:11px;color:var(--muted);letter-spacing:.07em;
  display:flex;align-items:center;gap:8px;}
.section-body{padding:18px;}

/* ── form ── */
.form-grid{display:flex;flex-direction:column;gap:12px;}
.field{display:flex;flex-direction:column;gap:5px;}
.field label{font-size:11px;color:var(--muted);letter-spacing:.06em;text-transform:uppercase;}
.field input,.field select{
  background:rgba(0,0,0,.35);border:1px solid var(--border);border-radius:9px;
  color:var(--text);font-family:var(--sans);font-size:14px;padding:11px 13px;
  transition:border .2s;outline:none;width:100%;}
.field input:focus,.field select:focus{border-color:var(--accent);}
.field select option{background:#111720;}
.field-row{display:grid;grid-template-columns:1fr 1fr;gap:10px;}

/* ── buttons ── */
.btn{width:100%;padding:14px;border:none;border-radius:11px;
  font-family:var(--mono);font-size:13px;font-weight:600;letter-spacing:.08em;
  cursor:pointer;transition:transform .12s,box-shadow .2s,opacity .15s;
  position:relative;overflow:hidden;display:flex;align-items:center;justify-content:center;gap:8px;}
.btn:active{transform:scale(.97);}
.btn:disabled{opacity:.35;cursor:not-allowed;transform:none!important;}
.btn-primary{background:linear-gradient(135deg,#0ea5e9,#38bdf8);color:#031520;box-shadow:0 4px 20px rgba(56,189,248,.12);}
.btn-primary:not(:disabled):hover{box-shadow:0 4px 28px rgba(56,189,248,.3);transform:translateY(-1px);}
.btn-win{background:linear-gradient(135deg,#059669,#22d3a5);color:#021a12;box-shadow:0 4px 20px rgba(34,211,165,.12);}
.btn-win:not(:disabled):hover{box-shadow:0 4px 28px rgba(34,211,165,.3);transform:translateY(-1px);}
.btn-ghost{background:transparent;border:1px solid var(--border);color:var(--muted);}
.ripple{position:absolute;border-radius:50%;background:rgba(255,255,255,.2);transform:scale(0);animation:rpl .55s linear forwards;pointer-events:none;}
@keyframes rpl{to{transform:scale(5);opacity:0;}}

/* ── result card ── */
.result-card{border-radius:14px;padding:24px 20px;text-align:center;display:none;flex-direction:column;align-items:center;gap:14px;}
.result-card.visible{display:flex;}
.result-card.win{background:rgba(34,211,165,.06);border:1px solid rgba(34,211,165,.25);}
.result-card.lose{background:rgba(244,63,94,.06);border:1px solid rgba(244,63,94,.2);}
.result-icon{font-size:48px;}
.result-title{font-family:var(--mono);font-size:18px;font-weight:600;}
.result-card.win .result-title{color:var(--green);}
.result-card.lose .result-title{color:var(--red);}
.result-info{font-size:13px;color:var(--muted);line-height:1.8;}
.token-box{background:rgba(0,0,0,.4);border:1px solid var(--border);border-radius:8px;
  padding:8px 14px;font-family:var(--mono);font-size:13px;color:var(--accent);letter-spacing:.08em;}

/* ── log ── */
.log-body{max-height:120px;overflow-y:auto;padding:12px 18px;display:flex;flex-direction:column;gap:5px;}
.log-body::-webkit-scrollbar{width:3px;}
.log-body::-webkit-scrollbar-thumb{background:var(--border);}
.log-row{display:flex;gap:8px;font-family:var(--mono);font-size:11px;animation:fin .2s ease;}
@keyframes fin{from{opacity:0;transform:translateY(3px)}}
.lt{color:var(--muted);flex-shrink:0;}
.lm{color:var(--text);}
.lm.ok{color:var(--green);}
.lm.err{color:var(--red);}
.lm.warn{color:var(--amber);}
.lm.info{color:var(--accent);}

/* ── toast ── */
#toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%) translateY(16px);
  background:var(--card);border:1px solid var(--border);border-radius:10px;
  padding:10px 18px;font-family:var(--mono);font-size:12px;
  opacity:0;transition:.25s;white-space:nowrap;z-index:99;pointer-events:none;}
#toast.show{opacity:1;transform:translateX(-50%) translateY(0);}

/* booking slots visual */
.slots-row{display:flex;gap:8px;margin-bottom:14px;}
.slot{flex:1;height:6px;border-radius:3px;background:var(--border);transition:.3s;}
.slot.filled{background:var(--accent);}
.slot.winner{background:var(--green);}

/* ── refresh top button ── */
.topbar-right{display:flex;align-items:center;gap:8px;}
.btn-refresh-top{
  width:30px;height:30px;border-radius:8px;
  border:1px solid var(--border);background:rgba(56,189,248,.06);
  color:var(--accent);cursor:pointer;font-size:14px;
  display:grid;place-items:center;transition:background .2s;}
.btn-refresh-top:hover{background:rgba(56,189,248,.16);}
.btn-refresh-top svg{transition:transform .55s ease;}
.btn-refresh-top.spinning svg{transform:rotate(360deg);}

/* ── reset booking button ── */
.btn-reset{
  background:transparent;
  border:1px solid rgba(251,146,60,.35);
  color:var(--amber);
}
.btn-reset:not(:disabled):hover{background:rgba(251,146,60,.08);}

/* ── slot machine section ── */
.slot-machine{background:var(--panel);border:1px solid var(--border);border-radius:14px;overflow:hidden;}
.slot-machine .section-head{padding:12px 18px;border-bottom:1px solid var(--border);
  font-family:var(--mono);font-size:11px;color:var(--muted);letter-spacing:.07em;
  display:flex;align-items:center;gap:8px;}
.slot-machine-body{padding:18px;display:flex;flex-direction:column;gap:14px;}

/* reel */
.reel-outer{
  width:100%;height:68px;
  background:rgba(0,0,0,.45);
  border:1px solid var(--border);border-radius:12px;
  overflow:hidden;position:relative;
}
.reel-outer::before,.reel-outer::after{
  content:'';position:absolute;left:0;right:0;height:18px;z-index:2;pointer-events:none;}
.reel-outer::before{top:0;background:linear-gradient(to bottom,rgba(12,16,24,.95),transparent);}
.reel-outer::after{bottom:0;background:linear-gradient(to top,rgba(12,16,24,.95),transparent);}
.reel-line{
  position:absolute;left:12px;right:12px;top:50%;
  transform:translateY(-50%);height:1px;
  background:var(--accent);opacity:.2;z-index:3;pointer-events:none;}
.reel-track{display:flex;flex-direction:column;align-items:center;}
.reel-item{
  height:68px;width:100%;
  display:flex;align-items:center;justify-content:center;
  font-family:var(--mono);font-size:17px;font-weight:600;
  color:var(--text);flex-shrink:0;padding:0 16px;text-align:center;
}
.reel-item.winner{color:var(--green);}

/* result banner */
.spin-result{
  display:none;width:100%;
  background:linear-gradient(135deg,rgba(34,211,165,.07),rgba(56,189,248,.07));
  border:1px solid rgba(34,211,165,.3);border-radius:12px;
  padding:16px;flex-direction:column;align-items:center;gap:8px;
  animation:popIn .4s cubic-bezier(.34,1.56,.64,1);
}
.spin-result.visible{display:flex;}
@keyframes popIn{from{opacity:0;transform:scale(.85)}to{opacity:1;transform:scale(1)}}
.spin-winner-lbl{font-size:10px;font-family:var(--mono);color:var(--muted);letter-spacing:.1em;}
.spin-winner-name{font-size:22px;font-weight:700;color:var(--green);font-family:var(--mono);text-align:center;}
.spin-confetti{font-size:22px;letter-spacing:4px;animation:wiggle .6s infinite alternate;}
@keyframes wiggle{from{transform:rotate(-8deg)}to{transform:rotate(8deg)}}

/* spin buttons */
.spin-btn-row{display:flex;gap:8px;}
.btn-spin{
  flex:1;padding:13px;border:none;border-radius:11px;
  background:linear-gradient(135deg,#7c3aed,#a78bfa);
  color:#fff;font-family:var(--mono);font-size:12px;font-weight:600;
  letter-spacing:.08em;cursor:pointer;
  transition:transform .12s,box-shadow .2s,opacity .2s;
  position:relative;overflow:hidden;
  display:flex;align-items:center;justify-content:center;gap:6px;
}
.btn-spin:not(:disabled):hover{box-shadow:0 4px 24px rgba(167,139,250,.3);transform:translateY(-1px);}
.btn-spin:active{transform:scale(.97);}
.btn-spin:disabled{opacity:.35;cursor:not-allowed;transform:none!important;}
.btn-spin-reset{
  padding:13px 16px;border-radius:11px;
  background:transparent;border:1px solid var(--border);
  color:var(--muted);font-family:var(--mono);font-size:12px;
  cursor:pointer;transition:.2s;display:flex;align-items:center;gap:6px;
}
.btn-spin-reset:hover{background:rgba(255,255,255,.04);color:var(--text);}
</style>
</head>
<body>
<div class="wrap">

  <!-- TOPBAR -->
  <div class="topbar">
    <div class="brand">
      <div class="brand-badge">P2</div>
      <div>
        <div class="brand-name">PASS2ROOM</div>
        <div class="brand-sub">Smart Room Booking</div>
      </div>
    </div>
    <div class="topbar-right">
      <button class="btn-refresh-top" id="btn-refresh-top" title="Refresh" onclick="manualRefresh(this)">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M23 4v6h-6"/><path d="M1 20v-6h6"/>
          <path d="M3.51 9a9 9 0 0114.36-3.36L23 10M1 14l5.09 4.36A9 9 0 0020.49 15"/>
        </svg>
      </button>
      <div class="sys-pill"><span class="sys-dot"></span><span id="sys-txt">ONLINE</span></div>
    </div>
  </div>

  <!-- COUNTERS -->
  <div class="counter-row">
    <div class="counter-cell"><div class="num num-count" id="c-count">0</div><div class="lbl">Booked</div></div>
    <div class="counter-cell"><div class="num num-slots" id="c-slots">3</div><div class="lbl">Slots left</div></div>
    <div class="counter-cell"><div class="num num-time" id="c-time">--:--</div><div class="lbl">Time</div></div>
  </div>

  <!-- COUNTDOWN -->
  <div class="cdown-card" id="cdown-card">
    <div class="cdown-top">
      <span class="cdown-title">▸ LOTTERY IN</span>
      <span class="cdown-num" id="cdown-num">60s</span>
    </div>
    <div class="bar-track"><div class="bar-fill" id="cdown-bar" style="width:100%"></div></div>
  </div>

  <!-- BOOKING FORM -->
  <div class="section" id="form-section">
    <div class="section-head">✦ BOOK A ROOM</div>
    <div class="section-body">
      <!-- slot indicators -->
      <div class="slots-row" id="slots-visual">
        <div class="slot" id="slot-0"></div>
        <div class="slot" id="slot-1"></div>
        <div class="slot" id="slot-2"></div>
      </div>

      <div class="form-grid">
        <div class="field">
          <label>Student ID</label>
          <input id="f-sid" type="text" placeholder="670XXXXXXX" maxlength="20">
        </div>
        <div class="field">
          <label>ชื่อ-นามสกุล</label>
          <input id="f-name" type="text" placeholder="ชื่อ นามสกุล">
        </div>
        <div class="field">
          <label>วันที่</label>
          <input id="f-date" type="date">
        </div>
        <div class="field-row">
          <div class="field">
            <label>เริ่ม (8–21)</label>
            <select id="f-start">
              <option value="">-- เลือก --</option>
            </select>
          </div>
          <div class="field">
            <label>ระยะเวลา</label>
            <select id="f-dur">
              <option value="1">1 ชั่วโมง</option>
              <option value="2">2 ชั่วโมง</option>
              <option value="3">3 ชั่วโมง</option>
            </select>
          </div>
        </div>
        <button class="btn btn-primary" id="btn-book" onclick="submitBook(this,event)">
          ✦ จองห้อง
        </button>
      </div>
    </div>
  </div>

  <!-- RESULT (ซ่อนก่อน แสดงหลัง lottery) -->
  <div class="section" id="result-section" style="display:none;">
    <div class="section-head">✦ ผลการสุ่ม</div>
    <div class="section-body">
      <div class="result-card" id="result-card">
        <div class="result-icon" id="res-icon">🎲</div>
        <div class="result-title" id="res-title">รอผล...</div>
        <div class="result-info" id="res-info"></div>
        <div class="token-box" id="res-token" style="display:none;"></div>
        <button class="btn btn-win" id="btn-unlock" style="display:none;" onclick="unlockDoor(this,event)">
          🔓 UNLOCK DOOR
        </button>
      </div>
    </div>
  </div>

  <!-- SLOT MACHINE — สุ่มผู้โชคดี -->
  <div class="slot-machine">
    <div class="section-head">🎰 สุ่มผู้โชคดีได้ห้อง</div>
    <div class="slot-machine-body">

      <textarea id="spin-names"
        style="width:100%;min-height:58px;resize:vertical;background:rgba(0,0,0,.35);
               border:1px solid var(--border);border-radius:9px;padding:10px 13px;
               color:var(--text);font-family:var(--mono);font-size:12px;outline:none;
               transition:border .2s;line-height:1.6;"
        placeholder="ใส่ชื่อผู้เข้าร่วม คนละบรรทัด&#10;Alice&#10;Bob&#10;Charlie"
        onfocus="this.style.borderColor='var(--accent)'"
        onblur="this.style.borderColor='var(--border)'"></textarea>
      <span style="font-size:10px;color:var(--muted);font-family:var(--mono);">※ กด Enter แยกแต่ละชื่อ — ต้องใส่อย่างน้อย 2 คน</span>

      <div class="reel-outer">
        <div class="reel-line"></div>
        <div class="reel-track" id="reel-track">
          <div class="reel-item" id="reel-display">— กรอกชื่อก่อนสุ่ม —</div>
        </div>
      </div>

      <div class="spin-result" id="spin-result">
        <div class="spin-confetti">🎉 🏆 🎉</div>
        <div class="spin-winner-lbl">ผู้โชคดีได้ห้อง</div>
        <div class="spin-winner-name" id="spin-winner-name">—</div>
      </div>

      <div class="spin-btn-row">
        <button class="btn-spin" id="btn-spin" onclick="startSpin(this,event)">🎰 สุ่มเลย!</button>
        <button class="btn-spin-reset" onclick="resetSpin()">↺ รีเซ็ต</button>
      </div>
    </div>
  </div>

  <!-- RESET BOOKING -->
  <button class="btn btn-reset" onclick="resetBooking(this,event)">🔄 จองใหม่ / RESET BOOKING</button>

  <!-- LOG -->
  <div class="section">
    <div class="section-head">▸ EVENT LOG</div>
    <div class="log-body" id="log"></div>
  </div>

</div>
<div id="toast"></div>

<script>
const BASE = window.location.origin;
let myToken    = localStorage.getItem('p2r_token') || '';
let myName     = localStorage.getItem('p2r_name')  || '';
let hasBooked  = !!myToken;
let lotteryDone = false;
let pollTimer  = null;
let cdownTotal = 60;

// ---- init form ----
(function(){
  const sel = document.getElementById('f-start');
  for(let h=8;h<=21;h++){
    const o=document.createElement('option');
    o.value=h; o.textContent=h+':00 น.';
    sel.appendChild(o);
  }
  const today = new Date().toISOString().slice(0,10);
  document.getElementById('f-date').value = today;
  document.getElementById('f-date').min   = today;

  // ถ้าเคยจองแล้ว
  if(hasBooked){ showBookedState(); }
  updateClock();
  setInterval(updateClock, 1000);
  fetchStatus();
  pollTimer = setInterval(pollResult, 3000);
})();

function updateClock(){
  const now=new Date();
  const h=String(now.getHours()).padStart(2,'0');
  const m=String(now.getMinutes()).padStart(2,'0');
  document.getElementById('c-time').textContent=h+':'+m;
}

// ---- log ----
function log(msg,type='info'){
  const el=document.getElementById('log');
  const t=new Date().toTimeString().slice(0,8);
  const row=document.createElement('div');
  row.className='log-row';
  row.innerHTML=`<span class="lt">${t}</span><span class="lm ${type}">${msg}</span>`;
  el.prepend(row);
  if(el.children.length>30)el.lastChild.remove();
}

// ---- toast ----
function toast(msg,color){
  const el=document.getElementById('toast');
  el.textContent=msg; el.style.color=color||'var(--text)';
  el.classList.add('show');
  clearTimeout(el._t);
  el._t=setTimeout(()=>el.classList.remove('show'),2800);
}

// ---- ripple ----
function ripple(btn,e){
  const r=document.createElement('span'); r.className='ripple';
  const rc=btn.getBoundingClientRect(),sz=Math.max(rc.width,rc.height);
  r.style.cssText=`width:${sz}px;height:${sz}px;left:${e.clientX-rc.left-sz/2}px;top:${e.clientY-rc.top-sz/2}px`;
  btn.appendChild(r); setTimeout(()=>r.remove(),600);
}

// ---- slot indicators ----
function updateSlots(count,winIdx=-1){
  for(let i=0;i<3;i++){
    const s=document.getElementById('slot-'+i);
    if(i<count) s.classList.add(winIdx===i?'winner':'filled');
    else s.classList.remove('filled','winner');
  }
}

// ---- fetch status (poll) ----
async function fetchStatus(){
  try{
    const res=await fetch(BASE+'/api/status');
    const d=await res.json();
    document.getElementById('c-count').textContent=d.count;
    document.getElementById('c-slots').textContent=d.max-d.count;
    document.getElementById('sys-txt').textContent='ONLINE';
    updateSlots(d.count);

    if(!lotteryDone && d.remaining>0){
      showCountdown(d.remaining);
    } else if(d.lotteryDone && !lotteryDone){
      lotteryDone=true;
      hideCountdown();
      if(hasBooked) pollResult();
    }
    if(d.lotteryDone && d.count===0){
      // ระบบว่าง ซ่อน form
    }
  } catch(e){
    document.getElementById('sys-txt').textContent='OFFLINE';
  }
}
setInterval(fetchStatus,3000);

// ---- countdown ----
let cdownInterval=null;
function showCountdown(secs){
  const card=document.getElementById('cdown-card');
  card.classList.add('visible');
  let left=secs;
  clearInterval(cdownInterval);
  cdownInterval=setInterval(()=>{
    left--;
    document.getElementById('cdown-num').textContent=left+'s';
    document.getElementById('cdown-bar').style.width=(left/60*100)+'%';
    if(left<=0){ clearInterval(cdownInterval); hideCountdown(); }
  },1000);
}
function hideCountdown(){ document.getElementById('cdown-card').classList.remove('visible'); }

// ---- submit booking ----
async function submitBook(btn,e){
  ripple(btn,e);
  const sid  = document.getElementById('f-sid').value.trim();
  const name = document.getElementById('f-name').value.trim();
  const date = document.getElementById('f-date').value;
  const sHr  = parseInt(document.getElementById('f-start').value);
  const dur  = parseInt(document.getElementById('f-dur').value);

  if(!sid||!name||!date||!sHr){ toast('กรุณากรอกข้อมูลให้ครบ','var(--red)'); return; }
  if(sHr+dur>21){ toast('เวลาเกิน 21:00 น.','var(--red)'); return; }

  btn.disabled=true;
  log('Booking: '+sid+' '+name,'info');

  try{
    const res=await fetch(BASE+'/api/book',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({studentId:sid,name,date,startHour:sHr,duration:dur})
    });
    const d=await res.json();

    if(d.status==='booked'){
      myToken=d.token; myName=name;
      localStorage.setItem('p2r_token',myToken);
      localStorage.setItem('p2r_name',myName);
      hasBooked=true;
      if(d.hasConflict){
        toast('⚠ เวลาชนกัน! กำลังนับถอยหลังสุ่ม...','var(--amber)');
        log('Booked (conflict!) รอสุ่ม — Token: '+myToken,'warn');
      } else {
        toast('✓ จองสำเร็จ! รอผลสุ่ม','var(--green)');
        log('Booked! Token: '+myToken,'ok');
      }
      showBookedState();
      fetchStatus();
      if(d.lotteryIn>0) showCountdown(d.lotteryIn);
    } else {
      const msgs={
        'already_booked':'คุณจองไปแล้ว',
        'full':'ที่นั่งเต็มแล้ว (3/3)',
        'time_out_of_range':'เวลาต้อง 8:00–21:00',
        'duration_invalid':'จองได้ 1–3 ชั่วโมง',
        'exceeds_closing':'เกินเวลาปิด 21:00',
        'lottery_done':'การสุ่มเสร็จแล้ว'
      };
      toast(msgs[d.error]||'เกิดข้อผิดพลาด','var(--red)');
      log('Error: '+d.error,'err');
    }
  } catch(e){
    toast('ไม่สามารถเชื่อมต่อ ESP32','var(--red)');
    log('Connection failed','err');
  } finally {
    setTimeout(()=>btn.disabled=false,1500);
  }
}

// ---- hide form after booking ----
function showBookedState(){
  document.getElementById('btn-book').disabled=true;
  document.getElementById('btn-book').textContent='✓ จองแล้ว';
  document.getElementById('f-sid').disabled=true;
  document.getElementById('f-name').disabled=true;
  document.getElementById('f-date').disabled=true;
  document.getElementById('f-start').disabled=true;
  document.getElementById('f-dur').disabled=true;
  document.getElementById('result-section').style.display='block';
}

// ---- poll result ----
async function pollResult(){
  if(!hasBooked||!myToken) return;
  try{
    const res=await fetch(BASE+'/api/result?token='+myToken);
    const d=await res.json();

    if(!d.lotteryDone) return;

    lotteryDone=true;
    clearInterval(pollTimer);

    const card=document.getElementById('result-card');
    card.classList.add('visible');

    if(d.status==='winner'){
      card.classList.add('win');
      document.getElementById('res-icon').textContent='🏆';
      document.getElementById('res-title').textContent='คุณได้รับสิทธิ์!';
      document.getElementById('res-info').innerHTML=
        '<b>'+d.name+'</b><br>วันที่ '+d.date+'<br>'+d.start+':00 – '+(d.start+d.duration)+':00 น.';
      const tb=document.getElementById('res-token');
      tb.textContent=myToken; tb.style.display='block';
      document.getElementById('btn-unlock').style.display='flex';
      toast('🏆 คุณชนะ! กด Unlock ได้เลย','var(--green)');
      log('WINNER! '+myName,'ok');
    } else if(d.status==='loser'){
      card.classList.add('lose');
      document.getElementById('res-icon').textContent='😔';
      document.getElementById('res-title').textContent='ไม่ได้รับสิทธิ์';
      document.getElementById('res-info').textContent='ขอโทษด้วย '+d.name+'\nลองใหม่รอบหน้านะครับ';
      toast('ไม่ได้รับสิทธิ์ในรอบนี้','var(--amber)');
      log('Not selected: '+myName,'warn');
    }
  } catch(e){}
}

// ---- unlock door ----
async function unlockDoor(btn,e){
  ripple(btn,e);
  btn.disabled=true;
  log('Sending unlock...','info');
  try{
    const res=await fetch(BASE+'/api/unlock',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({token:myToken})
    });
    const d=await res.json();
    if(d.status==='unlocked'||d.status==='already_open'){
      toast('🔓 ประตูเปิดแล้ว!','var(--green)');
      log('Door unlocked!','ok');
      btn.textContent='✓ ประตูเปิดแล้ว';
    } else {
      toast('Token ไม่ถูกต้อง','var(--red)');
      log('Unlock rejected','err');
      btn.disabled=false;
    }
  } catch(e){
    toast('เชื่อมต่อไม่ได้','var(--red)');
    btn.disabled=false;
  }
}
// ============================================================
//  SLOT MACHINE
// ============================================================
let isSpinning = false;
let spinInterval = null;

function getSpinNames(){
  return document.getElementById('spin-names').value
    .split('\n').map(n=>n.trim()).filter(Boolean);
}

function setReel(txt, cls){
  document.getElementById('reel-track').innerHTML =
    `<div class="reel-item${cls?' '+cls:''}">${txt}</div>`;
}

function startSpin(btn, e){
  ripple(btn, e);
  const names = getSpinNames();
  if(names.length < 2){ toast('⚠ กรอกชื่ออย่างน้อย 2 คน','var(--amber)'); return; }
  if(isSpinning) return;

  isSpinning = true;
  btn.disabled = true;
  document.getElementById('spin-result').classList.remove('visible');
  log('เริ่มสุ่มผู้โชคดี...','info');

  const winner = names[Math.floor(Math.random() * names.length)];
  let elapsed = 0;
  const FAST_END = 1400, SLOW_END = 2400, TOTAL = 2900;

  clearInterval(spinInterval);
  spinInterval = setInterval(()=>{
    elapsed += 60;
    if(elapsed < FAST_END){
      setReel(names[Math.floor(Math.random()*names.length)], '');
    } else if(elapsed < SLOW_END){
      if(Math.random() < 0.35) setReel(names[Math.floor(Math.random()*names.length)], '');
    } else if(elapsed < TOTAL){
      setReel(elapsed%240 < 120 ? winner : names[Math.floor(Math.random()*names.length)],
              elapsed%240 < 120 ? 'winner' : '');
    } else {
      clearInterval(spinInterval);
      setReel(winner, 'winner');
      document.getElementById('spin-winner-name').textContent = winner;
      document.getElementById('spin-result').classList.add('visible');
      log('ผู้โชคดี: '+winner,'ok');
      toast('🏆 '+winner+' ได้ห้อง!','var(--green)');
      isSpinning = false;
      btn.disabled = false;
    }
  }, 60);
}

function resetSpin(){
  if(isSpinning){ clearInterval(spinInterval); isSpinning = false; }
  document.getElementById('spin-result').classList.remove('visible');
  document.getElementById('spin-names').value = '';
  document.getElementById('btn-spin').disabled = false;
  setReel('— กรอกชื่อก่อนสุ่ม —', '');
  log('รีเซ็ตการสุ่มแล้ว','info');
}

// ============================================================
//  REFRESH + RESET BOOKING
// ============================================================
function manualRefresh(btn){
  btn.classList.add('spinning');
  fetchStatus();
  setTimeout(()=>btn.classList.remove('spinning'), 600);
  toast('✓ Refreshed','var(--accent)');
  log('Manual refresh','info');
}

function resetBooking(btn, e){
  ripple(btn, e);
  // ล้าง localStorage
  localStorage.removeItem('p2r_token');
  localStorage.removeItem('p2r_name');
  myToken = ''; myName = ''; hasBooked = false; lotteryDone = false;
  // reset form
  ['f-sid','f-name'].forEach(id=>{ const el=document.getElementById(id); el.value=''; el.disabled=false; });
  ['f-date','f-start','f-dur'].forEach(id=>{ document.getElementById(id).disabled=false; });
  const bb = document.getElementById('btn-book');
  bb.disabled = false; bb.textContent = '✦ จองห้อง';
  // ซ่อน result
  document.getElementById('result-section').style.display='none';
  document.getElementById('result-card').classList.remove('visible','win','lose');
  document.getElementById('btn-unlock').style.display='none';
  document.getElementById('res-token').style.display='none';
  // reset slot visual
  updateSlots(0);
  resetSpin();
  hideCountdown();
  log('Reset booking — พร้อมจองใหม่','ok');
  toast('🔄 จองใหม่แล้ว — กรอกข้อมูลได้เลย','var(--accent)');
}
</script>
</body>
</html>)rawhtml";

// ============================================================
//  HANDLERS → ROUTES
// ============================================================
void handleRoot()   { server.send_P(200,"text/html",INDEX_HTML); }

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(LED_RED,    OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // ตั้งค่าเริ่มต้นให้ชัดเจน
  digitalWrite(LED_RED,    LOW);
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);          // รอให้ pin stable
  ledLocked();         // RED = ON (ล็อค)

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed!"); for(;;);
  }

  oledShow("PASS2ROOM", "STARTING", "v3.0");
  delay(1000);

  connectWiFi();

  // Routes
  server.on("/",              HTTP_GET,  handleRoot);
  server.on("/api/status",    HTTP_GET,  handleStatus);
  server.on("/api/book",      HTTP_POST, handleBook);
  server.on("/api/result",    HTTP_GET,  handleResult);
  server.on("/api/unlock",    HTTP_POST, handleUnlock);
  server.begin();

  oledShow("PASS2ROOM", "READY", WiFi.localIP().toString());
  Serial.println("Server ready: http://" + WiFi.localIP().toString());
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  checkWiFi();
  server.handleClient();

  // Auto-lock ประตูหลัง 5 วินาที
  if (doorUnlocked && millis() - doorOpenedAt > DOOR_CLOSE_MS) {
    lockDoor();
  }

  // Countdown lottery อัตโนมัติ
  if (countingDown && !lotteryDone) {
    unsigned long elapsed = millis() - lotteryStartMs;
    if (elapsed >= LOTTERY_WAIT_MS) {
      runLottery();
    } else {
      // แสดง countdown บน OLED ทุก 5 วินาที
      static unsigned long lastOledUpdate = 0;
      if (millis() - lastOledUpdate > 5000) {
        lastOledUpdate = millis();
        int secsLeft = (LOTTERY_WAIT_MS - elapsed) / 1000;
        oledShow("LOTTERY IN", String(secsLeft)+" sec", String(bookingCount)+"/3 booked");
      }
    }
  }
}
