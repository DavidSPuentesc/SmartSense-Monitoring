#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <SPI.h>
#include <Adafruit_MAX31865.h>
#include <math.h>

// =====================================================
// CONFIG GENERAL
// =====================================================
static constexpr bool AUTO_CONFIGURE_ON_BOOT = false;
static constexpr uint16_t CONFIG_WINDOW_CYCLES = 30; // si dep seleep 120 entonces son 60 minutos para configurar

// =========================
// E220 - XIAO ESP32-C6
// =========================
static constexpr int PIN_E220_RX = 17;  // D7  <- TXD del E220
static constexpr int PIN_E220_TX = 19;  // D8  -> RXD del E220
static constexpr int PIN_E220_M0 = 18;  // D10
static constexpr int PIN_E220_M1 = 20;  // D9

// Dirección propia del nodo y destino maestro
static constexpr uint16_t NODE_ADDR = 13;
static constexpr uint8_t NODE_CH = 23;

static constexpr uint16_t MASTER_ADDR = 1;
static constexpr uint8_t MASTER_CH = 23;

// =========================
// CONFIGURACIÓN POR DEFECTO
// =========================
#define DEFAULT_SLEEP_TIME 60

// =========================
// PINES MAX31865 (SPI software)
// =========================
#define PIN_SCK  16
#define PIN_MISO 23
#define PIN_MOSI 22
#define PIN_CS   21

// =========================
// PIN ACTIVAR SENSOR
// =========================
#define PIN_SENSOR_PWR 2

// =========================
// LECTURA BATERÍA
// =========================
#define BAT_ADC_PIN 0

#define BAT_R1_OHM 344800.0f
#define BAT_R2_OHM 994300.0f

#define BAT_CAL_K 1.000f
#define BAT_CAL_B 0.090f

#define BAT_VMIN 3.30f
#define BAT_VMAX 4.10f

#define BAT_SAMPLES 1

// =========================
// CONFIG PT100
// =========================
#define RREF 421.1f
#define RNOMINAL 100.0f

// =========================
// DEBUG
// =========================
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

// =========================
// CÓDIGOS AT E220
// =========================
static constexpr int UART_BAUD_CODE   = 3;   // 9600
static constexpr int UART_PARITY_CODE = 0;   // none
static constexpr int AIR_RATE_CODE    = 2;   // 2.4kbps
static constexpr int TRANS_MODE_CODE  = 1;   // fixed
static constexpr int PACKET_LEN_CODE  = 0;   // 200 bytes
static constexpr int URXT_BYTE_TIME   = 3;
static constexpr int POWER_CODE       = 0;
static constexpr int KEY_CODE         = 0;
static constexpr int LBT_CODE         = 0;

// =========================
// OBJETOS
// =========================
Preferences prefs;
Adafruit_MAX31865 rtd(PIN_CS, PIN_MOSI, PIN_MISO, PIN_SCK);

// =========================
// CONFIG ACTUAL
// =========================
uint32_t sleepTime = DEFAULT_SLEEP_TIME;
uint16_t configCyclesRemaining = CONFIG_WINDOW_CYCLES;

// =========================
// TIMING / ESTADO
// =========================
static uint32_t bootMs = 0;
static String radioRxLine;
static bool g_gotConfigReply = false;

// Contador de paquetes para calcular PDR en el gateway.
// RTC_DATA_ATTR sobrevive al deep sleep; se reinicia solo al cortar la
// alimentación (el gateway detecta ese reinicio porque SEQ vuelve a 1).
RTC_DATA_ATTR uint32_t txSeq = 0;

// =====================================================
// DECLARACIONES ADELANTADAS
// =====================================================
static void saveConfig();

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
  DPRINTF("[%s] sleep=%lus mac=%s cfg_cycles=%u\n",
          tag,
          (unsigned long)sleepTime,
          getMacString().c_str(),
          configCyclesRemaining);
}

static void handleConfigWindowByWakeCause() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    if (configCyclesRemaining > 0) {
      configCyclesRemaining--;
      DPRINTF("[CFG_WINDOW] wake=TIMER -> ciclos restantes: %u\n", configCyclesRemaining);
      saveConfig();
    } else {
      DPRINTLN("[CFG_WINDOW] wake=TIMER -> ventana cerrada");
    }
  } else {
    configCyclesRemaining = CONFIG_WINDOW_CYCLES;
    DPRINTF("[CFG_WINDOW] wake=POWERON/OTHER -> ventana reiniciada a %u ciclos\n",
            CONFIG_WINDOW_CYCLES);
    saveConfig();
  }
}

// =====================================================
// PERSISTENCIA
// =====================================================
static void loadConfig() {
  prefs.begin("cfg", true);
  sleepTime = prefs.getUInt("sleep", DEFAULT_SLEEP_TIME);
  configCyclesRemaining = prefs.getUShort("cfg_cycles", CONFIG_WINDOW_CYCLES);
  prefs.end();

  if (sleepTime < 5) sleepTime = 5;
  if (sleepTime > 7200) sleepTime = 7200;
}

static void saveConfig() {
  prefs.begin("cfg", false);
  prefs.putUInt("sleep", sleepTime);
  prefs.putUShort("cfg_cycles", configCyclesRemaining);
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

  //DPRINTF("[BAT] adc=%.1f mV v_adc=%.3f V v_bat=%.3f V\n",(double)mv_adc, (double)v_adc, (double)v_bat);

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
static bool initMAX31865(uint8_t maxRetries = 2) {
  DPRINTLN("[MAX31865] init...");
  digitalWrite(PIN_SENSOR_PWR, HIGH);
  delay(80);

  for (uint8_t attempt = 0; attempt < maxRetries; attempt++) {
    if (attempt > 0) {
      DPRINTF("[MAX31865] Reintentando (%u/%u)...\n", attempt + 1, maxRetries);
      delay(100);
    }

    rtd.begin(MAX31865_3WIRE);
    rtd.clearFault();
    delay(10);

    uint16_t raw = rtd.readRTD();
    uint8_t fault = rtd.readFault();

    if (!fault && raw != 0) {
      //DPRINTF("[MAX31865] ok raw=%u\n", raw);
      return true;
    }

    DPRINTF("[MAX31865][ERR] raw=%u fault=0x%02X\n", raw, fault);
  }

  DPRINTLN("[MAX31865][ERR] Fallo después de reintentos");
  digitalWrite(PIN_SENSOR_PWR, LOW);
  return false;
}

static float readTemperature(uint8_t samples = 4) {
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
      //DPRINTF("[TEMP] sample[%u]=%.3f\n", i, (double)t);
    } else {
      DPRINTF("[TEMP][WARN] sample[%u]=NaN/Out(%.3f)\n", i, (double)t);
    }

    delay(10);
  }

  if (!valid) {
    DPRINTLN("[TEMP][WARN] 0 muestras válidas, intentando lectura adicional...");
    rtd.clearFault();
    delay(100);

    uint8_t fault = rtd.readFault();
    if (!fault) {
      float t = rtd.temperature(RNOMINAL, RREF);
      if (!isnan(t) && t > -80.0f && t < 300.0f) {
        DPRINTF("[TEMP] Lectura adicional exitosa: %.3f\n", (double)t);
        DPRINTLN("[SENSOR] Power OFF");
        digitalWrite(PIN_SENSOR_PWR, LOW);
        return t;
      } else {
        DPRINTF("[TEMP][ERR] Lectura adicional inválida: %.3f\n", (double)t);
      }
    } else {
      DPRINTF("[TEMP][ERR] Fault persistente: 0x%02X\n", fault);
    }

    DPRINTLN("[TEMP][ERR] No se pudo obtener lectura válida -> 0.0");
    digitalWrite(PIN_SENSOR_PWR, LOW);
    return 0.0f;
  }

  float avg = sum / valid;

  DPRINTF("[TEMP] avg=%.3f\n", (double)avg);

  //DPRINTLN("[SENSOR] Power OFF");
  digitalWrite(PIN_SENSOR_PWR, LOW);

  return avg;
}

// =====================================================
// E220 - MODO
// =====================================================
static void setModeConfig() {
  digitalWrite(PIN_E220_M0, HIGH);
  digitalWrite(PIN_E220_M1, HIGH);
}

static void setModeNormal() {
  digitalWrite(PIN_E220_M0, LOW);
  digitalWrite(PIN_E220_M1, LOW);
}

// =====================================================
// E220 - AT
// =====================================================
static void clearRadioInput() {
  while (Serial1.available() > 0) {
    Serial1.read();
  }
}

static String readATResponse(uint32_t timeoutMs = 800) {
  String out;
  uint32_t t0 = millis();

  while ((millis() - t0) < timeoutMs) {
    while (Serial1.available() > 0) {
      char c = (char)Serial1.read();
      out += c;
      t0 = millis();
    }
  }

  out.trim();
  return out;
}

static bool sendAT(const String& cmd, const char* expected = "=OK", uint32_t timeoutMs = 800) {
  clearRadioInput();

  DPRINT("[AT TX] ");
  DPRINTLN(cmd);

  Serial1.print(cmd);
  Serial1.print("\r\n");

  String resp = readATResponse(timeoutMs);

  DPRINT("[AT RX] ");
  DPRINTLN(resp.length() ? resp : "(sin respuesta)");

  if (!expected || expected[0] == '\0') return true;
  return resp.indexOf(expected) >= 0;
}

static bool queryAT(const String& cmd, uint32_t timeoutMs = 800) {
  clearRadioInput();

  DPRINT("[AT TX] ");
  DPRINTLN(cmd);

  Serial1.print(cmd);
  Serial1.print("\r\n");

  String resp = readATResponse(timeoutMs);

  DPRINT("[AT RX] ");
  DPRINTLN(resp.length() ? resp : "(sin respuesta)");

  return resp.length() > 0;
}

static bool configureE220() {
  bool ok = true;

  DPRINTLN("=== CONFIGURANDO E220 NODO ===");

  ok &= queryAT("AT+DEVTYPE=?");
  ok &= queryAT("AT+FWCODE=?");

  ok &= sendAT("AT+ADDR=" + String(NODE_ADDR));
  ok &= sendAT("AT+CHANNEL=" + String(NODE_CH));
  ok &= sendAT("AT+UART=" + String(UART_BAUD_CODE) + "," + String(UART_PARITY_CODE));
  ok &= sendAT("AT+RATE=" + String(AIR_RATE_CODE));
  ok &= sendAT("AT+TRANS=" + String(TRANS_MODE_CODE));
  ok &= sendAT("AT+PACKET=" + String(PACKET_LEN_CODE));
  ok &= sendAT("AT+URXT=" + String(URXT_BYTE_TIME));
  ok &= sendAT("AT+KEY=" + String(KEY_CODE));
  ok &= sendAT("AT+LBT=" + String(LBT_CODE));
  ok &= sendAT("AT+POWER=" + String(POWER_CODE));

  DPRINTLN("\n=== VERIFICACION FINAL NODO ===");
  ok &= queryAT("AT+ADDR=?");
  ok &= queryAT("AT+CHANNEL=?");
  ok &= queryAT("AT+UART=?");
  ok &= queryAT("AT+RATE=?");
  ok &= queryAT("AT+TRANS=?");

  return ok;
}

static bool bootConfigureIfEnabled() {
  if (!AUTO_CONFIGURE_ON_BOOT) {
   // DPRINTLN("[E220] auto config desactivado");
    return true;
  }

  setModeConfig();
  delay(150);

    
  delay(200);

  bool ok = configureE220();

  setModeNormal();
  delay(150);

  Serial1.end();
  delay(50);

  return ok;
}

// =====================================================
// RADIO TX
// =====================================================
static void sendToMaster(const char* msg) {
  const uint8_t addrH = (MASTER_ADDR >> 8) & 0xFF;
  const uint8_t addrL = MASTER_ADDR & 0xFF;
  const uint8_t ch = MASTER_CH;

  DPRINTF("[RADIO TX] %02X %02X %02X | %s\n",
          addrH, addrL, ch, msg);

  Serial1.write(addrH);
  Serial1.write(addrL);
  Serial1.write(ch);
  Serial1.print(msg);
  Serial1.print("\n");
  Serial1.flush();
  delay(20);
}

// =====================================================
// RADIO RX / CONFIG
// =====================================================
static bool applyConfigLine(const String& line) {
  if (!line.startsWith("CFG,")) {
    return false;
  }

  uint32_t newSleep = sleepTime;

  int start = 4;
  while (start < line.length()) {
    int comma = line.indexOf(',', start);
    String token = (comma == -1) ? line.substring(start) : line.substring(start, comma);
    token.trim();

    int eq = token.indexOf('=');
    if (eq > 0) {
      String key = token.substring(0, eq);
      String val = token.substring(eq + 1);

      key.trim();
      val.trim();
      key.toUpperCase();

      if (key == "SLEEP") {
        newSleep = (uint32_t)val.toInt();
      }
    }

    if (comma == -1) break;
    start = comma + 1;
  }

  if (newSleep < 5) newSleep = 5;
  if (newSleep > 7200) newSleep = 7200;

  bool changed = false;

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

  char ack[120];
  snprintf(ack, sizeof(ack),
           "CFG_ACK,ID=%u,SLEEP=%lu",
           NODE_ADDR,
           (unsigned long)sleepTime);
  sendToMaster(ack);

  g_gotConfigReply = true;
  return true;
}

static void processRadioLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  DPRINTF("[RADIO RX] %s\n", line.c_str());

  if (line == "PING") {
    char pong[32];
    snprintf(pong, sizeof(pong), "PONG,ID=%u", NODE_ADDR);
    sendToMaster(pong);
    return;
  }

  if (line == "REQ_STATUS") {
    char statusMsg[120];
    snprintf(statusMsg, sizeof(statusMsg),
             "STATUS,ID=%u,SLEEP=%lu",
             NODE_ADDR,
             (unsigned long)sleepTime);
    sendToMaster(statusMsg);
    return;
  }

  applyConfigLine(line);
}

static void handleRadioRx() {
  while (Serial1.available() > 0) {
    char c = (char)Serial1.read();

    if (c == '\r') continue;

    if (c == '\n') {
      processRadioLine(radioRxLine);
      radioRxLine = "";
    } else {
      if (radioRxLine.length() < 180) {
        radioRxLine += c;
      } else {
        radioRxLine = "";
      }
    }
  }
}

static void requestRemoteConfig(uint32_t waitMs = 1500) {
  g_gotConfigReply = false;

  char req[120];
  snprintf(req, sizeof(req),
           "CFG_REQ,ID=%u,MAC=%s",
           NODE_ADDR,
           getMacString().c_str());

  DPRINTF("[CFG] Solicitando config por %lums\n", (unsigned long)waitMs);
  sendToMaster(req);

  uint32_t t0 = millis();
  while ((millis() - t0) < waitMs) {
    handleRadioRx();

    if (g_gotConfigReply) {
      DPRINTLN("[CFG] Respuesta recibida, continuando...");
      return;
    }

    delay(5);
  }

  DPRINTLN("[CFG] Timeout sin respuesta");
}

// =====================================================
// ENVÍO DE DATOS
// =====================================================
static void sendDataToMaster(float tempC, float battPct) {
  char macStr[18];
  uint8_t mac[6];
  getMyMac(mac);
  snprintf(macStr, sizeof(macStr),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  txSeq++;  // primer paquete = 1

  // Campos extra para las pruebas: VBAT (voltaje real, para autonomia) y
  // UP (ms encendido hasta transmitir, para jitter/consumo).
  float vbat = readBatteryVoltage();

  char payload[160];
  snprintf(payload, sizeof(payload),
           "DATA,ID=%u,SEQ=%lu,TEMP=%.2f,VBAT=%.3f,BAT=%.1f,UP=%lu,MAC=%s",
           NODE_ADDR,
           (unsigned long)txSeq,
           tempC,
           vbat,
           battPct,
           (unsigned long)millis(),
           macStr);

  sendToMaster(payload);
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
  DPRINTLN("[BOOT] Nodo LoRa iniciando...");
  printWakeReason();

  pinMode(PIN_E220_M0, OUTPUT);
  pinMode(PIN_E220_M1, OUTPUT);

  setupBatteryADC();

  loadConfig();
  printCurrentConfig("CFG_LOADED");

  handleConfigWindowByWakeCause();
  DPRINTF("[CFG_WINDOW] ciclos disponibles: %u\n", configCyclesRemaining);

  bool e220Ok = bootConfigureIfEnabled();
  DPRINTF("[E220] config=%s\n", e220Ok ? "OK" : "ERROR");

  setModeNormal();
  delay(100);

  Serial1.begin(9600, SERIAL_8N1, PIN_E220_RX, PIN_E220_TX);
  delay(100);

  if (configCyclesRemaining > 0) {
    requestRemoteConfig(1500);
  } else {
    DPRINTLN("[CFG_WINDOW] modo normal -> no se solicita configuración");
  }

  bool ok31865 = initMAX31865();
  if (!ok31865) {
    DPRINTLN("[MAX31865][WARN] sensor no listo");
  }

  float temp = readTemperature(4);
  float battV = readBatteryVoltage();
  float battPct = batteryPercentFromVoltage(battV);

  DPRINTF("[BAT] V=%.3f pct=%.1f%%\n", (double)battV, (double)battPct);

  sendDataToMaster(temp, battPct);

  uint32_t postTxWaitMs = (configCyclesRemaining > 0) ? 100 : 20;

  uint32_t t0 = millis();
  while ((millis() - t0) < postTxWaitMs) {
    handleRadioRx();
    delay(2);
  }

  goToDeepSleep();
}

void loop() {
}
