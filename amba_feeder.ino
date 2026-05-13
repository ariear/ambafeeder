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
const int SERVO_OPEN_ANGLE = 90;
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

// Deklarasi fungsi front-end (Halaman SPA)
const char* getHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Amba Feeder Panel</title>
  <style>
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f4f6f8; margin: 0; padding: 0; color: #333; }
    .container { max-width: 800px; margin: 20px auto; padding: 20px; }
    .tabs { display: flex; border-bottom: 2px solid #ddd; margin-bottom: 20px; }
    .tab { padding: 10px 20px; cursor: pointer; font-weight: bold; color: #666; border-bottom: 3px solid transparent; transition: 0.3s; }
    .tab.active { border-bottom-color: #1976d2; color: #1976d2; }
    .content { display: none; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
    .content.active { display: block; }
    .card { background: #f9f9f9; padding: 15px; border-radius: 8px; margin-bottom: 15px; border: 1px solid #eee; }
    input, button { padding: 8px 12px; margin: 5px 0; border: 1px solid #ccc; border-radius: 4px; }
    button { background-color: #1976d2; color: white; border: none; cursor: pointer; font-weight: bold; }
    button:hover { background-color: #115293; }
    .btn-danger { background-color: #d32f2f; }
    .btn-danger:hover { background-color: #9a0007; }
    table { width: 100%; border-collapse: collapse; margin-top: 10px; }
    th, td { border: 1px solid #ddd; padding: 10px; text-align: left; }
    th { background-color: #f1f1f1; }
  </style>
</head>
<body>
  <div class="container">
    <h2 style="color: #1976d2; text-align: center;">Amba Feeder Control Panel</h2>
    
    <div class="tabs">
      <div class="tab active" onclick="switchTab('dashboard')">Dashboard</div>
      <div class="tab" onclick="switchTab('feed')">Schedule & Feed</div>
    </div>

    <div id="dashboard" class="content active">
      <div class="card">
        <h3>Status Sistem</h3>
        <p>Waktu Perangkat (RTC): <strong id="rtc-time">Loading...</strong></p>
        <p>Status Servo: <strong id="servo-status">Loading...</strong></p>
      </div>
    </div>

    <div id="feed" class="content">
      <div class="card">
        <h3>Manual Feed</h3>
        <label>Jumlah Pakan (Gram):</label>
        <input type="number" id="manual-grams" value="5" min="1" max="50">
        <button onclick="feedManual()">Feed Now</button>
      </div>

      <div class="card">
        <h3>Jadwal Otomatis (Schedules)</h3>
        <div style="display: flex; gap: 10px; align-items: center; margin-bottom: 15px;">
          <input type="time" id="sched-time" required>
          <input type="number" id="sched-grams" placeholder="Gram" min="1" style="width: 80px;" required>
          <button onclick="addSchedule()">Tambah Jadwal</button>
        </div>
        <table>
          <thead>
            <tr>
              <th>Waktu</th>
              <th>Gram</th>
              <th>Status</th>
              <th>Aksi</th>
            </tr>
          </thead>
          <tbody id="schedule-table"></tbody>
        </table>
      </div>

      <div class="card">
        <h3>Riwayat Pemberian Pakan</h3>
        <table>
          <thead>
            <tr>
              <th>Waktu</th>
              <th>Jumlah</th>
              <th>Metode</th>
            </tr>
          </thead>
          <tbody id="history-table"></tbody>
        </table>
      </div>
    </div>
  </div>

  <script>
    function switchTab(tabId) {
      document.querySelectorAll('.content').forEach(el => el.classList.remove('active'));
      document.querySelectorAll('.tab').forEach(el => el.classList.remove('active'));
      document.getElementById(tabId).classList.add('active');
      event.currentTarget.classList.add('active');
      if(tabId === 'feed') loadSchedulesAndHistory();
    }

    async function fetchStatus() {
      try {
        let res = await fetch('/api/status');
        let data = await res.json();
        document.getElementById('rtc-time').innerText = data.time;
        document.getElementById('servo-status').innerText = data.isFeeding ? 'Terbuka (Feeding)' : 'Tertutup (Standby)';
      } catch (e) { console.error(e); }
    }

    async function loadSchedulesAndHistory() {
      try {
        let res = await fetch('/api/data');
        let data = await res.json();
        
        let schedHtml = '';
        data.schedules.forEach((s, idx) => {
          let t = String(s.hour).padStart(2,'0') + ':' + String(s.minute).padStart(2,'0');
          schedHtml += `<tr>
            <td>${t}</td><td>${s.grams}g</td>
            <td>${s.active ? 'Aktif' : 'Nonaktif'}</td>
            <td><button class="btn-danger" onclick="deleteSchedule(${idx})">Hapus</button></td>
          </tr>`;
        });
        document.getElementById('schedule-table').innerHTML = schedHtml;

        let histHtml = '';
        data.history.forEach(h => {
          histHtml += `<tr><td>${h.timestamp}</td><td>${h.grams}g</td><td>${h.type}</td></tr>`;
        });
        document.getElementById('history-table').innerHTML = histHtml;
      } catch (e) { console.error(e); }
    }

    async function feedManual() {
      let g = document.getElementById('manual-grams').value;
      await fetch('/api/feed', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ grams: parseInt(g) })
      });
      alert('Perintah feed dikirim!');
      loadSchedulesAndHistory();
    }

    async function addSchedule() {
      let timeVal = document.getElementById('sched-time').value;
      let grams = document.getElementById('sched-grams').value;
      if(!timeVal || !grams) return alert('Lengkapi data jadwal!');
      
      let [h, m] = timeVal.split(':');
      await fetch('/api/schedule', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ hour: parseInt(h), minute: parseInt(m), grams: parseInt(grams) })
      });
      loadSchedulesAndHistory();
    }

    async function deleteSchedule(idx) {
      await fetch('/api/schedule?id=' + idx, { method: 'DELETE' });
      loadSchedulesAndHistory();
    }

    setInterval(fetchStatus, 2000);
    window.onload = () => { fetchStatus(); loadSchedulesAndHistory(); };
  </script>
</body>
</html>
)rawliteral";
}

// Fungsi Utilitas Logging
void addHistoryLog(int grams, String type) {
  DateTime now = rtc.now();
  char timeBuf[20];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d %02d/%02d", now.hour(), now.minute(), now.second(), now.day(), now.month());
  
  history[historyIndex].timestamp = String(timeBuf);
  history[historyIndex].grams = grams;
  history[historyIndex].type = type;
  
  historyIndex = (historyIndex + 1) % MAX_HISTORY;
  if(historyCount < MAX_HISTORY) historyCount++;
}

// Eksekusi Pakan (Non-Blocking Wrapper)
void executeFeed(int grams, String type) {
  if (isFeeding) return; // Cegah tumpang tindih perintah
  currentFeedDuration = grams * TIME_PER_GRAM_MS;
  feederServo.write(SERVO_OPEN_ANGLE);
  feedStartTime = millis();
  isFeeding = true;
  addHistoryLog(grams, type);
  Serial.printf("Feeding %d grams via %s\n", grams, type.c_str());
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

// Cek Jadwal Rutin
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
      int idx = (startIdx + i) % MAX_HISTORY;
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

  server.onNotFound([]() {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  });
}

void setup() {
  Serial.begin(115200);

  // Inisialisasi NVS dan Load Data
  preferences.begin("feeder", false);
  loadSchedules();

  // Inisialisasi Servo
  ESP32PWM::allocateTimer(0);
  feederServo.setPeriodHertz(50);
  feederServo.attach(SERVO_PIN, 500, 2400);
  feederServo.write(SERVO_CLOSED_ANGLE);

  // Inisialisasi I2C & RTC
  Wire.begin();
  if (!rtc.begin()) {
    Serial.println("RTC DS1307 tidak terdeteksi!");
  } else if (!rtc.isrunning()) {
    Serial.println("RTC tidak berjalan, melakukan inisialisasi waktu kompilasi...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Inisialisasi Jaringan SoftAP
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

  // Evaluasi jadwal dari RTC
  checkScheduledFeeds();

  // Manajemen State Servo (Non-Blocking)
  if (isFeeding && (millis() - feedStartTime >= currentFeedDuration)) {
    feederServo.write(SERVO_CLOSED_ANGLE);
    isFeeding = false;
    Serial.println("Feed completed. Servo closed.");
  }
}