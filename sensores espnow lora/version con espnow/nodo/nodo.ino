#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <esp_system.h>  // esp_read_mac
#include <esp_mac.h>
#include <SPI.h>
#include <Adafruit_MAX31865.h>
#define ESPNOW_CHANNEL 1
// ================= CONFIGURACIÓN POR DEFECTO =================
#define DEFAULT_SENSOR_NAME "SENSOR_1"
#define DEFAULT_SLEEP_TIME 60  // segundos
#define DEFAULT_TEMP_OFFSET 0.0

// ================= PINES MAX31865 (SPI por software) =================
#define PIN_SCK 16
#define PIN_MISO 23  // SDO
#define PIN_MOSI 22  // SDI
#define PIN_CS 21    // CS
// ================= PIN ACTIVAR SENSOR =================
#define PIN_SENSOR_PWR 2
// ================= LECTURA BATERÍA (ADC + DIVISOR) =================
#define BAT_ADC_PIN 0

// Divisor: VBAT -- R1 --(ADC)-- R2 -- GND
#define BAT_R1_OHM 344800.0f
#define BAT_R2_OHM 994300.0f

// Calibración fina (si tu multímetro difiere): VBAT_real = VBAT_calc * K + B
#define BAT_CAL_K 1.000f
#define BAT_CAL_B 0.090f

// Rango típico Li-ion 1S para % (ajusta según tu batería)
#define BAT_VMIN 3.30f
#define BAT_VMAX 4.15f

// Muestras para promedio
#define BAT_SAMPLES 3

// ================= CONFIG PT100 / PLACA =================
#define RREF 421.1f
#define RNOMINAL 100.0f  // PT100

// ================= MAC DEL MAESTRO =================
uint8_t MASTER_MAC[] = { 0x58, 0xE6, 0xC5, 0x19, 0x28, 0x20 };

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

// ================= ESTRUCTURAS =================
typedef struct __attribute__((packed)) {
  uint8_t msgType;  // 1=data, 2=config_request, 3=config_response

  union {
    struct {
      char name[25];
      float temperature;
      float battery;  // recomendado: VOLTIOS de batería
      uint8_t mac[6];
      uint32_t uptime_ms;
    } data;

    struct {
      char name[25];
      uint8_t mac[6];
    } cfg_req;

    struct {
      char target[25];  // nombre o "MAC:xx:xx..."
      char newName[25];
      float tempOffset;
      uint32_t sleepSec;
      bool apply;
    } cfg_rsp;
  };
} espnow_msg_t;

// ================= OBJETOS =================
Preferences prefs;
Adafruit_MAX31865 rtd(PIN_CS, PIN_MOSI, PIN_MISO, PIN_SCK);

espnow_msg_t txMsg;
espnow_msg_t rxMsg;

// ================= CONFIG ACTUAL =================
char sensorName[25];
float tempOffset;
uint32_t sleepTime;
volatile bool configUpdated = false;

// ================= TIMING (para medir cuánto dura encendida) =================
static uint32_t bootMs = 0;

// ================= UTILIDADES =================
static void getMyMac(uint8_t *mac) {
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

static String macToString(const uint8_t *mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

static void printWakeReason() {
  esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
  DPRINT("[BOOT] wake_cause=");
  switch (c) {
    case ESP_SLEEP_WAKEUP_TIMER: DPRINTLN("TIMER"); break;
    case ESP_SLEEP_WAKEUP_EXT0: DPRINTLN("EXT0"); break;
    case ESP_SLEEP_WAKEUP_EXT1: DPRINTLN("EXT1"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: DPRINTLN("TOUCH"); break;
    case ESP_SLEEP_WAKEUP_ULP: DPRINTLN("ULP"); break;
    default: DPRINTLN("POWERON/OTHER"); break;
  }
}

static void printCurrentConfig(const char *tag) {
  uint8_t mac[6];
  getMyMac(mac);
  DPRINTF("[%s] name='%s' offset=%.3f sleep=%lus mac=%s\n",
          tag,
          sensorName,
          (double)tempOffset,
          (unsigned long)sleepTime,
          macToString(mac).c_str());
}

// ================= BATERÍA =================
static void setupBatteryADC() {
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);

  pinMode(BAT_ADC_PIN, INPUT);
  pinMode(PIN_SENSOR_PWR, OUTPUT);
}

static float readBatteryVoltage() {

  uint32_t acc = 0;

  for (int i = 0; i < BAT_SAMPLES; i++) {
    acc += (uint32_t)analogReadMilliVolts(BAT_ADC_PIN);
    delay(2);
  }

  // promedio en milivoltios del pin ADC
  float mv_adc = (float)acc / (float)BAT_SAMPLES;

  // convertir a voltios
  float v_adc = mv_adc / 1000.0f;

  // reconstruir voltaje real de batería por divisor
  float v_bat = v_adc * ((BAT_R1_OHM + BAT_R2_OHM) / BAT_R2_OHM);

  // calibración fina
  v_bat = v_bat * BAT_CAL_K + BAT_CAL_B;

  // sanitizar
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

// ================= MAX31865 =================
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
  }

  if (!valid) {
    DPRINTLN("[TEMP][ERR] 0 muestras válidas -> -999.9");
    return -999.9f;
  }

  float avg = sum / valid;
  float out = avg + tempOffset;
  DPRINTF("[TEMP] avg=%.3f offset=%.3f -> out=%.3f\n", (double)avg, (double)tempOffset, (double)out);
  DPRINTLN("[SENSOR] Power OFF");
  digitalWrite(PIN_SENSOR_PWR, LOW);

  return out;
}

// ================= PERSISTENCIA =================
static void loadConfig() {
  prefs.begin("cfg", true);
  prefs.getString("name", DEFAULT_SENSOR_NAME).toCharArray(sensorName, sizeof(sensorName));
  tempOffset = prefs.getFloat("offset", DEFAULT_TEMP_OFFSET);
  sleepTime = prefs.getUInt("sleep", DEFAULT_SLEEP_TIME);
  prefs.end();

  // saneo
  if (sleepTime > 7200) sleepTime = 7200;
}

static void saveConfig() {
  prefs.begin("cfg", false);
  prefs.putString("name", sensorName);
  prefs.putFloat("offset", tempOffset);
  prefs.putUInt("sleep", sleepTime);
  prefs.end();
}

// ================= ESP-NOW CALLBACKS =================
static void onDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  if (len != (int)sizeof(espnow_msg_t)) return;
  memcpy(&rxMsg, data, sizeof(rxMsg));

  if (rxMsg.msgType != 3) return;

  WiFi.mode(WIFI_STA);
  delay(10);

  uint8_t myMac[6];
  getMyMac(myMac);

  char myTarget[32];
  snprintf(myTarget, sizeof(myTarget), "MAC:%02X:%02X:%02X:%02X:%02X:%02X",
           myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);

  bool forMe = false;
  if (strncmp(rxMsg.cfg_rsp.target, "MAC:", 4) == 0) {
    forMe = (strcmp(rxMsg.cfg_rsp.target, myTarget) == 0);
  } else {
    forMe = (strcmp(rxMsg.cfg_rsp.target, sensorName) == 0);
  }
  if (!forMe) return;

  if (!rxMsg.cfg_rsp.apply) {
    DPRINTF("[CFG] sin config nueva. target='%s' off=%.3f sleep=%lu\n",
            rxMsg.cfg_rsp.target,
            (double)rxMsg.cfg_rsp.tempOffset,
            (unsigned long)rxMsg.cfg_rsp.sleepSec);
    configUpdated = true;
    return;
  }

  DPRINTF("[CFG] APLICANDO config nueva. target='%s' newName='%s' off=%.3f sleep=%lu\n",
          rxMsg.cfg_rsp.target,
          rxMsg.cfg_rsp.newName,
          (double)rxMsg.cfg_rsp.tempOffset,
          (unsigned long)rxMsg.cfg_rsp.sleepSec);

  if (strlen(rxMsg.cfg_rsp.newName)) {
    strncpy(sensorName, rxMsg.cfg_rsp.newName, sizeof(sensorName) - 1);
    sensorName[sizeof(sensorName) - 1] = 0;
  }

  tempOffset = rxMsg.cfg_rsp.tempOffset;

  uint32_t s = rxMsg.cfg_rsp.sleepSec;
  if (s > 7200) s = 7200;
  sleepTime = s;

  saveConfig();
  configUpdated = true;

  printCurrentConfig("CFG_APPLIED");
}

static void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  DPRINTF("[ESP-NOW] send status=%s\n", (status == ESP_NOW_SEND_SUCCESS) ? "OK" : "FAIL");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  bootMs = millis();
  DPRINTLN("\n==============================");
  DPRINTLN("[BOOT] Nodo iniciando...");
  printWakeReason();

  // 0) ADC batería
  setupBatteryADC();

  // 1) Cargar config persistida (NVS / Preferences)
  loadConfig();
  printCurrentConfig("CFG_LOADED");

  // 2) Inicializar MAX31865
  bool ok31865 = initMAX31865();
  if (!ok31865) {
    DPRINTLN("[MAX31865] No listo (revisa jumper 3-wire y cableado F+/RTD+/RTD-).");
  }

  // 3) WiFi/ESP-NOW
  DPRINTLN("[NET] WiFi STA + ESP-NOW init...");
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  if (esp_now_init() != ESP_OK) {
    DPRINTLN("[NET][ERR] esp_now_init failed -> restart");
    delay(100);
    esp_restart();
  }

  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, MASTER_MAC, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;

  if (!esp_now_is_peer_exist(MASTER_MAC)) {
    esp_err_t a = esp_now_add_peer(&peer);
    DPRINTF("[NET] add_peer=%s\n", (a == ESP_OK) ? "OK" : "FAIL");
  } else {
    DPRINTLN("[NET] peer master ya existe");
  }

  // 4) Solicitar config (al arrancar siempre)
  DPRINTLN("[CFG] solicitando config al maestro...");
  txMsg.msgType = 2;
  strlcpy(txMsg.cfg_req.name, sensorName, sizeof(txMsg.cfg_req.name));
  getMyMac(txMsg.cfg_req.mac);

  configUpdated = false;
  esp_now_send(MASTER_MAC, (uint8_t *)&txMsg, sizeof(txMsg));

  const uint32_t waitMs = 1200;
  uint32_t t0 = millis();
  while (!configUpdated && (millis() - t0) < waitMs) {
    delay(10);
  }
  if (!configUpdated) {
    DPRINTF("[CFG][WARN] no llegó respuesta en %lums (uso config local)\n", (unsigned long)waitMs);
    printCurrentConfig("CFG_LOCAL");
  } else {
    DPRINTF("[CFG] config aplicada en %lums\n", (unsigned long)(millis() - t0));
  }

  // 5) Leer temperatura
  float temp = readTemperature();

  // 5.1) Leer batería
  float battV = readBatteryVoltage();
  float battPct = batteryPercentFromVoltage(battV);
  DPRINTF("[BAT] V=%.3f pct=%.1f%%\n", (double)battV, (double)battPct);

  // 6) Enviar datos
  DPRINTLN("[DATA] preparando envío...");
  txMsg.msgType = 1;
  strlcpy(txMsg.data.name, sensorName, sizeof(txMsg.data.name));
  txMsg.data.temperature = temp;
  txMsg.data.battery = battPct;
  txMsg.data.uptime_ms = millis();
  getMyMac(txMsg.data.mac);

  DPRINTF("[DATA] name='%s' temp=%.3f batt=%.3fV sleep=%lus uptime_ms=%lu\n",
          txMsg.data.name,
          (double)txMsg.data.temperature,
          (double)txMsg.data.battery,
          (unsigned long)sleepTime,
          (unsigned long)txMsg.data.uptime_ms);

  esp_now_send(MASTER_MAC, (uint8_t *)&txMsg, sizeof(txMsg));

  delay(40);

  // 7) Medir cuánto duró encendida
  uint32_t onMs = millis() - bootMs;
  DPRINTF("[BOOT] tiempo encendida: %lums (%.3fs)\n", (unsigned long)onMs, (double)onMs / 1000.0);

  // 8) Deep sleep
  DPRINTF("[SLEEP] entrando a deep sleep: %lus...\n", (unsigned long)sleepTime);
  WiFi.mode(WIFI_OFF);

  esp_sleep_enable_timer_wakeup((uint64_t)sleepTime * 1000000ULL);
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {}