#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <map>
#define ESPNOW_CHANNEL 1
// ================= MENSAJES =================
typedef struct __attribute__((packed)) {
  uint8_t msgType;  // 1=data, 2=cfg_request, 3=cfg_response

  union {
    struct {
      char name[25];
      float temperature;
      float battery;  // <-- % (0..100)
      uint8_t mac[6];
      uint32_t uptime_ms;
    } data;

    struct {
      char name[25];
      uint8_t mac[6];
    } cfg_req;

    struct {
      char target[25];
      char newName[25];
      float tempOffset;
      uint32_t sleepSec;
      bool apply;
    } cfg_rsp;
  };
} espnow_msg_t;

// ================= BASE DE DATOS =================
struct SensorInfo {
  String macStr;
  String reported_name;  // lo que el nodo reporta
  String cfg_name;       // nombre deseado (desde PC)
  float offset;
  uint32_t sleep;
  bool pending;
  uint8_t mac[6];
  unsigned long lastSeen;

  // batería
  float lastBatPct = NAN;
  unsigned long lastBatSeen = 0;
};

std::map<String, SensorInfo> sensors;

// ================= UTILIDADES =================
static void printMasterMac() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.printf("MASTER_MAC|%02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

String macToString(const uint8_t *mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

void printNewSensor(const String &mac, const String &name) {
  Serial.print("NUEVO_SENSOR|");
  Serial.print(mac);
  Serial.print("|");
  Serial.println(name);
}

// Formato para tu Processing:
//   DATOS|MAC|TEMP|xx.x
//   DATOS|MAC|BAT|yy
static void printDataToPC(const String &macStr,
                          const String &name,
                          float tempC,
                          float batPct,
                          uint32_t uptimeMs) {
  (void)name;
  (void)uptimeMs;

  // TEMP a 1 decimal (como ya dejaste tu UI)
  float t1 = roundf(tempC * 10.0f) / 10.0f;
  Serial.print("DATOS|");
  Serial.print(macStr);
  Serial.print("|TEMP|");
  Serial.println(t1, 1);

  // BAT sin decimales (0..100)
  float b = constrain(batPct, 0.0f, 100.0f);
  Serial.print("DATOS|");
  Serial.print(macStr);
  Serial.print("|BAT|");
  Serial.println(b, 0);
}

// ================= CALLBACK ESP-NOW =================
void onDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  if (len != (int)sizeof(espnow_msg_t)) return;

  espnow_msg_t msg;
  memcpy(&msg, data, sizeof(msg));

  const uint8_t *mac = info->src_addr;
  String macStr = macToString(mac);

  // asegurar peer
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }

  // ===== DATA =====
  if (msg.msgType == 1) {
    if (!sensors.count(macStr)) {
      SensorInfo s;
      s.macStr = macStr;
      s.reported_name = msg.data.name;
      s.cfg_name = msg.data.name;
      s.offset = 0.0f;
      s.sleep = 60;
      s.pending = false;
      memcpy(s.mac, mac, 6);
      s.lastSeen = millis();
      sensors[macStr] = s;
      printNewSensor(macStr, s.reported_name);
    }

    SensorInfo &s = sensors[macStr];
    s.lastSeen = millis();
    s.reported_name = msg.data.name;

    // guarda batería
    s.lastBatPct = constrain(msg.data.battery, 0.0f, 100.0f);
    s.lastBatSeen = millis();

    // manda al PC (Processing)
    printDataToPC(macStr, s.reported_name, msg.data.temperature, s.lastBatPct, msg.data.uptime_ms);

    // Si hay pending, manda config UNA VEZ cuando el nodo envía data
    if (s.pending) {
      espnow_msg_t rsp = {};
      rsp.msgType = 3;

      snprintf(rsp.cfg_rsp.target, sizeof(rsp.cfg_rsp.target), "MAC:%s", macStr.c_str());
      strncpy(rsp.cfg_rsp.newName, s.cfg_name.c_str(), sizeof(rsp.cfg_rsp.newName) - 1);
      rsp.cfg_rsp.tempOffset = s.offset;
      rsp.cfg_rsp.sleepSec = s.sleep;
      rsp.cfg_rsp.apply = true;

      esp_now_send(mac, (uint8_t *)&rsp, sizeof(rsp));
      s.pending = false;
    }
    return;
  }

  // ===== REQUEST CONFIG =====
  if (msg.msgType == 2) {
    espnow_msg_t rsp = {};
    rsp.msgType = 3;

    if (sensors.count(macStr)) {
      SensorInfo &s = sensors[macStr];
      snprintf(rsp.cfg_rsp.target, sizeof(rsp.cfg_rsp.target), "MAC:%s", macStr.c_str());
      rsp.cfg_rsp.tempOffset = s.offset;
      rsp.cfg_rsp.sleepSec = s.sleep;

      if (s.pending) {
        strncpy(rsp.cfg_rsp.newName, s.cfg_name.c_str(), sizeof(rsp.cfg_rsp.newName) - 1);
        rsp.cfg_rsp.apply = true;
        s.pending = false;
      } else {
        rsp.cfg_rsp.newName[0] = 0;
        rsp.cfg_rsp.apply = false;
      }
    } else {
      strncpy(rsp.cfg_rsp.target, msg.cfg_req.name, sizeof(rsp.cfg_rsp.target) - 1);
      rsp.cfg_rsp.tempOffset = 0;
      rsp.cfg_rsp.sleepSec = 60;
      rsp.cfg_rsp.newName[0] = 0;
      rsp.cfg_rsp.apply = false;
    }

    esp_now_send(mac, (uint8_t *)&rsp, sizeof(rsp));
    return;
  }
}

// ================= COMANDOS PC =================
void processCommand(String cmd) {
  if (cmd == "PING") {
    Serial.println("PONG");
    return;
  }

  if (cmd == "GET_SENSORS") {
    for (auto &p : sensors) {
      Serial.print("SENSOR|");
      Serial.print(p.first);
      Serial.print("|");
      Serial.print(p.second.reported_name);
      Serial.print("|");
      Serial.print(p.second.offset);
      Serial.print("|");
      Serial.print(p.second.sleep);
      Serial.print("|");
      Serial.print(p.second.cfg_name);
      Serial.print("|");
      Serial.println(isnan(p.second.lastBatPct) ? -1 : p.second.lastBatPct, 0);
    }
    return;
  }

  if (cmd.startsWith("SET_CONFIG|")) {
    // SET_CONFIG|MAC|OFFSET|SLEEP|NEWNAME
    String parts = cmd.substring(11);
    int p1 = parts.indexOf('|');
    int p2 = parts.indexOf('|', p1 + 1);
    int p3 = parts.indexOf('|', p2 + 1);
    if (p1 < 0 || p2 < 0 || p3 < 0) {
      Serial.println("CONFIG_ERROR|FORMAT");
      return;
    }

    String mac = parts.substring(0, p1);
    float offset = parts.substring(p1 + 1, p2).toFloat();
    uint32_t sleep = (uint32_t)parts.substring(p2 + 1, p3).toInt();
    String newName = parts.substring(p3 + 1);

    if (sensors.count(mac)) {
      SensorInfo &s = sensors[mac];
      s.offset = offset;
      s.sleep = sleep;
      if (newName.length()) s.cfg_name = newName;
      s.pending = true;
      Serial.println("CONFIG_OK|" + mac);
    } else {
      Serial.println("CONFIG_ERROR|NOT_FOUND");
    }
    return;
  }
}

// ================= SETUP/LOOP =================
void setup() {
  Serial.begin(115200);
  delay(1500);

  WiFi.mode(WIFI_STA);
  delay(100);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  printMasterMac();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP_NOW_ERROR");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  Serial.println("MAESTRO_LISTO");
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length()) processCommand(cmd);
  }
}