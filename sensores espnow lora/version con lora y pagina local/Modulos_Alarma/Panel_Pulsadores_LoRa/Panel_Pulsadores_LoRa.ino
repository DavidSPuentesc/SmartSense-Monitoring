#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_mac.h"
#include <esp_arduino_version.h>
#include <math.h>

// =====================================================
// PANEL DE PULSADORES LORA
// - 4 botones de emergencia
// - protocolo compacto por ADDR con CRC16 y secuencia
// - envio con ACK y reintentos al central de alarma
// - heartbeat periodico con ACK
// - wake por boton o por timer para heartbeat
// - reset fisico manteniendo los 4 botones 5 segundos
// - si ya hay una alarma activa, una nueva alarma espera
//   500 ms para confirmar que no era el combo de reset
// =====================================================

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << (GPIO))

static constexpr bool AUTO_CONFIGURE_ON_BOOT = false;
static constexpr bool LOG_DEBUG = false;
static constexpr bool ENABLE_BATTERY_MONITOR = false;

#define PANEL_DEBUG_LOGF(...) do { if (LOG_DEBUG) Serial.printf(__VA_ARGS__); } while (0)
#define PANEL_DEBUG_LOGLN(msg) do { if (LOG_DEBUG) Serial.println(msg); } while (0)

static constexpr uint16_t THIS_MODULE_ADDR = 21;
static constexpr uint16_t ALARM_MODULE_ADDR = 2;
static constexpr uint8_t LORA_CHANNEL = 23;

static constexpr int PIN_E220_RX = 17;
static constexpr int PIN_E220_TX = 19;
static constexpr int PIN_E220_M0M1 = 20;


static constexpr int PIN_BTN_FIRE = 5;
static constexpr int PIN_BTN_ACCIDENT = 2;
static constexpr int PIN_BTN_EVAC = 1;
static constexpr int PIN_BTN_MEDICAL = 0;

static constexpr bool BUTTON_ACTIVE_LOW = true;
static constexpr bool OUTPUT_ACTIVE_HIGH = true;
static constexpr int PIN_OUT_FIRE = 22;
static constexpr int PIN_OUT_ACCIDENT = 23;
static constexpr int PIN_OUT_EVAC = 21;
static constexpr int PIN_OUT_MEDICAL = 18;

static constexpr uint32_t PWM_FREQ = 5000;
static constexpr uint8_t PWM_RESOLUTION = 8;
static constexpr uint8_t PWM_DUTY_ACTIVE = 30;  // 30 de 255


static constexpr uint8_t PWM_CH_FIRE = 0;
static constexpr uint8_t PWM_CH_ACCIDENT = 1;
static constexpr uint8_t PWM_CH_EVAC = 2;
static constexpr uint8_t PWM_CH_MEDICAL = 3;


static constexpr unsigned long DEBOUNCE_MS = 45UL;
static constexpr unsigned long INPUT_SCAN_MS = 12UL;
static constexpr unsigned long ACK_RETRY_MS = 3000UL;
static constexpr unsigned long WAIT_LOG_MS = 2000UL;
static constexpr unsigned long HOLD_RESET_MS = 5000UL;
static constexpr unsigned long MULTI_PRESS_GUARD_MS = 500UL;
static constexpr unsigned long HEARTBEAT_INTERVAL_MS = 600000UL;
static constexpr uint64_t HEARTBEAT_WAKE_US = 300000000ULL; // 5 minutos
static constexpr unsigned long SLEEP_IDLE_MS = 1500UL;
static constexpr unsigned long OFFLINE_SEQUENCE_START_MS = 1000UL;
static constexpr unsigned long OFFLINE_SEQUENCE_STEP_MS = 140UL;
static constexpr unsigned long OTA_HOLD_MS = 6000UL; // mantener FIRE + MEDICAL por 6 segundos
static constexpr unsigned long OTA_WIFI_RETRY_MS = 10000UL;
static constexpr unsigned long OTA_READY_SEQUENCE_STEP_MS = 110UL;
static constexpr bool ENABLE_DEEP_SLEEP = true;
static constexpr int PANEL_BATTERY_ADC_PIN = -1; // Configura aqui el pin ADC real del divisor de bateria
static constexpr float BAT_R1_OHM = 344800.0f;
static constexpr float BAT_R2_OHM = 994300.0f;
static constexpr float BAT_CAL_K = 1.000f;
static constexpr float BAT_CAL_B = 0.090f;
static constexpr float BAT_VMIN = 3.30f;
static constexpr float BAT_VMAX = 4.10f;
static constexpr int BAT_SAMPLES = 1;

static constexpr const char* OTA_WIFI_SSID = "elyisus1";
static constexpr const char* OTA_WIFI_PASS = "123456789";
static constexpr const char* OTA_UPDATE_PASSWORD = "12345678";
static constexpr int OTA_BTN_FIRE = PIN_BTN_FIRE;
static constexpr int OTA_BTN_MEDICAL = PIN_BTN_MEDICAL;

static constexpr int UART_BAUD_CODE = 3;
static constexpr int UART_PARITY_CODE = 0;
static constexpr int AIR_RATE_CODE = 2;
static constexpr int TRANS_MODE_CODE = 1;
static constexpr int PACKET_LEN_CODE = 0;
static constexpr int URXT_BYTE_TIME = 3;
static constexpr int POWER_CODE = 0;
static constexpr int KEY_CODE = 0;
static constexpr int LBT_CODE = 0;

struct ButtonConfig {
  int pin;
  int outputPin;
  uint8_t pwmChannel;
  const char* type;
  const char* label;
};

static constexpr ButtonConfig BUTTONS[] = {
  { PIN_BTN_FIRE, PIN_OUT_FIRE, PWM_CH_FIRE, "FIRE", "Incendio" },
  { PIN_BTN_ACCIDENT, PIN_OUT_ACCIDENT, PWM_CH_ACCIDENT, "ACCIDENT", "Accidente" },
  { PIN_BTN_EVAC, PIN_OUT_EVAC, PWM_CH_EVAC, "EVACUATION", "Evacuacion" },
  { PIN_BTN_MEDICAL, PIN_OUT_MEDICAL, PWM_CH_MEDICAL, "MEDICAL", "Auxilio medico" },
};

static constexpr size_t BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

enum PendingMessageKind : uint8_t {
  PENDING_NONE = 0,
  PENDING_EVENT = 1,
  PENDING_HEARTBEAT = 2
};

struct PendingTx {
  bool active = false;
  PendingMessageKind kind = PENDING_NONE;
  bool desiredState = false;
  int buttonIndex = -1;
  uint32_t seq = 0;
  uint32_t sendCount = 0;
  unsigned long createdMs = 0;
  unsigned long lastSendMs = 0;
  unsigned long lastLogMs = 0;
  String payload;
};

struct DelayedAlarmTrigger {
  bool active = false;
  int buttonIndex = -1;
  unsigned long dueMs = 0;
};

RTC_DATA_ATTR uint32_t rtcWakeCounter = 0;
RTC_DATA_ATTR uint32_t rtcNextSequence = 1;

String panelUid;
bool lastStableState[BUTTON_COUNT] = { false, false, false, false };
unsigned long lastStableChangeMs[BUTTON_COUNT] = { 0, 0, 0, 0 };
unsigned long lastInputScanMs = 0;
unsigned long allPressedSinceMs = 0;
bool holdResetTriggered = false;

String radioLine;
bool radioOverflow = false;
PendingTx pendingTx;
DelayedAlarmTrigger delayedAlarmTrigger;
unsigned long lastAcceptedAlarmPressMs = 0;
int currentActiveButton = -1;
unsigned long lastActivityMs = 0;
unsigned long nextHeartbeatDueMs = 0;
uint64_t lastWakeGpioMask = 0;
size_t offlineSequenceIndex = 0;
unsigned long offlineSequenceStepStartedMs = 0;
size_t otaReadySequenceIndex = 0;
unsigned long otaReadySequenceStepStartedMs = 0;
unsigned long otaComboSinceMs = 0;
unsigned long otaLastConnectAttemptMs = 0;
bool otaModeActive = false;
bool otaReady = false;
char otaHostname[24] = { 0 };
int lastBatteryPercent = -1;

bool isPressedRaw(int pin) {
  const int state = digitalRead(pin);
  return BUTTON_ACTIVE_LOW ? (state == LOW) : (state == HIGH);
}

void markActivity() {
  lastActivityMs = millis();
}

void setupPwmOutput(int pin, uint8_t channel) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(pin, PWM_FREQ, PWM_RESOLUTION, channel);
#else
  ledcSetup(channel, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(pin, channel);
#endif
}

void writePwmOutput(int pin, uint8_t channel, uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, duty);
#else
  ledcWrite(channel, duty);
#endif
}

uint8_t getPwmChannelByPin(int pin) {
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    if (BUTTONS[i].outputPin == pin) {
      return BUTTONS[i].pwmChannel;
    }
  }
  return 0;
}

void writeAlarmOutput(int pin, bool active) {
  const uint8_t channel = getPwmChannelByPin(pin);
  uint8_t duty = 0;

  if (OUTPUT_ACTIVE_HIGH) {
    duty = active ? PWM_DUTY_ACTIVE : 0;
  } else {
    duty = active ? 0 : PWM_DUTY_ACTIVE;
  }

  writePwmOutput(pin, channel, duty);
}

bool batteryMonitorConfigured() {
  return ENABLE_BATTERY_MONITOR && PANEL_BATTERY_ADC_PIN >= 0;
}

void setupBatteryADC() {
  if (!batteryMonitorConfigured()) return;
  analogReadResolution(12);
  analogSetPinAttenuation(PANEL_BATTERY_ADC_PIN, ADC_11db);
  pinMode(PANEL_BATTERY_ADC_PIN, INPUT);
}

float readBatteryVoltage() {
  if (!batteryMonitorConfigured()) return NAN;

  uint32_t acc = 0;
  for (int i = 0; i < BAT_SAMPLES; ++i) {
    acc += static_cast<uint32_t>(analogReadMilliVolts(PANEL_BATTERY_ADC_PIN));
    delay(2);
  }

  float mvAdc = static_cast<float>(acc) / static_cast<float>(BAT_SAMPLES);
  float vAdc = mvAdc / 1000.0f;
  float vBat = vAdc * ((BAT_R1_OHM + BAT_R2_OHM) / BAT_R2_OHM);
  vBat = vBat * BAT_CAL_K + BAT_CAL_B;

  if (vBat < 0.0f) vBat = 0.0f;
  if (vBat > 6.0f) vBat = 6.0f;
  return vBat;
}

float batteryPercentFromVoltage(float vbat) {
  float pct = (vbat - BAT_VMIN) / (BAT_VMAX - BAT_VMIN);
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 1.0f) pct = 1.0f;
  return pct * 100.0f;
}

int readBatteryPercent() {
  if (!batteryMonitorConfigured()) return -1;
  const float vbat = readBatteryVoltage();
  if (!isfinite(vbat)) return -1;
  const float pct = batteryPercentFromVoltage(vbat);
  int result = static_cast<int>(lroundf(pct));
  if (result < 0) result = 0;
  if (result > 100) result = 100;
  return result;
}

void updateAlarmOutputs() {
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    writeAlarmOutput(BUTTONS[i].outputPin, static_cast<int>(i) == currentActiveButton);
  }
}

void resetOfflineLinkSequence() {
  offlineSequenceIndex = 0;
  offlineSequenceStepStartedMs = 0;
}

void resetOtaReadySequence() {
  otaReadySequenceIndex = 0;
  otaReadySequenceStepStartedMs = 0;
}

void disconnectOtaWifi() {
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
  }
}

bool shouldAnimateOfflineLink(unsigned long now) {
  if (currentActiveButton >= 0) return false;
  if (!pendingTx.active || pendingTx.kind != PENDING_HEARTBEAT) return false;
  return (now - pendingTx.createdMs) >= OFFLINE_SEQUENCE_START_MS;
}

void applyOfflineLinkSequence() {
  const uint8_t phase = static_cast<uint8_t>(offlineSequenceIndex % 4);
  const bool fireAndMedicalOn = (phase == 0);
  const bool accidentAndEvacOn = (phase == 2);

  writeAlarmOutput(PIN_OUT_FIRE, fireAndMedicalOn);
  writeAlarmOutput(PIN_OUT_MEDICAL, fireAndMedicalOn);
  writeAlarmOutput(PIN_OUT_ACCIDENT, accidentAndEvacOn);
  writeAlarmOutput(PIN_OUT_EVAC, accidentAndEvacOn);
}

void applyOtaReadySequence() {
  const uint8_t phase = static_cast<uint8_t>(otaReadySequenceIndex % 6);
  const bool fireAndMedicalOn = (phase % 2) == 0;
  const bool accidentAndEvacOn = phase < 3;

  writeAlarmOutput(PIN_OUT_FIRE, fireAndMedicalOn);
  writeAlarmOutput(PIN_OUT_MEDICAL, fireAndMedicalOn);
  writeAlarmOutput(PIN_OUT_ACCIDENT, accidentAndEvacOn);
  writeAlarmOutput(PIN_OUT_EVAC, accidentAndEvacOn);
}

void serviceOfflineLinkSequence(unsigned long now) {
  if (!shouldAnimateOfflineLink(now)) {
    if (offlineSequenceStepStartedMs != 0 || offlineSequenceIndex != 0) {
      resetOfflineLinkSequence();
      updateAlarmOutputs();
    }
    return;
  }

  if (offlineSequenceStepStartedMs == 0) {
    offlineSequenceStepStartedMs = now;
    offlineSequenceIndex = 0;
    applyOfflineLinkSequence();
    return;
  }

  if ((now - offlineSequenceStepStartedMs) < OFFLINE_SEQUENCE_STEP_MS) return;

  offlineSequenceStepStartedMs = now;
  offlineSequenceIndex = (offlineSequenceIndex + 1) % 4;
  applyOfflineLinkSequence();
}

bool shouldAnimateOtaReady() {
  return otaModeActive && otaReady && WiFi.status() == WL_CONNECTED;
}

void serviceOtaReadySequence(unsigned long now) {
  if (!shouldAnimateOtaReady()) {
    if (otaReadySequenceStepStartedMs != 0 || otaReadySequenceIndex != 0) {
      resetOtaReadySequence();
      updateAlarmOutputs();
    }
    return;
  }

  if (otaReadySequenceStepStartedMs == 0) {
    otaReadySequenceStepStartedMs = now;
    otaReadySequenceIndex = 0;
    applyOtaReadySequence();
    return;
  }

  if ((now - otaReadySequenceStepStartedMs) < OTA_READY_SEQUENCE_STEP_MS) return;

  otaReadySequenceStepStartedMs = now;
  otaReadySequenceIndex = (otaReadySequenceIndex + 1) % 6;
  applyOtaReadySequence();
}

void setModeConfig() {
  digitalWrite(PIN_E220_M0M1, HIGH);
  delay(80);
}

void setModeNormal() {
  digitalWrite(PIN_E220_M0M1, LOW);
  delay(80);
}

void clearRadioInput() {
  while (Serial1.available() > 0) {
    Serial1.read();
  }
}

String readATResponse(uint32_t timeoutMs = 800) {
  String out;
  uint32_t t0 = millis();

  while ((millis() - t0) < timeoutMs) {
    while (Serial1.available() > 0) {
      const char c = static_cast<char>(Serial1.read());
      out += c;
      t0 = millis();
    }
  }

  out.trim();
  return out;
}

bool sendAT(const String& cmd, const char* expected = "=OK", uint32_t timeoutMs = 800) {
  clearRadioInput();

  Serial.print("[E220][TX] ");
  Serial.println(cmd);

  Serial1.print(cmd);
  Serial1.print("\r\n");

  const String resp = readATResponse(timeoutMs);
  Serial.print("[E220][RX] ");
  Serial.println(resp.length() ? resp : "<sin respuesta>");

  if (!expected || expected[0] == '\0') return true;
  return resp.indexOf(expected) >= 0;
}

bool queryAT(const String& cmd, uint32_t timeoutMs = 800) {
  clearRadioInput();

  Serial.print("[E220][TX] ");
  Serial.println(cmd);

  Serial1.print(cmd);
  Serial1.print("\r\n");

  const String resp = readATResponse(timeoutMs);
  Serial.print("[E220][RX] ");
  Serial.println(resp.length() ? resp : "<sin respuesta>");

  return resp.length() > 0;
}

bool configureE220() {
  bool ok = true;

  Serial.println("[E220] Entrando en modo configuracion...");

  ok &= queryAT("AT+DEVTYPE=?");
  ok &= queryAT("AT+FWCODE=?");
  ok &= sendAT("AT+ADDR=" + String(THIS_MODULE_ADDR));
  ok &= sendAT("AT+CHANNEL=" + String(LORA_CHANNEL));
  ok &= sendAT("AT+UART=" + String(UART_BAUD_CODE) + "," + String(UART_PARITY_CODE));
  ok &= sendAT("AT+RATE=" + String(AIR_RATE_CODE));
  ok &= sendAT("AT+TRANS=" + String(TRANS_MODE_CODE));
  ok &= sendAT("AT+PACKET=" + String(PACKET_LEN_CODE));
  ok &= sendAT("AT+URXT=" + String(URXT_BYTE_TIME));
  ok &= sendAT("AT+KEY=" + String(KEY_CODE));
  ok &= sendAT("AT+LBT=" + String(LBT_CODE));
  ok &= sendAT("AT+POWER=" + String(POWER_CODE));

  Serial.printf("[E220] Configuracion final: %s\n", ok ? "OK" : "ERROR");
  return ok;
}

bool bootConfigureIfEnabled() {
  if (!AUTO_CONFIGURE_ON_BOOT) {
    return true;
  }

  setModeConfig();
  delay(150);

  Serial1.begin(9600, SERIAL_8N1, PIN_E220_RX, PIN_E220_TX);
  delay(200);

  const bool ok = configureE220();

  setModeNormal();
  delay(150);

  Serial1.end();
  delay(50);

  return ok;
}

String getFieldValue(const String& line, const String& key) {
  const String needle = key + "=";
  const int start = line.indexOf(needle);
  if (start < 0) return "";

  const int valueStart = start + needle.length();
  int end = line.indexOf(',', valueStart);
  if (end < 0) end = line.length();
  return line.substring(valueStart, end);
}

bool parseUInt32Field(const String& value, uint32_t& out) {
  if (value.isEmpty()) return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') return false;
  out = static_cast<uint32_t>(parsed);
  return true;
}

String normalizeUid(String uid) {
  String out;
  out.reserve(uid.length());
  for (size_t i = 0; i < uid.length(); ++i) {
    const char c = uid[i];
    if (isAlphaNumeric(static_cast<unsigned char>(c))) {
      out += static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
  }
  return out;
}

uint16_t crc16Ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x8000) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

String formatHex16(uint16_t value) {
  char buffer[5];
  snprintf(buffer, sizeof(buffer), "%04X", value);
  return String(buffer);
}

bool parseHex16(String text, uint16_t& out) {
  text.trim();
  if (text.length() != 4) return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(text.c_str(), &end, 16);
  if (end == text.c_str() || *end != '\0' || parsed > 0xFFFFUL) return false;
  out = static_cast<uint16_t>(parsed);
  return true;
}

String buildProtocolFrame(const String& body) {
  const uint16_t crc = crc16Ccitt(reinterpret_cast<const uint8_t*>(body.c_str()), body.length());
  return String("!") + body + "*" + formatHex16(crc);
}

bool extractProtocolBody(String line, String& bodyOut) {
  line.trim();
  const int start = line.indexOf('!');
  if (start < 0) return false;
  const int star = line.indexOf('*', start + 1);
  if (star < 0) return false;

  const String body = line.substring(start + 1, star);
  String crcText = line.substring(star + 1);
  crcText.trim();
  if (crcText.length() < 4) return false;
  crcText = crcText.substring(0, 4);

  uint16_t expected = 0;
  if (!parseHex16(crcText, expected)) return false;

  const uint16_t actual = crc16Ccitt(reinterpret_cast<const uint8_t*>(body.c_str()), body.length());
  if (actual != expected) return false;

  bodyOut = body;
  return true;
}

int splitCsvFields(const String& text, String fields[], int maxFields) {
  int count = 0;
  int start = 0;
  while (count < maxFields) {
    const int comma = text.indexOf(',', start);
    if (comma < 0) {
      fields[count++] = text.substring(start);
      break;
    }
    fields[count++] = text.substring(start, comma);
    start = comma + 1;
  }
  return count;
}

bool parseProtocolFrame(String line, char& cmdOut, String fields[], int& fieldCountOut) {
  String body;
  if (!extractProtocolBody(line, body)) return false;
  fieldCountOut = splitCsvFields(body, fields, 8);
  if (fieldCountOut <= 0 || fields[0].isEmpty()) return false;
  cmdOut = fields[0].charAt(0);
  return true;
}

uint8_t buttonIndexToTypeCode(int buttonIndex) {
  if (buttonIndex < 0 || buttonIndex >= static_cast<int>(BUTTON_COUNT)) return 0;
  return static_cast<uint8_t>(buttonIndex + 1);
}

int typeCodeToButtonIndex(uint8_t typeCode) {
  if (typeCode == 0 || typeCode > BUTTON_COUNT) return -1;
  return static_cast<int>(typeCode - 1);
}

String protocolTypeLabel(uint8_t typeCode) {
  const int index = typeCodeToButtonIndex(typeCode);
  if (index < 0) return "";
  return String(BUTTONS[index].type);
}

String readPanelUid() {
  uint8_t mac[6] = { 0 };
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    return "000000000000";
  }

  char buffer[13];
  snprintf(buffer,
           sizeof(buffer),
           "%02X%02X%02X%02X%02X%02X",
           mac[0],
           mac[1],
           mac[2],
           mac[3],
           mac[4],
           mac[5]);
  return String(buffer);
}

String panelShortUid() {
  if (panelUid.length() <= 4) return panelUid;
  return panelUid.substring(panelUid.length() - 4);
}

String debugRadioText(const String& text) {
  String out;
  out.reserve(text.length() * 4);
  for (size_t i = 0; i < text.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(text.charAt(i));
    if (c >= 32 && c <= 126) {
      out += static_cast<char>(c);
    } else {
      char buf[5];
      snprintf(buf, sizeof(buf), "\\x%02X", c);
      out += buf;
    }
  }
  return out;
}

String friendlyAlarmType(String type) {
  type.trim();
  type.toUpperCase();
  if (type == "FIRE") return "Incendio";
  if (type == "ACCIDENT") return "Accidente";
  if (type == "EVACUATION") return "Evacuacion";
  if (type == "MEDICAL") return "Auxilio medico";
  return type;
}

void sendAddressed(uint16_t addr, const String& msg) {
  const uint8_t addrH = (addr >> 8) & 0xFF;
  const uint8_t addrL = addr & 0xFF;
  Serial1.write(addrH);
  Serial1.write(addrL);
  Serial1.write(LORA_CHANNEL);
  Serial1.print(msg);
  Serial1.print("\n");
  Serial1.flush();
  delay(25);
  markActivity();
}

String buildEventMessage(int buttonIndex, bool state, uint32_t seq) {
  const uint8_t typeCode = buttonIndexToTypeCode(buttonIndex);
  const String body = String("E,") + String(THIS_MODULE_ADDR)
                    + "," + String(typeCode)
                    + "," + (state ? "1" : "0")
                    + "," + String(seq);
  return buildProtocolFrame(body);
}

String buildHeartbeatMessage(uint32_t seq) {
  const uint8_t typeCode = buttonIndexToTypeCode(currentActiveButton);
  lastBatteryPercent = readBatteryPercent();
  String body = String("H,") + String(THIS_MODULE_ADDR)
              + "," + (currentActiveButton >= 0 ? "1" : "0")
              + "," + String(typeCode)
              + "," + String(seq);
  if (lastBatteryPercent >= 0) {
    body += "," + String(lastBatteryPercent);
  }
  return buildProtocolFrame(body);
}

String buildAckMessage(char ackFor, uint32_t seq) {
  const String body = String("K,") + String(THIS_MODULE_ADDR)
                    + "," + String(ackFor)
                    + "," + String(seq);
  return buildProtocolFrame(body);
}

void sendPendingTx(unsigned long now) {
  if (!pendingTx.active) return;

  sendAddressed(ALARM_MODULE_ADDR, pendingTx.payload);
  pendingTx.lastSendMs = now;
  pendingTx.sendCount++;

  if (pendingTx.kind == PENDING_EVENT) {
    PANEL_DEBUG_LOGF("[PANEL][TX #%lu] %s\n",
                     static_cast<unsigned long>(pendingTx.sendCount),
                     pendingTx.payload.c_str());
  } else if (pendingTx.kind == PENDING_HEARTBEAT) {
    PANEL_DEBUG_LOGF("[PANEL][HB TX #%lu] uid=%s seq=%lu\n",
                     static_cast<unsigned long>(pendingTx.sendCount),
                     panelUid.c_str(),
                     static_cast<unsigned long>(pendingTx.seq));
  }
}

uint32_t nextSequence() {
  return rtcNextSequence++;
}

void queuePendingEvent(int buttonIndex, bool state) {
  if (buttonIndex < 0 || buttonIndex >= static_cast<int>(BUTTON_COUNT)) return;

  pendingTx.active = true;
  pendingTx.kind = PENDING_EVENT;
  pendingTx.desiredState = state;
  pendingTx.buttonIndex = buttonIndex;
  pendingTx.seq = nextSequence();
  pendingTx.sendCount = 0;
  pendingTx.createdMs = millis();
  pendingTx.lastSendMs = 0;
  pendingTx.lastLogMs = pendingTx.createdMs;
  pendingTx.payload = buildEventMessage(buttonIndex, state, pendingTx.seq);

  currentActiveButton = state ? buttonIndex : -1;
  updateAlarmOutputs();
  markActivity();
  sendPendingTx(pendingTx.createdMs);
}

void queueHeartbeat() {
  if (pendingTx.active) return;

  pendingTx.active = true;
  pendingTx.kind = PENDING_HEARTBEAT;
  pendingTx.desiredState = (currentActiveButton >= 0);
  pendingTx.buttonIndex = currentActiveButton;
  pendingTx.seq = nextSequence();
  pendingTx.sendCount = 0;
  pendingTx.createdMs = millis();
  pendingTx.lastSendMs = 0;
  pendingTx.lastLogMs = pendingTx.createdMs;
  pendingTx.payload = buildHeartbeatMessage(pendingTx.seq);

  resetOfflineLinkSequence();
  markActivity();
  sendPendingTx(pendingTx.createdMs);
}

bool triggerAlarm(int index) {
  if (index < 0 || index >= static_cast<int>(BUTTON_COUNT)) return false;
  if (pendingTx.active) return false;
  if (currentActiveButton == index) return false;

  const unsigned long now = millis();
  if (lastAcceptedAlarmPressMs > 0 && (now - lastAcceptedAlarmPressMs) < MULTI_PRESS_GUARD_MS) {
    return false;
  }

  if (currentActiveButton >= 0 && currentActiveButton != index) {
    delayedAlarmTrigger.active = true;
    delayedAlarmTrigger.buttonIndex = index;
    delayedAlarmTrigger.dueMs = now + MULTI_PRESS_GUARD_MS;
    return false;
  }

  lastAcceptedAlarmPressMs = now;
  queuePendingEvent(index, true);
  return true;
}

void resetAlarm() {
  if (currentActiveButton < 0 || currentActiveButton >= static_cast<int>(BUTTON_COUNT)) return;
  delayedAlarmTrigger = DelayedAlarmTrigger();
  queuePendingEvent(currentActiveButton, false);
}

int typeToButtonIndex(String type) {
  type.trim();
  type.toUpperCase();
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    if (type.equalsIgnoreCase(BUTTONS[i].type)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void clearAlarmLocallyFromCentral(int buttonIndex) {
  if (buttonIndex < 0 || buttonIndex >= static_cast<int>(BUTTON_COUNT)) return;

  currentActiveButton = -1;
  delayedAlarmTrigger = DelayedAlarmTrigger();
  holdResetTriggered = false;
  allPressedSinceMs = 0;
  updateAlarmOutputs();
  nextHeartbeatDueMs = millis() + HEARTBEAT_INTERVAL_MS;
  markActivity();

  Serial.printf("[PANEL][SYNC] %s desactivada desde central\n", BUTTONS[buttonIndex].label);
}

void startOtaWifiConnect() {
  otaLastConnectAttemptMs = millis();
  otaReady = false;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASS);
  Serial.printf("[PANEL][OTA] Conectando a WiFi %s...\n", OTA_WIFI_SSID);
}

void enterOtaMode() {
  if (otaModeActive) return;

  otaModeActive = true;
  otaComboSinceMs = 0;
  pendingTx = PendingTx();
  delayedAlarmTrigger = DelayedAlarmTrigger();
  resetOfflineLinkSequence();
  resetOtaReadySequence();
  updateAlarmOutputs();
  markActivity();

  snprintf(otaHostname, sizeof(otaHostname), "panel-%u", THIS_MODULE_ADDR);
  Serial.printf("[PANEL][OTA] Modo OTA activado | host=%s\n", otaHostname);
  startOtaWifiConnect();
}

void serviceOtaTrigger(unsigned long now) {
  if (otaModeActive) return;
  if (!shouldAnimateOfflineLink(now)) {
    otaComboSinceMs = 0;
    return;
  }

  if (!areOtaButtonsPressedRaw()) {
    otaComboSinceMs = 0;
    return;
  }

  if (otaComboSinceMs == 0) {
    otaComboSinceMs = now;
    Serial.println("[PANEL][OTA] FIRE + MEDICAL detectados. Mantener 6s para entrar en OTA.");
    return;
  }

  if ((now - otaComboSinceMs) >= OTA_HOLD_MS) {
    enterOtaMode();
  }
}

void serviceOtaMode(unsigned long now) {
  if (!otaModeActive) return;

  markActivity();

  if (WiFi.status() != WL_CONNECTED) {
    if (otaLastConnectAttemptMs == 0 || (now - otaLastConnectAttemptMs) >= OTA_WIFI_RETRY_MS) {
      startOtaWifiConnect();
    }
    return;
  }

  if (!otaReady) {
    ArduinoOTA.setHostname(otaHostname);
    ArduinoOTA.setPassword(OTA_UPDATE_PASSWORD);
    ArduinoOTA
      .onStart([]() {
        Serial.println("[PANEL][OTA] Inicio de actualizacion");
      })
      .onEnd([]() {
        Serial.println("\n[PANEL][OTA] Actualizacion completada");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        static unsigned int lastPercent = 101;
        const unsigned int percent = total == 0 ? 0U : static_cast<unsigned int>((progress * 100U) / total);
        if (percent != lastPercent && (percent % 10U) == 0U) {
          Serial.printf("[PANEL][OTA] Progreso %u%%\n", percent);
          lastPercent = percent;
        }
      })
      .onError([](ota_error_t error) {
        Serial.printf("[PANEL][OTA] Error[%u]\n", static_cast<unsigned>(error));
      });
    ArduinoOTA.begin();
    otaReady = true;
    Serial.printf("[PANEL][OTA] Listo | host=%s | ip=%s | pass=%s\n",
                  otaHostname,
                  WiFi.localIP().toString().c_str(),
                  OTA_UPDATE_PASSWORD);
  }

  serviceOtaReadySequence(now);
  ArduinoOTA.handle();
}

void processEventAckLine(String line) {
  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'K' || fieldCount < 4) return;

  uint32_t addrValue = 0;
  if (!parseUInt32Field(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (static_cast<uint16_t>(addrValue) != THIS_MODULE_ADDR) return;
  if (fields[2].length() != 1 || fields[2].charAt(0) != 'E') return;

  uint32_t seq = 0;
  if (!parseUInt32Field(fields[3], seq)) return;
  if (!pendingTx.active || pendingTx.kind != PENDING_EVENT || seq != pendingTx.seq) return;

  const String type = (pendingTx.buttonIndex >= 0 && pendingTx.buttonIndex < static_cast<int>(BUTTON_COUNT))
                        ? String(BUTTONS[pendingTx.buttonIndex].label)
                        : String("Alarma");
  const char* stateText = pendingTx.desiredState ? "activada" : "liberada";

  Serial.printf("[PANEL][ACK] %s %s | seq=%lu | %lums | envios=%lu\n",
                type.c_str(),
                stateText,
                static_cast<unsigned long>(seq),
                millis() - pendingTx.createdMs,
                static_cast<unsigned long>(pendingTx.sendCount));

  pendingTx = PendingTx();
  nextHeartbeatDueMs = millis() + HEARTBEAT_INTERVAL_MS;
  markActivity();
}

void processHeartbeatAckLine(String line) {
  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'K' || fieldCount < 4) return;

  uint32_t addrValue = 0;
  if (!parseUInt32Field(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (static_cast<uint16_t>(addrValue) != THIS_MODULE_ADDR) return;
  if (fields[2].length() != 1 || fields[2].charAt(0) != 'H') return;

  uint32_t seq = 0;
  if (!parseUInt32Field(fields[3], seq)) return;
  if (!pendingTx.active || pendingTx.kind != PENDING_HEARTBEAT || seq != pendingTx.seq) return;

  Serial.printf("[PANEL][HB ACK] uid=%s seq=%lu | %lums | envios=%lu\n",
                panelUid.c_str(),
                static_cast<unsigned long>(seq),
                millis() - pendingTx.createdMs,
                static_cast<unsigned long>(pendingTx.sendCount));

  pendingTx = PendingTx();
  resetOfflineLinkSequence();
  updateAlarmOutputs();
  nextHeartbeatDueMs = millis() + HEARTBEAT_INTERVAL_MS;
  markActivity();
}

void processSyncLine(String line) {
  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'S' || fieldCount < 5) return;

  uint32_t addrValue = 0;
  if (!parseUInt32Field(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (static_cast<uint16_t>(addrValue) != THIS_MODULE_ADDR) return;

  uint32_t typeCodeValue = 0;
  if (!parseUInt32Field(fields[2], typeCodeValue) || typeCodeValue > 0xFFUL) return;
  const int typeIndex = typeCodeToButtonIndex(static_cast<uint8_t>(typeCodeValue));
  if (typeIndex < 0) return;

  uint32_t stateValue = 0;
  if (!parseUInt32Field(fields[3], stateValue)) return;
  const bool active = stateValue != 0;

  uint32_t syncSeq = 0;
  if (!parseUInt32Field(fields[4], syncSeq)) return;
  if (active) return;

  const bool shouldClear = currentActiveButton == typeIndex;
  const bool canAcknowledge = shouldClear || currentActiveButton < 0;
  if (!canAcknowledge) return;

  if (shouldClear) {
    clearAlarmLocallyFromCentral(typeIndex);
  }

  sendAddressed(ALARM_MODULE_ADDR, buildAckMessage('S', syncSeq));
}

void handleRadioRx() {
  while (Serial1.available() > 0) {
    const char c = static_cast<char>(Serial1.read());
    if (c == '\r') continue;

    if (c == '\n') {
      if (!radioOverflow && !radioLine.isEmpty()) {
        String line = radioLine;
        line.trim();
        if (line != "SUCCESS") {
          PANEL_DEBUG_LOGF("[PANEL][RX RAW] %s\n", debugRadioText(line).c_str());
          processEventAckLine(line);
          processHeartbeatAckLine(line);
          processSyncLine(line);
        }
      }
      radioLine = "";
      radioOverflow = false;
      continue;
    }

    const uint8_t byteValue = static_cast<uint8_t>(c);
    if (byteValue < 32 || byteValue > 126) {
      continue;
    }

    if (radioOverflow) continue;
    if (radioLine.length() >= 220) {
      radioOverflow = true;
      radioLine = "";
      continue;
    }
    radioLine += c;
  }
}

void servicePendingTx(unsigned long now) {
  if (otaModeActive) return;
  if (!pendingTx.active) return;

  if ((now - pendingTx.lastSendMs) >= ACK_RETRY_MS) {
    sendPendingTx(now);
  }

  if ((now - pendingTx.lastLogMs) >= WAIT_LOG_MS) {
    pendingTx.lastLogMs = now;
    if (pendingTx.kind == PENDING_EVENT) {
      PANEL_DEBUG_LOGF("[PANEL][WAIT] seq=%lu esperando ACK (%lums)\n",
                       static_cast<unsigned long>(pendingTx.seq),
                       now - pendingTx.createdMs);
    } else if (pendingTx.kind == PENDING_HEARTBEAT) {
      PANEL_DEBUG_LOGF("[PANEL][HB WAIT] seq=%lu esperando ACK (%lums)\n",
                       static_cast<unsigned long>(pendingTx.seq),
                       now - pendingTx.createdMs);
    }
  }
}

size_t countPressedButtonsRaw() {
  size_t count = 0;
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    if (isPressedRaw(BUTTONS[i].pin)) count++;
  }
  return count;
}

bool areAnyButtonsPressedRaw() {
  return countPressedButtonsRaw() > 0;
}

bool areAllButtonsPressedRaw() {
  return countPressedButtonsRaw() == BUTTON_COUNT;
}

bool areOtaButtonsPressedRaw() {
  const bool firePressed = isPressedRaw(OTA_BTN_FIRE);
  const bool medicalPressed = isPressedRaw(OTA_BTN_MEDICAL);
  return firePressed && medicalPressed && countPressedButtonsRaw() == 2;
}

int getSinglePressedButtonRawIndex() {
  int foundIndex = -1;
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    if (!isPressedRaw(BUTTONS[i].pin)) continue;
    if (foundIndex >= 0) return -1;
    foundIndex = static_cast<int>(i);
  }
  return foundIndex;
}

void serviceDelayedAlarmTrigger(unsigned long now) {
  if (otaModeActive) return;
  if (!delayedAlarmTrigger.active) return;
  if (pendingTx.active) return;
  if (now < delayedAlarmTrigger.dueMs) return;

  const int index = delayedAlarmTrigger.buttonIndex;
  delayedAlarmTrigger = DelayedAlarmTrigger();

  if (index < 0 || index >= static_cast<int>(BUTTON_COUNT)) return;
  if (countPressedButtonsRaw() != 1) return;
  if (!isPressedRaw(BUTTONS[index].pin)) return;

  lastAcceptedAlarmPressMs = now;
  queuePendingEvent(index, true);
  Serial.printf("[PANEL][Boton] %s\n", BUTTONS[index].label);
}

void serviceHoldReset(unsigned long now) {
  if (otaModeActive) return;
  if (!areAllButtonsPressedRaw()) {
    allPressedSinceMs = 0;
    holdResetTriggered = false;
    return;
  }

  if (allPressedSinceMs == 0) {
    allPressedSinceMs = now;
    return;
  }

  if (!holdResetTriggered && (now - allPressedSinceMs) >= HOLD_RESET_MS) {
    holdResetTriggered = true;
    Serial.println("[PANEL][HOLD] 4 botones sostenidos 5s -> RESET");
    resetAlarm();
  }
}

void scanButtons() {
  if (otaModeActive) return;
  const unsigned long now = millis();
  if ((now - lastInputScanMs) < INPUT_SCAN_MS) return;

  lastInputScanMs = now;
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    const bool pressed = isPressedRaw(BUTTONS[i].pin);
    if (pressed == lastStableState[i]) continue;
    if ((now - lastStableChangeMs[i]) < DEBOUNCE_MS) continue;

    lastStableState[i] = pressed;
    lastStableChangeMs[i] = now;

    if (pressed) {
      if (triggerAlarm(static_cast<int>(i))) {
        Serial.printf("[PANEL][Boton] %s\n", BUTTONS[i].label);
      }
    }
  }
}

void serviceHeartbeat(unsigned long now) {
  if (otaModeActive) return;
  if (pendingTx.active) return;
  if (nextHeartbeatDueMs == 0 || now < nextHeartbeatDueMs) return;
  queueHeartbeat();
}

void printWakeReason() {
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  switch (wakeCause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("[PANEL][WAKE] Despertado por heartbeat");
      break;
    case ESP_SLEEP_WAKEUP_GPIO:
      lastWakeGpioMask = esp_sleep_get_gpio_wakeup_status();
      PANEL_DEBUG_LOGF("[PANEL][WAKE] Despertado por GPIO mask=0x%llX\n", lastWakeGpioMask);
      break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      Serial.println("[PANEL][WAKE] Arranque normal");
      break;
    default:
      PANEL_DEBUG_LOGF("[PANEL][WAKE] Causa=%d\n", static_cast<int>(wakeCause));
      break;
  }
}

int wakeMaskToSingleButtonIndex() {
  if (lastWakeGpioMask == 0) return -1;

  int foundIndex = -1;
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    const uint64_t bit = BUTTON_PIN_BITMASK(BUTTONS[i].pin);
    if ((lastWakeGpioMask & bit) == 0) continue;
    if (foundIndex >= 0) return -1;
    foundIndex = static_cast<int>(i);
  }
  return foundIndex;
}

void triggerWakeAlarmIfNeeded() {
  if (currentActiveButton >= 0 || pendingTx.active) return;

  int index = wakeMaskToSingleButtonIndex();
  if (index < 0) {
    index = getSinglePressedButtonRawIndex();
  }
  if (index < 0) return;

  if (triggerAlarm(index)) {
    Serial.printf("[PANEL][Wake] %s\n", BUTTONS[index].label);
  }
}

uint64_t buildValidWakeMask(bool logWarnings) {
  uint64_t mask = 0;
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    const gpio_num_t gpio = static_cast<gpio_num_t>(BUTTONS[i].pin);
    if (esp_sleep_is_valid_wakeup_gpio(gpio)) {
      mask |= BUTTON_PIN_BITMASK(BUTTONS[i].pin);
      continue;
    }
    if (logWarnings) {
      Serial.printf("[PANEL][SLEEP] GPIO %d (%s) no es valido para wakeup profundo, se omite\n",
                    BUTTONS[i].pin,
                    BUTTONS[i].label);
    }
  }
  return mask;
}

void prepareWakePinsForSleep() {
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    const gpio_num_t gpio = static_cast<gpio_num_t>(BUTTONS[i].pin);
    gpio_pullup_en(gpio);
    gpio_pulldown_dis(gpio);
    gpio_hold_en(gpio);
  }
}

void releaseWakePinHolds() {
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    gpio_hold_dis(static_cast<gpio_num_t>(BUTTONS[i].pin));
  }
}

void enterDeepSleep() {
  if (!ENABLE_DEEP_SLEEP) return;

  updateAlarmOutputs();
  Serial1.flush();

  Serial.println("[PANEL][SLEEP] Entrando en deep sleep");
  Serial.flush();

  const uint64_t wakeMask = buildValidWakeMask(true);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (wakeMask != 0) {
    esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_LOW);
  } else {
    Serial.println("[PANEL][SLEEP] No hay botones validos para wakeup GPIO profundo; solo quedara el timer.");
  }
  esp_sleep_enable_timer_wakeup(HEARTBEAT_WAKE_US);
  prepareWakePinsForSleep();

  delay(40);
  esp_deep_sleep_start();
}

void serviceSleep(unsigned long now) {
  if (otaModeActive) return;
  if (!ENABLE_DEEP_SLEEP) return;
  if (pendingTx.active) return;
  if (currentActiveButton >= 0) return;
  if (delayedAlarmTrigger.active) return;
  if (areAnyButtonsPressedRaw()) return;
  if ((now - lastActivityMs) < SLEEP_IDLE_MS) return;
  enterDeepSleep();
}

void printStatus() {
  Serial.println("\n=== PANEL LORA ===");
  Serial.printf("UID: %s | UID corto: %s\n", panelUid.c_str(), panelShortUid().c_str());
  Serial.printf("ADDR=%u DEST=%u CH=%u\n",
                THIS_MODULE_ADDR,
                ALARM_MODULE_ADDR,
                LORA_CHANNEL);
  Serial.printf("Activo: %s\n",
                currentActiveButton >= 0 ? BUTTONS[currentActiveButton].label : "Ninguno");

  if (pendingTx.active) {
    Serial.printf("Pendiente %s seq=%lu intentos=%lu\n",
                  pendingTx.kind == PENDING_HEARTBEAT ? "heartbeat" : "evento",
                  static_cast<unsigned long>(pendingTx.seq),
                  static_cast<unsigned long>(pendingTx.sendCount));
  } else {
    Serial.println("Pendiente: no");
  }
}

void processSerialCommand(String command) {
  command.trim();
  if (command.isEmpty()) return;

  markActivity();
  String upper = command;
  upper.toUpperCase();

  if (upper == "HELP") {
    Serial.println("\nComandos:");
    Serial.println("  FIRE");
    Serial.println("  ACCIDENT");
    Serial.println("  EVAC");
    Serial.println("  MEDICAL");
    Serial.println("  RESET");
    Serial.println("  HB");
    Serial.println("  OTA");
    Serial.println("  STATUS");
    Serial.println("  Reset fisico: mantener los 4 botones pulsados por 5 segundos");
    Serial.println("  OTA: en falla sin central, mantener FIRE + MEDICAL por 6 segundos");
    return;
  }

  if (upper == "STATUS") {
    printStatus();
    return;
  }

  if (upper == "HB") {
    if (!pendingTx.active) {
      queueHeartbeat();
    }
    return;
  }

  if (upper == "OTA") {
    enterOtaMode();
    return;
  }

  if (upper == "FIRE") {
    if (triggerAlarm(0)) Serial.printf("[PANEL][Boton] %s\n", BUTTONS[0].label);
    return;
  }
  if (upper == "ACCIDENT") {
    if (triggerAlarm(1)) Serial.printf("[PANEL][Boton] %s\n", BUTTONS[1].label);
    return;
  }
  if (upper == "EVAC") {
    if (triggerAlarm(2)) Serial.printf("[PANEL][Boton] %s\n", BUTTONS[2].label);
    return;
  }
  if (upper == "MEDICAL") {
    if (triggerAlarm(3)) Serial.printf("[PANEL][Boton] %s\n", BUTTONS[3].label);
    return;
  }
  if (upper == "RESET") {
    resetAlarm();
    return;
  }

  Serial.printf("[PANEL] Comando no reconocido: %s\n", command.c_str());
}

void readUsbSerial() {
  if (!Serial.available()) return;
  const String command = Serial.readStringUntil('\n');
  processSerialCommand(command);
}

void setup() {
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();

  Serial.begin(115200);
  delay(wakeCause == ESP_SLEEP_WAKEUP_GPIO ? 120 : 500);

  ++rtcWakeCounter;
  panelUid = readPanelUid();

  Serial.println("\n[PANEL] Iniciando panel de pulsadores LoRa");
  printWakeReason();
  Serial.printf("[PANEL] Wake count RTC: %lu\n", static_cast<unsigned long>(rtcWakeCounter));

  releaseWakePinHolds();

  pinMode(PIN_E220_M0M1, OUTPUT);
  const bool e220Ok = bootConfigureIfEnabled();
  Serial.printf("[E220] config=%s\n", e220Ok ? "OK" : "ERROR");
  setModeNormal();
  delay(100);
  Serial1.begin(9600, SERIAL_8N1, PIN_E220_RX, PIN_E220_TX);
  delay(100);

  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    setupPwmOutput(BUTTONS[i].outputPin, BUTTONS[i].pwmChannel);
    writeAlarmOutput(BUTTONS[i].outputPin, false);
    pinMode(BUTTONS[i].pin, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
    lastStableState[i] = isPressedRaw(BUTTONS[i].pin);
    lastStableChangeMs[i] = millis();
  }

  updateAlarmOutputs();
  markActivity();
  nextHeartbeatDueMs = millis() + 1000UL;

  Serial.printf("[PANEL] UID=%s | corto=%s\n", panelUid.c_str(), panelShortUid().c_str());
  Serial.printf("[PANEL] LoRa RX=%d TX=%d M0=%d @ 9600\n", PIN_E220_RX, PIN_E220_TX, PIN_E220_M0M1);
  Serial.printf("[PANEL] ADDR=%u DEST=%u CH=%u\n",
                THIS_MODULE_ADDR,
                ALARM_MODULE_ADDR,
                LORA_CHANNEL);
  Serial.printf("[PANEL] Salidas PWM alarma FIRE=%d ACCIDENT=%d EVAC=%d MEDICAL=%d | Duty=%u/255 | Freq=%luHz\n",
                PIN_OUT_FIRE,
                PIN_OUT_ACCIDENT,
                PIN_OUT_EVAC,
                PIN_OUT_MEDICAL,
                PWM_DUTY_ACTIVE,
                static_cast<unsigned long>(PWM_FREQ));
  if (batteryMonitorConfigured()) {
    setupBatteryADC();
    lastBatteryPercent = readBatteryPercent();
    Serial.printf("[PANEL] Bateria ADC=%d | inicial=%d%%\n",
                  PANEL_BATTERY_ADC_PIN,
                  lastBatteryPercent >= 0 ? lastBatteryPercent : 0);
  } else {
    Serial.println("[PANEL] Bateria: monitoreo desactivado (sin ADC configurado).");
  }
  Serial.println("[PANEL] Reset fisico: mantener los 4 botones pulsados por 5 segundos.");
  Serial.println("[PANEL] OTA en falla: mantener FIRE + MEDICAL pulsados por 6 segundos.");
  Serial.println("[PANEL] Escribe HELP en el monitor serial para pruebas manuales.");

  triggerWakeAlarmIfNeeded();

  if (!pendingTx.active && (wakeCause == ESP_SLEEP_WAKEUP_TIMER || wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED)) {
    queueHeartbeat();
  }
}

void loop() {
  const unsigned long now = millis();
  handleRadioRx();
  scanButtons();
  serviceHoldReset(now);
  serviceOtaTrigger(now);
  serviceDelayedAlarmTrigger(now);
  servicePendingTx(now);
  serviceHeartbeat(now);
  serviceOfflineLinkSequence(now);
  serviceOtaMode(now);
  readUsbSerial();
  serviceSleep(now);
}
