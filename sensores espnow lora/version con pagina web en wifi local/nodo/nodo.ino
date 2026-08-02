#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <SPI.h>
#include <Adafruit_MAX31865.h>
#include <ArduinoJson.h>
#include <math.h>

// ================= CONFIG WIFI =================
const char* WIFI_SSID = "elyisus";
const char* WIFI_PASS = "4j6vx7gm:DEDOS";

// ================= MAESTRO =================
const char* MASTER_HOST = "192.168.0.33";
const uint16_t MASTER_PORT = 80;

// ================= RED DEL NODO =================
// Para varios nodos, es más seguro DHCP.
// Si realmente quieres IP fija por nodo, activa USE_STATIC_IP y cambia NODE_LOCAL_IP.
static const bool USE_STATIC_IP = true;
IPAddress NODE_LOCAL_IP(192, 168, 0, 37);
IPAddress NODE_GATEWAY(192, 168, 0, 1);
IPAddress NODE_SUBNET(255, 255, 255, 0);

// ================= CONFIGURACIÓN POR DEFECTO =================
#define DEFAULT_SENSOR_NAME "SENSOR_1"
#define DEFAULT_SLEEP_TIME 60
#define DEFAULT_TEMP_OFFSET 0.0f

// ================= PINES MAX31865 (SPI por software) =================



#define PIN_SCK  16 //scl
#define PIN_MISO 23 // sdo
#define PIN_MOSI 22 //sdi
#define PIN_CS   21


// ================= PIN ACTIVAR SENSOR =================
#define PIN_SENSOR_PWR 2

// ================= LECTURA BATERÍA (ADC + DIVISOR) =================
#define BAT_ADC_PIN 0

// Divisor: VBAT -- R1 --(ADC)-- R2 -- GND
#define BAT_R1_OHM 344800.0f
#define BAT_R2_OHM 994300.0f

// Calibración fina
#define BAT_CAL_K 1.000f
#define BAT_CAL_B 0.090f

// Rango típico Li-ion 1S para %
#define BAT_VMIN 3.30f
#define BAT_VMAX 4.10f

// Muestras para promedio
#define BAT_SAMPLES 3

// ================= CONFIG PT100 / PLACA =================
#define RREF 421.1f
#define RNOMINAL 100.0f

// ================= DEBUG =================
#define DBG 1
#if DBG
  #define DPRINT(...) Serial.print(__VA_ARGS__)
  #define DPRINTLN(...) Serial.println(__VA_ARGS__)
  #define DPRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DPRINT(...)
  #define DPRINTLN(...)
  #define DPRINTF(...)
#endif

// ================= OBJETOS =================
Preferences prefs;
Adafruit_MAX31865 rtd(PIN_CS, PIN_MOSI, PIN_MISO, PIN_SCK);

// ================= CONFIG ACTUAL =================
char sensorName[25];
float tempOffset = DEFAULT_TEMP_OFFSET;
uint32_t sleepTime = DEFAULT_SLEEP_TIME;

// ================= TIMING =================
static uint32_t bootMs = 0;

// =====================================================
// UTILIDADES
// =====================================================
static void getMyMac(uint8_t* mac) {
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

static String macToString(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

static String getMacString() {
  uint8_t mac[6];
  getMyMac(mac);
  return macToString(mac);
}

static String urlEncode(const String& s) {
  String out;
  out.reserve(s.length() * 3);
  const char* hex = "0123456789ABCDEF";

  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

static void printWakeReason() {
  esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
  DPRINT("[BOOT] wake_cause=");
  switch (c) {
    case ESP_SLEEP_WAKEUP_TIMER:    DPRINTLN("TIMER"); break;
    case ESP_SLEEP_WAKEUP_EXT0:     DPRINTLN("EXT0"); break;
    case ESP_SLEEP_WAKEUP_EXT1:     DPRINTLN("EXT1"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: DPRINTLN("TOUCH"); break;
    case ESP_SLEEP_WAKEUP_ULP:      DPRINTLN("ULP"); break;
    default:                        DPRINTLN("POWERON/OTHER"); break;
  }
}

static void printCurrentConfig(const char* tag) {
  DPRINTF("[%s] name='%s' offset=%.3f sleep=%lus mac=%s\n",
          tag,
          sensorName,
          (double)tempOffset,
          (unsigned long)sleepTime,
          getMacString().c_str());
}

// =====================================================
// PERSISTENCIA
// =====================================================
static void loadConfig() {
  prefs.begin("cfg", true);

  String s = prefs.getString("name", DEFAULT_SENSOR_NAME);
  strlcpy(sensorName, s.c_str(), sizeof(sensorName));

  tempOffset = prefs.getFloat("offset", DEFAULT_TEMP_OFFSET);
  sleepTime = prefs.getUInt("sleep", DEFAULT_SLEEP_TIME);

  prefs.end();

  if (strlen(sensorName) == 0) {
    strlcpy(sensorName, DEFAULT_SENSOR_NAME, sizeof(sensorName));
  }

  if (sleepTime < 5) sleepTime = 5;
  if (sleepTime > 7200) sleepTime = 7200;
}

static void saveConfig() {
  prefs.begin("cfg", false);
  prefs.putString("name", sensorName);
  prefs.putFloat("offset", tempOffset);
  prefs.putUInt("sleep", sleepTime);
  prefs.end();
}

// =====================================================
// BATERÍA
// =====================================================
static void setupBatteryADC() {
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);

  pinMode(BAT_ADC_PIN, INPUT);
  pinMode(PIN_SENSOR_PWR, OUTPUT);
  digitalWrite(PIN_SENSOR_PWR, LOW);
}

static float readBatteryVoltage() {
  uint32_t acc = 0;

  for (int i = 0; i < BAT_SAMPLES; i++) {
    acc += (uint32_t)analogReadMilliVolts(BAT_ADC_PIN);
    delay(2);
  }

  float mv_adc = (float)acc / (float)BAT_SAMPLES;
  float v_adc = mv_adc / 1000.0f;
  float v_bat = v_adc * ((BAT_R1_OHM + BAT_R2_OHM) / BAT_R2_OHM);
  v_bat = v_bat * BAT_CAL_K + BAT_CAL_B;

  if (v_bat < 0.0f) v_bat = 0.0f;
  if (v_bat > 6.0f) v_bat = 6.0f;

  DPRINTF("[BAT] adc=%.1f mV v_adc=%.3f V v_bat=%.3f V\n",
          (double)mv_adc, (double)v_adc, (double)v_bat);

  return v_bat;
}

static float batteryPercentFromVoltage(float vbat) {
  float pct = (vbat - BAT_VMIN) / (BAT_VMAX - BAT_VMIN);
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 1.0f) pct = 1.0f;
  return pct * 100.0f;
}

// =====================================================
// MAX31865
// =====================================================
static bool initMAX31865() {
  DPRINTLN("[MAX31865] init...");
  digitalWrite(PIN_SENSOR_PWR, HIGH);
  delay(150);

  rtd.begin(MAX31865_3WIRE);
  rtd.clearFault();
  delay(10);

  uint16_t raw = rtd.readRTD();
  uint8_t fault = rtd.readFault();

  if (fault || raw == 0) {
    DPRINTF("[MAX31865][ERR] raw=%u fault=0x%02X\n", raw, fault);
    digitalWrite(PIN_SENSOR_PWR, LOW);
    return false;
  }

  DPRINTF("[MAX31865] ok raw=%u\n", raw);
  return true;
}

static float readTemperature(uint8_t samples = 3) {
  DPRINTF("[TEMP] leyendo %u muestras...\n", samples);

  float sum = 0.0f;
  uint8_t valid = 0;

  for (uint8_t i = 0; i < samples; i++) {
    uint8_t fault = rtd.readFault();
    if (fault) {
      DPRINTF("[TEMP][WARN] fault=0x%02X (clear)\n", fault);
      rtd.clearFault();
      delay(50);
      continue;
    }

    float t = rtd.temperature(RNOMINAL, RREF);

    if (!isnan(t) && t > -80.0f && t < 300.0f) {
      sum += t;
      valid++;
      DPRINTF("[TEMP] sample[%u]=%.3f\n", i, (double)t);
    } else {
      DPRINTF("[TEMP][WARN] sample[%u]=NaN/Out(%.3f)\n", i, (double)t);
    }

    delay(20);
  }

  if (!valid) {
    DPRINTLN("[TEMP][ERR] 0 muestras válidas -> -999.9");
    digitalWrite(PIN_SENSOR_PWR, LOW);
    return -999.9f;
  }

  float avg = sum / valid;
  float out = avg + tempOffset;

  DPRINTF("[TEMP] avg=%.3f offset=%.3f -> out=%.3f\n",
          (double)avg, (double)tempOffset, (double)out);

  DPRINTLN("[SENSOR] Power OFF");
  digitalWrite(PIN_SENSOR_PWR, LOW);

  return out;
}

// =====================================================
// WIFI
// =====================================================
static bool connectWiFi(uint32_t timeoutMs = 15000) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (USE_STATIC_IP) {
    if (!WiFi.config(NODE_LOCAL_IP, NODE_GATEWAY, NODE_SUBNET)) {
      DPRINTLN("[WIFI] ERROR al configurar IP fija");
    } else {
      DPRINTF("[WIFI] IP fija configurada: %s\n", NODE_LOCAL_IP.toString().c_str());
    }
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  DPRINT("[WIFI] conectando");
  uint32_t t0 = millis();

  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(400);
    DPRINT(".");
  }
  DPRINTLN("");

  if (WiFi.status() != WL_CONNECTED) {
    DPRINTLN("[WIFI][ERR] no conectado");
    return false;
  }

  DPRINTF("[WIFI] conectado IP=%s RSSI=%d\n",
          WiFi.localIP().toString().c_str(),
          WiFi.RSSI());
  return true;
}

// =====================================================
// DESCARGAR CONFIG DEL MAESTRO
// =====================================================
static bool fetchRemoteConfig() {
  if (WiFi.status() != WL_CONNECTED) {
    DPRINTLN("[CFG][ERR] WiFi no conectado");
    return false;
  }

  String mac = getMacString();
  String url = "http://" + String(MASTER_HOST) + ":" + String(MASTER_PORT)
             + "/api/config?mac=" + urlEncode(mac)
             + "&name=" + urlEncode(String(sensorName));

  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);

  DPRINTF("[CFG] GET %s\n", url.c_str());

  if (!http.begin(url)) {
    DPRINTLN("[CFG][ERR] http.begin falló");
    return false;
  }

  int code = http.GET();
  String resp = http.getString();
  http.end();

  DPRINTF("[CFG] code=%d resp=%s\n", code, resp.c_str());

  if (code < 200 || code >= 300) {
    return false;
  }

  DynamicJsonDocument doc(384);
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    DPRINTF("[CFG][ERR] JSON inválido: %s\n", err.c_str());
    return false;
  }

  bool apply = doc["apply"] | false;
  if (!apply) {
    DPRINTLN("[CFG] apply=false, se mantiene config local");
    return true;
  }

  String newName = doc["newName"] | String(sensorName);
  float newOffset = doc["tempOffset"] | tempOffset;
  uint32_t newSleep = doc["sleepSec"] | sleepTime;

  if (newName.length() == 0) newName = DEFAULT_SENSOR_NAME;
  if (newSleep < 5) newSleep = 5;
  if (newSleep > 7200) newSleep = 7200;

  bool changed = false;

  if (newName != String(sensorName)) {
    strlcpy(sensorName, newName.c_str(), sizeof(sensorName));
    changed = true;
  }

  if (fabs(newOffset - tempOffset) > 0.0001f) {
    tempOffset = newOffset;
    changed = true;
  }

  if (newSleep != sleepTime) {
    sleepTime = newSleep;
    changed = true;
  }

  if (changed) {
    saveConfig();
    printCurrentConfig("CFG_APPLIED");
  } else {
    printCurrentConfig("CFG_NO_CHANGE");
  }

  return true;
}

// =====================================================
// ENVIAR DATOS AL MAESTRO
// =====================================================
static bool sendDataToMaster(float tempC, float battPct) {
  if (WiFi.status() != WL_CONNECTED) {
    DPRINTLN("[DATA][ERR] WiFi no conectado");
    return false;
  }

  String mac = getMacString();
  String url = "http://" + String(MASTER_HOST) + ":" + String(MASTER_PORT) + "/api/push";

  DynamicJsonDocument doc(256);
  doc["mac"] = mac;
  doc["name"] = sensorName;
  doc["temperature"] = tempC;
  doc["battery"] = battPct;
  doc["uptime_ms"] = millis();
  doc["rssi"] = WiFi.RSSI();

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.setConnectTimeout(4000);
  http.setTimeout(5000);

  DPRINTF("[DATA] POST %s\n", url.c_str());
  DPRINTLN(payload);

  if (!http.begin(url)) {
    DPRINTLN("[DATA][ERR] http.begin falló");
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  int code = http.POST(payload);
  String resp = http.getString();
  http.end();

  DPRINTF("[DATA] code=%d resp=%s\n", code, resp.c_str());

  return (code >= 200 && code < 300);
}

// =====================================================
// DEEP SLEEP
// =====================================================
static void goToDeepSleep() {
  uint32_t onMs = millis() - bootMs;

  DPRINTF("[BOOT] tiempo encendida: %lums (%.3fs)\n",
          (unsigned long)onMs, (double)onMs / 1000.0);

  DPRINTF("[SLEEP] entrando a deep sleep: %lus...\n",
          (unsigned long)sleepTime);

  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);

  esp_sleep_enable_timer_wakeup((uint64_t)sleepTime * 1000000ULL);
  Serial.flush();
  esp_deep_sleep_start();
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  bootMs = millis();

  DPRINTLN("\n==============================");
  DPRINTLN("[BOOT] Nodo WiFi iniciando...");
  printWakeReason();

  setupBatteryADC();

  loadConfig();
  printCurrentConfig("CFG_LOADED");

  bool ok31865 = initMAX31865();
  if (!ok31865) {
    DPRINTLN("[MAX31865][WARN] sensor no listo");
  }

  bool wifiOk = connectWiFi(15000);
  if (wifiOk) {
    fetchRemoteConfig();
  } else {
    DPRINTLN("[CFG][WARN] sin WiFi, se usa config local");
  }

  float temp = readTemperature();
  float battV = readBatteryVoltage();
  float battPct = batteryPercentFromVoltage(battV);

  DPRINTF("[BAT] V=%.3f pct=%.1f%%\n", (double)battV, (double)battPct);

  bool sentOk = false;
  if (wifiOk) {
    sentOk = sendDataToMaster(temp, battPct);
  }

  if (!sentOk) {
    DPRINTLN("[DATA][WARN] no se pudo enviar al maestro");
  }

  goToDeepSleep();
}

void loop() {
}