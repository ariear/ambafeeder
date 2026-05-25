#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// Konfigurasi Access Point & WebServer
const char* ssid = "Amba Feeder V1";
const char* hostname = "amba";
const char* passphrase = "12345678";
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer server(80);
const byte DNS_PORT = 53;

// Konfigurasi Perangkat Keras
const int SERVO_PIN = 13;
const int SERVO_CLOSED_ANGLE = 0;
const int SERVO_OPEN_ANGLE = 40;
const unsigned long TIME_PER_GRAM_MS = 500; // Kalibrasi: Waktu buka servo per 1 gram pakan

Servo feederServo;
RTC_DS1307 rtc;
Preferences preferences;

// Struktur Data
struct Schedule {
  int id;
  int hour;
  int minute;
  int grams;
  bool active;
};

struct HistoryLog {
  String timestamp;
  int grams;
  String type;
};

// Variabel Global
const int MAX_SCHEDULES = 10;
const int MAX_HISTORY = 10;
Schedule schedules[MAX_SCHEDULES];
HistoryLog history[MAX_HISTORY];
int scheduleCount = 0;
int historyIndex = 0;
int historyCount = 0;

// Variabel Kontrol State Machine Servo
bool isFeeding = false;
unsigned long feedStartTime = 0;
unsigned long currentFeedDuration = 0;

// Variabel Kontrol Smooth Servo
int currentServoAngle = SERVO_CLOSED_ANGLE;
int targetServoAngle = SERVO_CLOSED_ANGLE;
unsigned long lastServoMoveTime = 0;
const int SERVO_DELAY_MS = 10; // Kecepatan pergerakan servo (ms per derajat)

// Deklarasi fungsi front-end (Halaman SPA - Desain Baru)
const char* getHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>AmbaFeeder</title>
  <link href="https://fonts.googleapis.com/css2?family=DM+Sans:wght@400;500;600;700&display=swap" rel="stylesheet">
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      padding: 14px 18px 40px;
      background-color: #f8fafb;
      font-family: 'DM Sans', 'Segoe UI', sans-serif;
      color: #1a2533;
    }

    /* HEADER */
    .header {
      display: flex;
      align-items: center;
      font-size: 11px;
      margin-bottom: 24px;
    }
    .header h1 {
      margin-left: 10px;
      font-size: 16px;
      font-weight: 700;
      color: #025f89;
    }

    /* SUB-HEADER */
    .sub-header { margin-bottom: 18px; }
    .sub-header p:nth-child(1) {
      color: #40484e;
      font-size: 11px;
      font-weight: 600;
      letter-spacing: 0.08em;
      margin-bottom: 2px;
    }
    .sub-header h4 {
      font-size: 22px;
      font-weight: 700;
    }
    .sub-header-otomatis {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 18px;
    }
    .sub-header-otomatis .sub-header { margin-bottom: 0; }
    .sub-header-otomatis button {
      background-color: #c8e6ff;
      padding: 12px 14px;
      border-radius: 12px;
      border: none;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      transition: background 0.2s;
    }
    .sub-header-otomatis button:hover { background-color: #a8d4f5; }

    /* STATUS CHIP */
    .status-chip {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      background: white;
      border-radius: 20px;
      padding: 6px 14px;
      font-size: 12px;
      font-weight: 500;
      color: #40484e;
      box-shadow: 0 1px 4px rgba(0,0,0,0.08);
      margin-bottom: 20px;
    }
    .status-dot {
      width: 8px; height: 8px;
      border-radius: 50%;
      background: #b0b0b0;
    }
    .status-dot.feeding { background: #22c55e; animation: pulse 1s infinite; }
    @keyframes pulse {
      0%, 100% { opacity: 1; }
      50% { opacity: 0.4; }
    }

    /* MANUAL CARD */
    .manual {
      background-color: #eaeaeb;
      padding: 18px;
      border-radius: 20px;
      display: flex;
      flex-direction: column;
      margin-bottom: 28px;
    }
    .manual label {
      margin-bottom: 8px;
      font-weight: 500;
      color: #40484e;
      font-size: 14px;
    }
    .manual span {
      display: flex;
      align-items: center;
      background-color: white;
      padding: 14px 18px;
      border-radius: 16px;
      margin-bottom: 16px;
      gap: 10px;
    }
    .manual span input {
      width: 100%;
      border: none;
      outline: none;
      font-size: 16px;
      font-family: inherit;
      font-weight: 500;
      color: #1a2533;
      background: transparent;
    }
    .manual button {
      background-color: #025f89;
      color: white;
      padding: 16px 0;
      border-radius: 16px;
      font-size: 16px;
      font-weight: 600;
      display: flex;
      align-items: center;
      justify-content: center;
      border: none;
      cursor: pointer;
      gap: 12px;
      transition: background 0.2s, transform 0.1s;
      font-family: inherit;
    }
    .manual button:hover { background-color: #014e72; }
    .manual button:active { transform: scale(0.98); }
    .manual button:disabled { background-color: #7fb3cc; cursor: not-allowed; }

    /* SCHEDULE CARD */
    .schedule {
      background-color: white;
      padding: 16px 20px;
      border-radius: 20px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 12px;
      box-shadow: 0 1px 4px rgba(0,0,0,0.06);
    }
    .schedule .kiri { display: flex; align-items: center; }
    .schedule .kiri .icon {
      background-color: #acedda;
      padding: 9px 11px;
      border-radius: 80px;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .schedule .kiri .content { margin-left: 16px; }
    .schedule .kiri .content p:nth-child(1) {
      font-size: 18px;
      font-weight: 600;
      margin-bottom: 3px;
    }
    .schedule .kiri .content p:nth-child(2) {
      font-size: 13px;
      font-weight: 400;
      color: #40484e;
    }
    .schedule .kanan { display: flex; align-items: center; gap: 8px; }
    .schedule .kanan button {
      background-color: transparent;
      border: none;
      cursor: pointer;
      padding: 6px;
      border-radius: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
      transition: background 0.15s;
    }
    .schedule .kanan button:hover { background: #f2f4f5; }

    /* ADD SCHEDULE MODAL OVERLAY */
    .modal-overlay {
      display: none;
      position: fixed;
      inset: 0;
      background: rgba(0,0,0,0.3);
      z-index: 100;
      align-items: flex-end;
      justify-content: center;
    }
    .modal-overlay.open { display: flex; }
    .modal {
      background: white;
      border-radius: 24px 24px 0 0;
      padding: 24px 20px 36px;
      width: 100%;
      max-width: 480px;
      animation: slideUp 0.25s ease;
    }
    @keyframes slideUp {
      from { transform: translateY(100%); }
      to { transform: translateY(0); }
    }
    .modal h3 { font-size: 18px; font-weight: 700; margin-bottom: 20px; }
    .modal label {
      display: block;
      font-size: 13px;
      font-weight: 500;
      color: #40484e;
      margin-bottom: 6px;
      margin-top: 14px;
    }
    .modal input {
      width: 100%;
      padding: 14px 16px;
      border: 1.5px solid #e0e0e0;
      border-radius: 14px;
      font-size: 15px;
      font-family: inherit;
      outline: none;
      transition: border 0.2s;
    }
    .modal input:focus { border-color: #025f89; }
    .modal-actions { display: flex; gap: 10px; margin-top: 22px; }
    .modal-actions button {
      flex: 1;
      padding: 15px;
      border-radius: 14px;
      font-size: 15px;
      font-weight: 600;
      font-family: inherit;
      border: none;
      cursor: pointer;
      transition: background 0.2s;
    }
    .btn-cancel { background: #f2f4f5; color: #40484e; }
    .btn-cancel:hover { background: #e5e7e9; }
    .btn-save { background: #025f89; color: white; }
    .btn-save:hover { background: #014e72; }

    /* TIMELINE */
    .timeline { position: relative; padding-left: 32px; }
    .timeline::before {
      content: "";
      position: absolute;
      left: 8px;
      top: 18px;
      bottom: 18px;
      width: 2px;
      background: #d0d0d0;
    }
    .tl-item { position: relative; margin-bottom: 12px; }
    .tl-dot {
      position: absolute;
      left: -24px;
      top: 16px;
      width: 14px; height: 14px;
      border-radius: 50%;
      background: #b0b0b0;
      border: 2px solid #d0d0d0;
    }
    .tl-item.active .tl-dot {
      background: #025f89;
      border-color: #025f89;
    }
    .tl-card {
      background: #f2f4f5;
      border-radius: 14px;
      padding: 14px 16px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .tl-card .tl-time { font-size: 15px; font-weight: 600; margin-bottom: 4px; }
    .tl-card .tl-desc { font-size: 13px; color: #40484e; }
    .badge {
      font-size: 11px;
      font-weight: 600;
      padding: 4px 10px;
      border-radius: 20px;
      text-transform: uppercase;
    }
    .badge.auto { background: #d4eee7; color: #0f6e56; }
    .badge.manual { background: #dce5f0; color: #185fa5; }

    .empty-state {
      text-align: center;
      padding: 24px 0;
      color: #888;
      font-size: 14px;
    }
  </style>
</head>
<body>

  <div class="header">
    <svg width="20" height="17" viewBox="0 0 20 17" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M0 16.7V14.75C0.483333 14.75 0.895833 14.675 1.2375 14.525C1.57917 14.375 1.925 14.2125 2.275 14.0375C2.625 13.8625 3.0125 13.7042 3.4375 13.5625C3.8625 13.4208 4.3875 13.35 5.0125 13.35C5.6375 13.35 6.15417 13.4208 6.5625 13.5625C6.97083 13.7042 7.35 13.8625 7.7 14.0375C8.05 14.2125 8.4 14.375 8.75 14.525C9.1 14.675 9.51667 14.75 10 14.75C10.4833 14.75 10.9 14.675 11.25 14.525C11.6 14.375 11.95 14.2125 12.3 14.0375C12.65 13.8625 13.0333 13.7042 13.45 13.5625C13.8667 13.4208 14.3875 13.35 15.0125 13.35C15.6375 13.35 16.1583 13.4208 16.575 13.5625C16.9917 13.7042 17.375 13.8625 17.725 14.0375C18.075 14.2125 18.425 14.375 18.775 14.525C19.125 14.675 19.5333 14.75 20 14.75V16.7C19.3667 16.7 18.8375 16.625 18.4125 16.475C17.9875 16.325 17.6 16.1625 17.25 15.9875C16.9 15.8125 16.5583 15.6542 16.225 15.5125C15.8917 15.3708 15.4833 15.3 15 15.3C14.5333 15.3 14.1292 15.3708 13.7875 15.5125C13.4458 15.6542 13.1042 15.8125 12.7625 15.9875C12.4208 16.1625 12.0375 16.325 11.6125 16.475C11.1875 16.625 10.65 16.7 10 16.7C9.35 16.7 8.8125 16.625 8.3875 16.475C7.9625 16.325 7.57917 16.1625 7.2375 15.9875C6.89583 15.8125 6.55833 15.6542 6.225 15.5125C5.89167 15.3708 5.4875 15.3 5.0125 15.3C4.5375 15.3 4.12917 15.3708 3.7875 15.5125C3.44583 15.6542 3.1 15.8125 2.75 15.9875C2.4 16.1625 2.0125 16.325 1.5875 16.475C1.1625 16.625 0.633333 16.7 0 16.7ZM0 12.25V10.3C0.483333 10.3 0.895833 10.225 1.2375 10.075C1.57917 9.925 1.925 9.7625 2.275 9.5875C2.625 9.4125 3.0125 9.25417 3.4375 9.1125C3.8625 8.97083 4.3875 8.9 5.0125 8.9C5.6375 8.9 6.15417 8.97083 6.5625 9.1125C6.97083 9.25417 7.35 9.4125 7.7 9.5875C8.05 9.7625 8.4 9.925 8.75 10.075C9.1 10.225 9.51667 10.3 10 10.3C10.4833 10.3 10.9 10.225 11.25 10.075C11.6 9.925 11.95 9.7625 12.3 9.5875C12.65 9.4125 13.0333 9.25417 13.45 9.1125C13.8667 8.97083 14.3833 8.9 15 8.9C15.6333 8.9 16.1583 8.97083 16.575 9.1125C16.9917 9.25417 17.375 9.4125 17.725 9.5875C18.075 9.7625 18.425 9.925 18.775 10.075C19.125 10.225 19.5333 10.3 20 10.3V12.25C19.3667 12.25 18.8375 12.175 18.4125 12.025C17.9875 11.875 17.6 11.7125 17.25 11.5375C16.9 11.3625 16.5583 11.2042 16.225 11.0625C15.8917 10.9208 15.4833 10.85 15 10.85C14.5167 10.85 14.1042 10.9208 13.7625 11.0625C13.4458 11.2042 13.0792 11.3625 12.7375 11.5375C12.3958 11.7125 12.0167 11.875 11.6 12.025C11.1833 12.175 10.65 12.25 10 12.25C9.35 12.25 8.8125 12.175 8.3875 12.025C7.9625 11.875 7.57917 11.7125 7.2375 11.5375C6.89583 11.3625 6.55833 11.2042 6.225 11.0625C5.89167 10.9208 5.4875 10.85 5.0125 10.85C4.5375 10.85 4.12917 10.9208 3.7875 11.0625C3.44583 11.2042 3.1 11.3625 2.75 11.5375C2.4 11.7125 2.0125 11.875 1.5875 12.025C1.1625 12.175 0.633333 12.25 0 12.25ZM0 7.8V5.85C0.483333 5.85 0.895833 5.775 1.2375 5.625C1.57917 5.475 1.925 5.3125 2.275 5.1375C2.625 4.9625 3.0125 4.80417 3.4375 4.6625C3.8625 4.52083 4.3875 4.45 5.0125 4.45C5.6375 4.45 6.15417 4.52083 6.5625 4.6625C6.97083 4.80417 7.35 4.9625 7.7 5.1375C8.05 5.3125 8.4 5.475 8.75 5.625C9.1 5.775 9.51667 5.85 10 5.85C10.4833 5.85 10.9 5.775 11.25 5.625C11.6 5.475 11.95 5.3125 12.3 5.1375C12.65 4.9625 13.0333 4.80417 13.45 4.6625C13.8667 4.52083 14.3833 4.45 15 4.45C15.6333 4.45 16.1583 4.52083 16.575 4.6625C16.9917 4.80417 17.375 4.9625 17.725 5.1375C18.075 5.3125 18.425 5.475 18.775 5.625C19.125 5.775 19.5333 5.85 20 5.85V7.8C19.3667 7.8 18.8375 7.725 18.4125 7.575C17.9875 7.425 17.6 7.2625 17.25 7.0875C16.9 6.9125 16.5583 6.75417 16.225 6.6125C15.8917 6.47083 15.4833 6.4 15 6.4C14.5333 6.4 14.1292 6.47083 13.7875 6.6125C13.4458 6.75417 13.1042 6.9125 12.7625 7.0875C12.4208 7.2625 12.0375 7.425 11.6125 7.575C11.1875 7.725 10.65 7.8 10 7.8C9.35 7.8 8.8125 7.725 8.3875 7.575C7.9625 7.425 7.57917 7.2625 7.2375 7.0875C6.89583 6.9125 6.55833 6.75417 6.225 6.6125C5.89167 6.47083 5.4875 6.4 5.0125 6.4C4.5375 6.4 4.12917 6.47083 3.7875 6.6125C3.44583 6.75417 3.1 6.9125 2.75 7.0875C2.4 7.2625 2.0125 7.425 1.5875 7.575C1.1625 7.725 0.633333 7.8 0 7.8ZM0 3.35V1.4C0.483333 1.4 0.895833 1.325 1.2375 1.175C1.57917 1.025 1.925 0.8625 2.275 0.6875C2.625 0.5125 3.0125 0.354167 3.4375 0.2125C3.8625 0.0708333 4.3875 0 5.0125 0C5.6375 0 6.15417 0.0708333 6.5625 0.2125C6.97083 0.354167 7.35 0.5125 7.7 0.6875C8.05 0.8625 8.4 1.025 8.75 1.175C9.1 1.325 9.51667 1.4 10 1.4C10.4833 1.4 10.9 1.325 11.25 1.175C11.6 1.025 11.95 0.8625 12.3 0.6875C12.65 0.5125 13.0333 0.354167 13.45 0.2125C13.8667 0.0708333 14.3833 0 15 0C15.6333 0 16.1583 0.0708333 16.575 0.2125C16.9917 0.354167 17.375 0.5125 17.725 0.6875C18.075 0.8625 18.425 1.025 18.775 1.175C19.125 1.325 19.5333 1.4 20 1.4V3.35C19.3667 3.35 18.8375 3.275 18.4125 3.125C17.9875 2.975 17.6 2.8125 17.25 2.6375C16.9 2.4625 16.5583 2.30417 16.225 2.1625C15.8917 2.02083 15.4833 1.95 15 1.95C14.5333 1.95 14.1292 2.02083 13.7875 2.1625C13.4458 2.30417 13.1042 2.4625 12.7625 2.6375C12.4208 2.8125 12.0375 2.975 11.6125 3.125C11.1875 3.275 10.65 3.35 10 3.35C9.35 3.35 8.8125 3.275 8.3875 3.125C7.9625 2.975 7.57917 2.8125 7.2375 2.6375C6.89583 2.4625 6.55833 2.30417 6.225 2.1625C5.89167 2.02083 5.4875 1.95 5.0125 1.95C4.5375 1.95 4.12917 2.02083 3.7875 2.1625C3.44583 2.30417 3.1 2.4625 2.75 2.6375C2.4 2.8125 2.0125 2.975 1.5875 3.125C1.1625 3.275 0.633333 3.35 0 3.35Z" fill="#006692"/>
    </svg>
    <h1>AmbaFeeder</h1>
    <div class="status-chip" style="margin-left: auto; margin-bottom: 0;">
      <div class="status-dot" id="status-dot"></div>
      <span id="status-text">Standby</span>
      &nbsp;·&nbsp;
      <span id="rtc-time">--:--:--</span>
    </div>
  </div>

  <div class="sub-header">
    <p>MANUAL</p>
    <h4>Pakan Manual</h4>
  </div>
  <div class="manual">
    <label for="manual-grams">Jumlah Pakan (Gram)</label>
    <span>
      <svg xmlns="http://www.w3.org/2000/svg" width="18" height="21" viewBox="0 0 18 21" fill="none">
        <path d="M3 20V10.85C2.15 10.6167 1.4375 10.15 0.8625 9.45C0.2875 8.75 0 7.93333 0 7V0H2V7H3V0H5V7H6V0H8V7C8 7.93333 7.7125 8.75 7.1375 9.45C6.5625 10.15 5.85 10.6167 5 10.85V20H3ZM13 20V12H10V5C10 3.61667 10.4875 2.4375 11.4625 1.4625C12.4375 0.4875 13.6167 0 15 0V20H13Z" fill="#004D6F" fill-opacity="0.6"/>
      </svg>
      <input type="number" id="manual-grams" value="5" min="1" max="50" placeholder="Masukkan gram...">
    </span>
    <button id="feed-btn" onclick="feedManual()">
      <svg xmlns="http://www.w3.org/2000/svg" width="15" height="20" viewBox="0 0 15 20" fill="none">
        <path d="M3 20V10.85C2.15 10.6167 1.4375 10.15 0.8625 9.45C0.2875 8.75 0 7.93333 0 7V0H2V7H3V0H5V7H6V0H8V7C8 7.93333 7.7125 8.75 7.1375 9.45C6.5625 10.15 5.85 10.6167 5 10.85V20H3ZM13 20V12H10V5C10 3.61667 10.4875 2.4375 11.4625 1.4625C12.4375 0.4875 13.6167 0 15 0V20H13Z" fill="white"/>
      </svg>
      Pakan Sekarang
    </button>
  </div>

  <div class="sub-header-otomatis">
    <div class="sub-header">
      <p>OTOMATIS</p>
      <h4>Jadwal Otomatis</h4>
    </div>
    <button onclick="openModal()">
      <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" viewBox="0 0 14 14" fill="none">
        <path d="M6 8H0V6H6V0H8V6H14V8H8V14H6V8Z" fill="#004C6E"/>
      </svg>
    </button>
  </div>
  <div id="schedule-list"></div>

  <div class="sub-header" style="margin-top: 28px;">
    <p>AKTIVITAS</p>
    <h4>Histori Pakan</h4>
  </div>
  <div class="timeline" id="history-timeline"></div>

  <div class="modal-overlay" id="modal-overlay" onclick="handleOverlayClick(event)">
    <div class="modal">
      <h3>Tambah Jadwal</h3>
      <label>Waktu</label>
      <input type="time" id="sched-time">
      <label>Jumlah Pakan (Gram)</label>
      <input type="number" id="sched-grams" placeholder="Contoh: 5" min="1" max="50">
      <div class="modal-actions">
        <button class="btn-cancel" onclick="closeModal()">Batal</button>
        <button class="btn-save" onclick="addSchedule()">Simpan</button>
      </div>
    </div>
  </div>

  <script>
    // ── MODAL ──
    function openModal() {
      document.getElementById('modal-overlay').classList.add('open');
    }
    function closeModal() {
      document.getElementById('modal-overlay').classList.remove('open');
      document.getElementById('sched-time').value = '';
      document.getElementById('sched-grams').value = '';
    }
    function handleOverlayClick(e) {
      if (e.target === document.getElementById('modal-overlay')) closeModal();
    }

    // ── STATUS POLLING ──
    async function fetchStatus() {
      try {
        let res = await fetch('/api/status');
        let data = await res.json();
        document.getElementById('rtc-time').innerText = data.time;
        const dot = document.getElementById('status-dot');
        const txt = document.getElementById('status-text');
        const btn = document.getElementById('feed-btn');
        if (data.isFeeding) {
          dot.className = 'status-dot feeding';
          txt.innerText = 'Feeding...';
          btn.disabled = true;
        } else {
          dot.className = 'status-dot';
          txt.innerText = 'Standby';
          btn.disabled = false;
        }
      } catch (e) { console.error(e); }
    }

    // ── LOAD DATA ──
    async function loadData() {
      try {
        let res = await fetch('/api/data');
        let data = await res.json();
        renderSchedules(data.schedules);
        renderHistory(data.history);
      } catch (e) { console.error(e); }
    }

    function renderSchedules(schedules) {
      const el = document.getElementById('schedule-list');
      if (!schedules || schedules.length === 0) {
        el.innerHTML = '<div class="empty-state">Belum ada jadwal. Tap + untuk menambah.</div>';
        return;
      }
      el.innerHTML = schedules.map((s, idx) => {
        let h = String(s.hour).padStart(2, '0');
        let m = String(s.minute).padStart(2, '0');
        let ampm = s.hour >= 12 ? 'PM' : 'AM';
        let h12 = s.hour % 12 || 12;
        return `<div class="schedule">
          <div class="kiri">
            <div class="icon">
              <svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="0 0 22 22" fill="none">
                <path d="M10 3V0H12V3H10ZM10 22V19H12V22H10ZM19 12V10H22V12H19ZM0 12V10H3V12H0ZM17.7 5.7L16.3 4.3L18.05 2.5L19.5 3.95L17.7 5.7ZM3.95 19.5L2.5 18.05L4.3 16.3L5.7 17.7L3.95 19.5ZM18.05 19.5L16.3 17.7L17.7 16.3L19.5 18.05L18.05 19.5ZM4.3 5.7L2.5 3.95L3.95 2.5L5.7 4.3L4.3 5.7ZM11 17C9.33333 17 7.91667 16.4167 6.75 15.25C5.58333 14.0833 5 12.6667 5 11C5 9.33333 5.58333 7.91667 6.75 6.75C7.91667 5.58333 9.33333 5 11 5C12.6667 5 14.0833 5.58333 15.25 6.75C16.4167 7.91667 17 9.33333 17 11C17 12.6667 16.4167 14.0833 15.25 15.25C14.0833 16.4167 12.6667 17 11 17Z" fill="#2E6D5F"/>
              </svg>
            </div>
            <div class="content">
              <p>${String(h12).padStart(2,'0')}:${m} ${ampm}</p>
              <p>${s.grams} gram pakan</p>
            </div>
          </div>
          <div class="kanan">
            <button onclick="deleteSchedule(${idx})" title="Hapus jadwal">
              <svg xmlns="http://www.w3.org/2000/svg" width="16" height="18" viewBox="0 0 16 18" fill="none">
                <path d="M3 18C2.45 18 1.97917 17.8042 1.5875 17.4125C1.19583 17.0208 1 16.55 1 16V3H0V1H5V0H11V1H16V3H15V16C15 16.55 14.8042 17.0208 14.4125 17.4125C14.0208 17.8042 13.55 18 13 18H3ZM13 3H3V16H13V3ZM5 14H7V5H5V14ZM9 14H11V5H9V14ZM3 3V16V3Z" fill="#d32f2f"/>
              </svg>
            </button>
          </div>
        </div>`;
      }).join('');
    }

    function renderHistory(historyData) {
      const el = document.getElementById('history-timeline');
      if (!historyData || historyData.length === 0) {
        el.innerHTML = '<div class="empty-state">Belum ada riwayat pemberian pakan.</div>';
        return;
      }
      el.innerHTML = historyData.map((h, idx) => `
        <div class="tl-item ${idx === 0 ? 'active' : ''}">
          <div class="tl-dot"></div>
          <div class="tl-card">
            <div>
              <p class="tl-time">${h.timestamp}</p>
              <p class="tl-desc">${h.grams} gram pakan</p>
            </div>
            <span class="badge ${h.type === 'Manual' ? 'manual' : 'auto'}">${h.type === 'Manual' ? 'MANUAL' : 'AUTO'}</span>
          </div>
        </div>
      `).join('');
    }

    // ── ACTIONS ──
    async function feedManual() {
      let g = parseInt(document.getElementById('manual-grams').value);
      if (!g || g < 1) return alert('Masukkan jumlah gram yang valid!');
      await fetch('/api/feed', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ grams: g })
      });
      loadData();
    }

    async function addSchedule() {
      let timeVal = document.getElementById('sched-time').value;
      let grams = document.getElementById('sched-grams').value;
      if (!timeVal || !grams) return alert('Lengkapi data jadwal!');
      let [h, m] = timeVal.split(':');
      await fetch('/api/schedule', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ hour: parseInt(h), minute: parseInt(m), grams: parseInt(grams) })
      });
      closeModal();
      loadData();
    }

    async function deleteSchedule(idx) {
      if (!confirm('Hapus jadwal ini?')) return;
      await fetch('/api/schedule?id=' + idx, { method: 'DELETE' });
      loadData();
    }

    async function syncTime() {
      try {
        const now = Math.floor(Date.now() / 1000); // UNIX time (detik)

        await fetch('/api/sync-time', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ epoch: now })
        });

      } catch (e) {
        console.error("Sync time gagal", e);
      }
    }

    // ── INIT ──
    setInterval(fetchStatus, 2000);
    setInterval(loadData, 10000);
    window.onload = () => { fetchStatus(); loadData(); syncTime();};
  </script>
</body>
</html>
)rawliteral";
}

// NVS Helper
void saveSchedules() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < scheduleCount; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["h"] = schedules[i].hour;
    obj["m"] = schedules[i].minute;
    obj["g"] = schedules[i].grams;
    obj["a"] = schedules[i].active;
  }
  String jsonStr;
  serializeJson(doc, jsonStr);
  preferences.putString("scheds", jsonStr);
}

void saveHistory() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  int startIdx = (historyIndex - historyCount + MAX_HISTORY) % MAX_HISTORY;

  for (int i = 0; i < historyCount; i++) {
    int idx = (startIdx + i) % MAX_HISTORY;
    JsonObject obj = arr.add<JsonObject>();
    obj["t"] = history[idx].timestamp;
    obj["g"] = history[idx].grams;
    obj["ty"] = history[idx].type;
  }

  String jsonStr;
  serializeJson(doc, jsonStr);
  preferences.putString("history", jsonStr);
}

void loadSchedules() {
  String jsonStr = preferences.getString("scheds", "[]");
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonStr);
  
  if (!error) {
    JsonArray arr = doc.as<JsonArray>();
    scheduleCount = 0;
    for (JsonObject obj : arr) {
      if (scheduleCount >= MAX_SCHEDULES) break;
      schedules[scheduleCount].hour = obj["h"];
      schedules[scheduleCount].minute = obj["m"];
      schedules[scheduleCount].grams = obj["g"];
      schedules[scheduleCount].active = obj["a"];
      scheduleCount++;
    }
  }
}

void loadHistory() {
  String jsonStr = preferences.getString("history", "[]");

  JsonDocument doc;
  if (deserializeJson(doc, jsonStr)) return;

  JsonArray arr = doc.as<JsonArray>();

  historyCount = 0;
  historyIndex = 0;

  for (JsonObject obj : arr) {
    if (historyCount >= MAX_HISTORY) break;

    history[historyCount].timestamp = (const char*)obj["t"];
    history[historyCount].grams = obj["g"];
    history[historyCount].type = (const char*)obj["ty"];

    historyCount++;
  }

  historyIndex = historyCount % MAX_HISTORY;
}

void addHistoryLog(int grams, String type) {
  DateTime now = rtc.now();
  char timeBuf[20];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d %02d/%02d", now.hour(), now.minute(), now.second(), now.day(), now.month());
  
  history[historyIndex].timestamp = String(timeBuf);
  history[historyIndex].grams = grams;
  history[historyIndex].type = type;
  
  historyIndex = (historyIndex + 1) % MAX_HISTORY;
  if(historyCount < MAX_HISTORY) historyCount++;

  saveHistory();
}

void executeFeed(int grams, String type) {
  if (isFeeding) return;
  currentFeedDuration = grams * TIME_PER_GRAM_MS;

  // 🔥 1. Simpan durasi ke NVS Preferences tepat sebelum Servo bergerak
  preferences.putULong("rem_dur", currentFeedDuration);

  targetServoAngle = SERVO_OPEN_ANGLE;
  feedStartTime = millis();
  isFeeding = true;
  addHistoryLog(grams, type);
  Serial.printf("Feeding %d grams via %s\n", grams, type.c_str());
}

void updateServo() {
  if (currentServoAngle != targetServoAngle) {
    if (millis() - lastServoMoveTime >= SERVO_DELAY_MS) {
      if (currentServoAngle < targetServoAngle) {
        currentServoAngle++;
      } else {
        currentServoAngle--;
      }
      feederServo.write(currentServoAngle);
      lastServoMoveTime = millis();
    }
  }
}

void checkScheduledFeeds() {
  static int lastCheckedMinute = -1;
  DateTime now = rtc.now();
  
  if (now.minute() != lastCheckedMinute) {
    lastCheckedMinute = now.minute();
    for (int i = 0; i < scheduleCount; i++) {
      if (schedules[i].active && schedules[i].hour == now.hour() && schedules[i].minute == now.minute()) {
        executeFeed(schedules[i].grams, "Scheduled");
      }
    }
  }
}

// Setup Endpoint API
void setupRouting() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", getHTML());
  });

  server.on("/api/status", HTTP_GET, []() {
    DateTime now = rtc.now();
    char timeBuf[10];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    
    JsonDocument doc;
    doc["time"] = String(timeBuf);
    doc["isFeeding"] = isFeeding;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.on("/api/data", HTTP_GET, []() {
    JsonDocument doc;
    JsonArray schedArr = doc["schedules"].to<JsonArray>();
    for (int i = 0; i < scheduleCount; i++) {
      JsonObject obj = schedArr.add<JsonObject>();
      obj["hour"] = schedules[i].hour;
      obj["minute"] = schedules[i].minute;
      obj["grams"] = schedules[i].grams;
      obj["active"] = schedules[i].active;
    }
    
    JsonArray histArr = doc["history"].to<JsonArray>();
    int startIdx = (historyIndex - historyCount + MAX_HISTORY) % MAX_HISTORY;
    for (int i = 0; i < historyCount; i++) {
      int idx = (historyIndex - 1 - i + MAX_HISTORY) % MAX_HISTORY;
      JsonObject obj = histArr.add<JsonObject>();
      obj["timestamp"] = history[idx].timestamp;
      obj["grams"] = history[idx].grams;
      obj["type"] = history[idx].type;
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.on("/api/feed", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      JsonDocument doc;
      deserializeJson(doc, server.arg("plain"));
      int grams = doc["grams"] | 5;
      executeFeed(grams, "Manual");
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.send(400, "application/json", "{\"status\":\"error\"}");
    }
  });

  server.on("/api/schedule", HTTP_POST, []() {
    if (server.hasArg("plain") && scheduleCount < MAX_SCHEDULES) {
      JsonDocument doc;
      deserializeJson(doc, server.arg("plain"));
      schedules[scheduleCount] = {scheduleCount, doc["hour"], doc["minute"], doc["grams"], true};
      scheduleCount++;
      saveSchedules();
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.send(400, "application/json", "{\"status\":\"full or error\"}");
    }
  });

  server.on("/api/schedule", HTTP_DELETE, []() {
    if (server.hasArg("id")) {
      int idToRemove = server.arg("id").toInt();
      if (idToRemove >= 0 && idToRemove < scheduleCount) {
        for (int i = idToRemove; i < scheduleCount - 1; i++) {
          schedules[i] = schedules[i + 1];
        }
        scheduleCount--;
        saveSchedules();
        server.send(200, "application/json", "{\"status\":\"deleted\"}");
      }
    }
  });

  server.on("/api/sync-time", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"status\":\"no data\"}");
      return;
    }

    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));

    long clientEpoch = doc["epoch"];

    // 🔥 TAMBAHKAN OFFSET WIB (UTC+7)
    clientEpoch += 7 * 3600;

    DateTime rtcNow = rtc.now();
    long rtcEpoch = rtcNow.unixtime();

    long diff = abs(clientEpoch - rtcEpoch);

    if (diff > 10) {
      rtc.adjust(DateTime(clientEpoch));
      Serial.println("RTC disinkronkan ke WIB!");
      server.send(200, "application/json", "{\"status\":\"synced\"}");
    } else {
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    }
  });

  server.onNotFound([]() {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  });
}

void setup() {
  Serial.begin(115200);

  preferences.begin("feeder", false);
  loadSchedules();
  loadHistory();

  ESP32PWM::allocateTimer(0);
  feederServo.setPeriodHertz(50);
  feederServo.attach(SERVO_PIN, 500, 2400);

  // 🔥 2. Cek apakah ada sisa durasi feeding akibat crash sebelumnya
  unsigned long crashRecoveryDuration = preferences.getULong("rem_dur", 0);
  if (crashRecoveryDuration > 0) {
    Serial.printf("Mendeteksi crash! Melanjutkan feeding selama %lu ms\n", crashRecoveryDuration);
    
    // Langsung hapus (nol-kan) data NVS untuk mencegah "Infinite Boot Loop" jika crash terjadi lagi
    preferences.putULong("rem_dur", 0); 

    targetServoAngle = SERVO_OPEN_ANGLE;
    feedStartTime = millis();
    currentFeedDuration = crashRecoveryDuration;
    isFeeding = true;
  } else {
    // Jika boot normal tanpa crash, servo dipastikan tertutup
    feederServo.write(SERVO_CLOSED_ANGLE);
    currentServoAngle = SERVO_CLOSED_ANGLE;
    targetServoAngle = SERVO_CLOSED_ANGLE;
  }

  Wire.begin();
  if (!rtc.begin()) {
    Serial.println("RTC DS1307 tidak terdeteksi!");
  } else if (!rtc.isrunning()) {
    Serial.println("RTC tidak berjalan, melakukan inisialisasi waktu kompilasi...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, passphrase);

  if (!MDNS.begin(hostname)) {
    Serial.println("Kesalahan saat mengatur mDNS!");
  } else {
    MDNS.addService("http", "tcp", 80);
  }

  dnsServer.start(DNS_PORT, "*", apIP);
  setupRouting();
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  checkScheduledFeeds();
  updateServo();

  if (isFeeding && (millis() - feedStartTime >= currentFeedDuration)) {
    targetServoAngle = SERVO_CLOSED_ANGLE;
    isFeeding = false;
    
    // 🔥 3. Pastikan data di Preferences dinolkan setelah feeding sukses selesai
    preferences.putULong("rem_dur", 0); 
    Serial.println("Feed completed. Servo closing.");
  }
}
