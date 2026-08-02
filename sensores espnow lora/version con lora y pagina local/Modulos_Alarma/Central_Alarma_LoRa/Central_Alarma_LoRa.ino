#include <Arduino.h>
#include "esp_task_wdt.h"

// =====================================================
// CENTRAL DE ALARMA LORA
// - Recibe tramas compactas !E y !H desde paneles
// - Responde ACK generico !K al panel origen
// - Reenvia estado !P y modulo !M al maestro
// - Gestiona CLEAR !C del maestro y sincroniza al panel con !S
// - Valida integridad con CRC16 y usa ADDR como identidad del panel
// =====================================================

static constexpr bool AUTO_CONFIGURE_ON_BOOT = false;
static constexpr bool LOG_DEBUG = false;
static constexpr bool ENABLE_LOCAL_INPUTS = false;

#define CENTRAL_DEBUG_LOGF(...) do { if (LOG_DEBUG) Serial.printf(__VA_ARGS__); } while (0)
#define CENTRAL_DEBUG_LOGLN(msg) do { if (LOG_DEBUG) Serial.println(msg); } while (0)

static constexpr uint16_t THIS_MODULE_ADDR = 2;
static constexpr uint16_t MASTER_ADDR = 1;
static constexpr uint8_t LORA_CHANNEL = 23;

static constexpr int PIN_E220_RX = 16;
static constexpr int PIN_E220_TX = 17;
static constexpr int PIN_E220_MOM1 = 14;

static constexpr int PIN_ALARM_MAIN = 15;
static constexpr int RELAY_PINS[4] = {23, 5, 4, 13};
static constexpr int INPUT_PINS[4] = {25, 26, 27, 33};
static constexpr int PATTERN_RELAY_INDICES[2] = {0, 1};  // Dos reles en paralelo para la salida de alarma
static constexpr size_t PATTERN_RELAY_COUNT = sizeof(PATTERN_RELAY_INDICES) / sizeof(PATTERN_RELAY_INDICES[0]);

static constexpr const char* ALARM_TYPES[4] = {
  "FIRE",
  "ACCIDENT",
  "EVACUATION",
  "MEDICAL"
};

static constexpr unsigned long MASTER_BURST_INTERVAL_MS = 180UL;
static constexpr uint8_t MASTER_BURST_COUNT = 3;
static constexpr unsigned long MASTER_HEARTBEAT_MS = 5000UL;
static constexpr size_t MAX_SOURCE_TRACK = 12;
static constexpr unsigned long PANEL_CONFIRM_SETTLE_MS = 800UL;
static constexpr unsigned long SYNC_ACK_RETRY_MS = 1000UL;
static constexpr unsigned long SYNC_WAIT_LOG_MS = 2000UL;
static constexpr uint8_t SYNC_CLEAR_BURST_COUNT = 5;
static constexpr unsigned long SYNC_CLEAR_INITIAL_DELAY_MS = 3000UL;
static constexpr unsigned long SYNC_CLEAR_BURST_INTERVAL_MS = 1000UL;
static constexpr uint8_t MASTER_CTRL_ACK_BURST_COUNT = 3;
static constexpr unsigned long MASTER_CTRL_ACK_BURST_INTERVAL_MS = 300UL;
static constexpr unsigned long MASTER_ACK_RETRY_MS = 5000UL;
static constexpr unsigned long MASTER_ACK_WAIT_LOG_MS = 5000UL;
static constexpr unsigned long MASTER_PUBLISH_START_DELAY_MS = 120UL;
static constexpr unsigned long INPUT_SCAN_MS = 12UL;
static constexpr unsigned long INPUT_DEBOUNCE_MS = 45UL;
static constexpr uint32_t WATCHDOG_TIMEOUT_MS = 8000UL;
// Patrones lentos para no castigar el rele.
// Los pasos alternan ON/OFF y siempre empiezan en ON.
// FIRE: 5s encendida, 2s apagada, continuo.
static constexpr uint16_t FIRE_PATTERN_STEPS_MS[] = {5000, 2000};
// ACCIDENT: 2 pulsos de 2s, pausas de 1s y pausa larga de 5s.
static constexpr uint16_t ACCIDENT_PATTERN_STEPS_MS[] = {2000, 1000, 2000, 5000};
// EVACUATION: 5 pulsos de 2s, pausas de 1s y pausa larga de 3s.
static constexpr uint16_t EVAC_PATTERN_STEPS_MS[] = {2000, 1000, 2000, 1000, 2000, 1000, 2000, 1000, 2000, 3000};
// MEDICAL: 2 pulsos de 2s, pausas de 1s y pausa larga de 8s.
static constexpr uint16_t MEDICAL_PATTERN_STEPS_MS[] = {2000, 1000, 2000, 8000};

static constexpr int UART_BAUD_CODE = 3;
static constexpr int UART_PARITY_CODE = 0;
static constexpr int AIR_RATE_CODE = 2;
static constexpr int TRANS_MODE_CODE = 1;
static constexpr int PACKET_LEN_CODE = 0;
static constexpr int URXT_BYTE_TIME = 3;
static constexpr int POWER_CODE = 0;
static constexpr int KEY_CODE = 0;
static constexpr int LBT_CODE = 0;

HardwareSerial RadioSerial(2);
String radioLine;
bool radioOverflow = false;

struct AlarmState {
  bool active = false;
  uint16_t sourceAddr = 0;
  String sourceUid;
  String type;
  String mode;
};

struct SourceTrack {
  bool used = false;
  uint16_t sourceAddr = 0;
  String sourceUid;
  uint32_t lastSeq = 0;
  int batteryPercent = -1;
};

struct PendingSyncMessage {
  bool active = false;
  uint16_t targetAddr = 0;
  String targetUid;
  String type;
  bool desiredState = false;
  uint32_t syncSeq = 0;
  uint32_t sendCount = 0;
  unsigned long createdMs = 0;
  unsigned long lastSendMs = 0;
  unsigned long lastLogMs = 0;
  String payload;
};

struct PendingMasterPanelMessage {
  bool active = false;
  String targetUid;
  bool publishAlarmModuleOnAck = false;
  uint32_t seq = 0;
  uint32_t sendCount = 0;
  unsigned long createdMs = 0;
  unsigned long lastSendMs = 0;
  unsigned long lastLogMs = 0;
  String payload;
};

enum PendingSourcePublishKind : uint8_t {
  PENDING_SOURCE_NONE = 0,
  PENDING_SOURCE_EVENT = 1,
  PENDING_SOURCE_HEARTBEAT = 2
};

struct PendingSourcePublish {
  bool active = false;
  PendingSourcePublishKind kind = PENDING_SOURCE_NONE;
  uint16_t sourceAddr = 0;
  String sourceUid;
  String type;
  bool activeState = false;
  uint32_t seq = 0;
  unsigned long quietUntilMs = 0;
};

struct LastControlAckState {
  bool valid = false;
  uint32_t reqId = 0;
  String uid;
  String type;
  bool ok = false;
  String reason;
};

struct AlarmPatternSpec {
  const uint16_t* steps = nullptr;
  uint8_t stepCount = 0;
};

AlarmState currentAlarm;
SourceTrack sourceTrack[MAX_SOURCE_TRACK];
String pendingMasterPayload;
uint8_t pendingMasterBursts = 0;
unsigned long nextMasterPublishMs = 0;
unsigned long lastMasterHeartbeatMs = 0;
PendingSyncMessage pendingSync;
PendingMasterPanelMessage pendingMasterPanel;
PendingSourcePublish pendingSourcePublish;
LastControlAckState lastControlAckState;
uint32_t nextSyncSequence = 1;
uint32_t nextMasterPanelSequence = 1;
uint32_t nextMasterModuleSequence = 1;
uint32_t currentMasterModuleSequence = 0;
bool masterAlarmPublishArmed = false;
bool relayStates[4] = {false, false, false, false};
bool lastStableInputStates[4] = {false, false, false, false};
unsigned long lastStableChangeMs[4] = {0, 0, 0, 0};
unsigned long lastInputScanMs = 0;
bool activePatternRelaysEnabled = false;
uint8_t activePatternStepIndex = 0;
unsigned long activePatternStepStartedMs = 0;

void initTaskWatchdog() {
  esp_task_wdt_config_t wdtConfig = {};
  wdtConfig.timeout_ms = WATCHDOG_TIMEOUT_MS;
  wdtConfig.idle_core_mask = 0;
  wdtConfig.trigger_panic = true;

  esp_err_t err = esp_task_wdt_init(&wdtConfig);
  if (err == ESP_OK) {
    Serial.printf("[ALARM][WDT] Inicializado (%lums)\n", static_cast<unsigned long>(WATCHDOG_TIMEOUT_MS));
  } else if (err == ESP_ERR_INVALID_STATE) {
    Serial.println("[ALARM][WDT] Ya estaba inicializado");
  } else {
    Serial.printf("[ALARM][WDT] Error init=%d\n", static_cast<int>(err));
  }

  err = esp_task_wdt_add(nullptr);
  if (err == ESP_OK) {
    Serial.println("[ALARM][WDT] Loop task suscrita");
  } else if (err == ESP_ERR_INVALID_STATE) {
    Serial.println("[ALARM][WDT] TWDT aun no disponible");
  } else {
    Serial.printf("[ALARM][WDT] Error add=%d\n", static_cast<int>(err));
  }
}

inline void feedTaskWatchdog() {
  esp_task_wdt_reset();
}

String debugRadioText(const String& input) {
  String out;
  out.reserve(input.length() * 4);

  for (size_t i = 0; i < input.length(); ++i) {
    const uint8_t b = static_cast<uint8_t>(input[i]);
    if (b == '\r') {
      out += "\\r";
    } else if (b == '\n') {
      out += "\\n";
    } else if (b >= 32 && b <= 126) {
      out += static_cast<char>(b);
    } else {
      char buf[5];
      snprintf(buf, sizeof(buf), "\\x%02X", b);
      out += buf;
    }
  }

  return out;
}

String getFieldValue(const String& line, const String& key) {
  const String needle = key + "=";
  int start = -1;
  if (line.startsWith(needle)) {
    start = 0;
  } else {
    start = line.indexOf("," + needle);
    if (start >= 0) start += 1;
  }
  if (start < 0) return "";

  const int valueStart = start + static_cast<int>(needle.length());
  int end = line.indexOf(',', valueStart);
  if (end < 0) end = line.length();
  return line.substring(valueStart, end);
}

bool parseActiveValue(String value, bool& out) {
  value.trim();
  value.toUpperCase();
  if (value == "1" || value == "ON" || value == "TRUE" || value == "ACTIVE") {
    out = true;
    return true;
  }
  if (value == "0" || value == "OFF" || value == "FALSE" || value == "CLEAR" || value == "IDLE") {
    out = false;
    return true;
  }
  return false;
}

bool parseUInt16Field(const String& value, uint16_t& out) {
  if (value.isEmpty()) return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || parsed > 0xFFFFUL) return false;
  out = static_cast<uint16_t>(parsed);
  return true;
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

String normalizeType(String rawType) {
  rawType.trim();
  rawType.toUpperCase();
  if (rawType == "INCENDIO") return "FIRE";
  if (rawType == "ACCIDENTE") return "ACCIDENT";
  if (rawType == "EVACUACION") return "EVACUATION";
  if (rawType == "AUXILIO" || rawType == "AUXILIO_MEDICO") return "MEDICAL";
  return rawType;
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

String panelKeyFromAddr(uint16_t addr) {
  return addr == 0 ? String("CENTRAL") : String(addr);
}

uint8_t alarmTypeToCode(const String& type) {
  if (type.equalsIgnoreCase("FIRE")) return 1;
  if (type.equalsIgnoreCase("ACCIDENT")) return 2;
  if (type.equalsIgnoreCase("EVACUATION")) return 3;
  if (type.equalsIgnoreCase("MEDICAL")) return 4;
  return 0;
}

String alarmTypeFromCode(uint8_t typeCode) {
  switch (typeCode) {
    case 1: return "FIRE";
    case 2: return "ACCIDENT";
    case 3: return "EVACUATION";
    case 4: return "MEDICAL";
    default: return "NONE";
  }
}

uint8_t alarmModeToCode(const String& mode) {
  if (mode.equalsIgnoreCase("PATRON_1")) return 1;
  if (mode.equalsIgnoreCase("PATRON_2")) return 2;
  if (mode.equalsIgnoreCase("PATRON_3")) return 3;
  if (mode.equalsIgnoreCase("PATRON_4")) return 4;
  return 0;
}

String alarmModeFromCode(uint8_t modeCode) {
  switch (modeCode) {
    case 1: return "PATRON_1";
    case 2: return "PATRON_2";
    case 3: return "PATRON_3";
    case 4: return "PATRON_4";
    default: return "PATRON_0";
  }
}

uint8_t panelReasonCode(const char* reason) {
  if (!reason) return 0;
  if (strcmp(reason, "event") == 0) return 1;
  if (strcmp(reason, "sync") == 0) return 2;
  if (strcmp(reason, "local") == 0) return 3;
  return 0;
}

String alarmModeForType(const String& type) {
  if (type == "FIRE") return "PATRON_1";
  if (type == "ACCIDENT") return "PATRON_2";
  if (type == "EVACUATION") return "PATRON_3";
  if (type == "MEDICAL") return "PATRON_4";
  return "PATRON_0";
}

void setModeConfig() {
  digitalWrite(PIN_E220_MOM1, HIGH);
  delay(80);
}

void setModeNormal() {
  digitalWrite(PIN_E220_MOM1, LOW);
  delay(80);
}

void clearRadioInput() {
  while (RadioSerial.available() > 0) {
    RadioSerial.read();
  }
}

String readATResponse(uint32_t timeoutMs = 800) {
  String out;
  uint32_t t0 = millis();

  while ((millis() - t0) < timeoutMs) {
    while (RadioSerial.available() > 0) {
      const char c = static_cast<char>(RadioSerial.read());
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

  RadioSerial.print(cmd);
  RadioSerial.print("\r\n");

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

  RadioSerial.print(cmd);
  RadioSerial.print("\r\n");

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

  RadioSerial.begin(9600, SERIAL_8N1, PIN_E220_RX, PIN_E220_TX);
  delay(200);

  const bool ok = configureE220();

  setModeNormal();
  delay(150);

  RadioSerial.end();
  delay(50);

  return ok;
}

int typeToRelayIndex(const String& type) {
  for (size_t i = 0; i < 4; ++i) {
    if (type.equalsIgnoreCase(ALARM_TYPES[i])) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

AlarmPatternSpec alarmPatternForType(const String& type) {
  if (type.equalsIgnoreCase("FIRE")) {
    return {FIRE_PATTERN_STEPS_MS, static_cast<uint8_t>(sizeof(FIRE_PATTERN_STEPS_MS) / sizeof(FIRE_PATTERN_STEPS_MS[0]))};
  }
  if (type.equalsIgnoreCase("ACCIDENT")) {
    return {ACCIDENT_PATTERN_STEPS_MS, static_cast<uint8_t>(sizeof(ACCIDENT_PATTERN_STEPS_MS) / sizeof(ACCIDENT_PATTERN_STEPS_MS[0]))};
  }
  if (type.equalsIgnoreCase("EVACUATION")) {
    return {EVAC_PATTERN_STEPS_MS, static_cast<uint8_t>(sizeof(EVAC_PATTERN_STEPS_MS) / sizeof(EVAC_PATTERN_STEPS_MS[0]))};
  }
  if (type.equalsIgnoreCase("MEDICAL")) {
    return {MEDICAL_PATTERN_STEPS_MS, static_cast<uint8_t>(sizeof(MEDICAL_PATTERN_STEPS_MS) / sizeof(MEDICAL_PATTERN_STEPS_MS[0]))};
  }
  return {};
}

void resetAlarmPatternState() {
  activePatternRelaysEnabled = false;
  activePatternStepIndex = 0;
  activePatternStepStartedMs = 0;
}

void setRelayPhysical(size_t index, bool on) {
  relayStates[index] = on;
  digitalWrite(RELAY_PINS[index], on ? LOW : HIGH);
}

void clearAllRelays() {
  for (size_t i = 0; i < 4; ++i) {
    setRelayPhysical(i, false);
  }
}

void setPatternRelaysPhysical(bool on) {
  for (size_t i = 0; i < PATTERN_RELAY_COUNT; ++i) {
    const int relayIndex = PATTERN_RELAY_INDICES[i];
    if (relayIndex >= 0 && relayIndex < 4) {
      setRelayPhysical(static_cast<size_t>(relayIndex), on);
    }
  }
}

void sendAddressed(uint16_t addr, const String& msg) {
  feedTaskWatchdog();
  const uint8_t addrH = (addr >> 8) & 0xFF;
  const uint8_t addrL = addr & 0xFF;
  RadioSerial.write(addrH);
  RadioSerial.write(addrL);
  RadioSerial.write(LORA_CHANNEL);
  RadioSerial.print(msg);
  RadioSerial.print("\n");
  RadioSerial.flush();
  delay(25);
  feedTaskWatchdog();
}

void clearAlarmOutputs() {
  digitalWrite(PIN_ALARM_MAIN, 1);
  clearAllRelays();
  resetAlarmPatternState();
}

void applyAlarmOutputs(const String& type, bool active) {
  clearAlarmOutputs();
  if (!active) return;

  digitalWrite(PIN_ALARM_MAIN, 0);
  if (PATTERN_RELAY_COUNT > 0) {
    activePatternRelaysEnabled = true;
    activePatternStepIndex = 0;
    activePatternStepStartedMs = millis();
    setPatternRelaysPhysical(true);
  }
}

void serviceAlarmPattern() {
  if (!currentAlarm.active) return;
  if (!activePatternRelaysEnabled) return;

  const unsigned long tick = millis();
  if (tick < activePatternStepStartedMs) {
    return;
  }

  const AlarmPatternSpec pattern = alarmPatternForType(currentAlarm.type);
  if (!pattern.steps || pattern.stepCount == 0) return;
  if (activePatternStepIndex >= pattern.stepCount) {
    activePatternStepIndex = 0;
    activePatternStepStartedMs = tick;
  }

  const unsigned long elapsed = tick - activePatternStepStartedMs;
  const unsigned long currentStepDuration = pattern.steps[activePatternStepIndex];
  if (elapsed < currentStepDuration) return;

  activePatternStepIndex = static_cast<uint8_t>((activePatternStepIndex + 1U) % pattern.stepCount);
  activePatternStepStartedMs = tick;
  const bool relayOn = (activePatternStepIndex % 2U) == 0U;
  setPatternRelaysPhysical(relayOn);
}

String buildMasterAlarmPayload(uint32_t seq) {
  const String body = String("M,") + String(currentAlarm.sourceAddr)
                    + "," + String(alarmTypeToCode(currentAlarm.type))
                    + "," + String(alarmModeToCode(currentAlarm.mode))
                    + "," + (currentAlarm.active ? "1" : "0")
                    + "," + String(seq);
  return buildProtocolFrame(body);
}

String buildMasterPanelPayload(uint16_t sourceAddr,
                               const String& sourceUid,
                               bool active,
                               const String& type,
                               uint32_t seq,
                               const char* reason) {
  String body = String("P,") + String(sourceAddr)
              + "," + (active ? "1" : "0")
              + "," + String(alarmTypeToCode(type))
              + "," + String(panelReasonCode(reason))
              + "," + String(seq);

  int batteryPercent = -1;
  for (size_t i = 0; i < MAX_SOURCE_TRACK; ++i) {
    if (!sourceTrack[i].used) continue;
    if (sourceTrack[i].sourceAddr == sourceAddr || sourceTrack[i].sourceUid.equalsIgnoreCase(sourceUid)) {
      batteryPercent = sourceTrack[i].batteryPercent;
      break;
    }
  }
  if (batteryPercent >= 0) {
    body += "," + String(batteryPercent);
  }
  return buildProtocolFrame(body);
}

void sendToMasterNow(const String& msg) {
  sendAddressed(MASTER_ADDR, msg);
  CENTRAL_DEBUG_LOGF("[ALARM][TX->MASTER] %s\n", msg.c_str());
}

void sendMasterControlAckBurst(const String& msg) {
  for (uint8_t i = 0; i < MASTER_CTRL_ACK_BURST_COUNT; ++i) {
    sendAddressed(MASTER_ADDR, msg);
    CENTRAL_DEBUG_LOGF("[ALARM][CTRL ACK->MASTER #%u] %s\n",
                       static_cast<unsigned>(i + 1),
                       msg.c_str());
    if ((i + 1) < MASTER_CTRL_ACK_BURST_COUNT) {
      feedTaskWatchdog();
      delay(MASTER_CTRL_ACK_BURST_INTERVAL_MS);
      feedTaskWatchdog();
    }
  }
}

void rememberLastControlAck(const String& uid, const String& type, bool ok, const String& reason, uint32_t reqId) {
  lastControlAckState.valid = reqId != 0;
  lastControlAckState.reqId = reqId;
  lastControlAckState.uid = normalizeUid(uid);
  lastControlAckState.type = normalizeType(type);
  lastControlAckState.ok = ok;
  lastControlAckState.reason = reason;
}

bool resendDuplicateControlAckIfNeeded(uint16_t targetAddr, const String& type, uint32_t reqId) {
  if (!lastControlAckState.valid || reqId == 0) return false;
  if (reqId != lastControlAckState.reqId) return false;

  const String cleanUid = panelKeyFromAddr(targetAddr);
  const String cleanType = normalizeType(type);
  if (!lastControlAckState.uid.isEmpty() && !cleanUid.isEmpty() && !lastControlAckState.uid.equalsIgnoreCase(cleanUid)) return false;
  if (!lastControlAckState.type.isEmpty() && !cleanType.isEmpty() && !lastControlAckState.type.equalsIgnoreCase(cleanType)) return false;

  CENTRAL_DEBUG_LOGF("[ALARM][CTRL DUP] REQ=%lu -> reenviando ACK anterior\n",
                     static_cast<unsigned long>(reqId));
  delay(MASTER_CTRL_ACK_BURST_INTERVAL_MS);
  sendMasterControlAckBurst(buildMasterControlAck(
    targetAddr,
    lastControlAckState.type.isEmpty() ? type : lastControlAckState.type,
    lastControlAckState.ok,
    lastControlAckState.reason,
    reqId));
  return true;
}

void sendPanelStatusToMaster(uint16_t sourceAddr,
                             const String& uid,
                             bool active,
                             const String& type,
                             const char* reason,
                             bool publishAlarmModuleOnAck = false) {
  if (uid.isEmpty()) return;
  pendingMasterPanel.active = true;
  pendingMasterPanel.targetUid = uid;
  pendingMasterPanel.publishAlarmModuleOnAck = publishAlarmModuleOnAck;
  pendingMasterPanel.seq = nextMasterPanelSequence++;
  pendingMasterPanel.sendCount = 0;
  pendingMasterPanel.createdMs = millis();
  pendingMasterPanel.lastSendMs = 0;
  pendingMasterPanel.lastLogMs = pendingMasterPanel.createdMs;
  pendingMasterPanel.payload = buildMasterPanelPayload(sourceAddr, uid, active, type, pendingMasterPanel.seq, reason);

  sendToMasterNow(pendingMasterPanel.payload);
  const unsigned long sentAt = millis();
  pendingMasterPanel.sendCount = 1;
  pendingMasterPanel.lastSendMs = sentAt;
}

void scheduleMasterPublish() {
  masterAlarmPublishArmed = true;
  currentMasterModuleSequence = nextMasterModuleSequence++;
  pendingMasterPayload = buildMasterAlarmPayload(currentMasterModuleSequence);
  pendingMasterBursts = MASTER_BURST_COUNT;
  nextMasterPublishMs = millis() + MASTER_PUBLISH_START_DELAY_MS;
}

void serviceMasterPublish(unsigned long now) {
  if (!masterAlarmPublishArmed) return;

  if (pendingMasterBursts > 0 && now >= nextMasterPublishMs) {
    sendToMasterNow(pendingMasterPayload);
    pendingMasterBursts--;
    nextMasterPublishMs = now + MASTER_BURST_INTERVAL_MS;
    lastMasterHeartbeatMs = now;
  }

  if (currentAlarm.active && pendingMasterBursts == 0 && (now - lastMasterHeartbeatMs) >= MASTER_HEARTBEAT_MS) {
    currentMasterModuleSequence = nextMasterModuleSequence++;
    pendingMasterPayload = buildMasterAlarmPayload(currentMasterModuleSequence);
    pendingMasterBursts = MASTER_BURST_COUNT;
    nextMasterPublishMs = now + MASTER_PUBLISH_START_DELAY_MS;
    lastMasterHeartbeatMs = now;
  }
}

void servicePendingMasterPanelAck(unsigned long now) {
  if (!pendingMasterPanel.active) return;
  const unsigned long tick = millis();

  if ((tick - pendingMasterPanel.lastSendMs) >= MASTER_ACK_RETRY_MS) {
    sendToMasterNow(pendingMasterPanel.payload);
    pendingMasterPanel.sendCount++;
    pendingMasterPanel.lastSendMs = tick;
  }

  if ((tick - pendingMasterPanel.lastLogMs) >= MASTER_ACK_WAIT_LOG_MS) {
    pendingMasterPanel.lastLogMs = tick;
    CENTRAL_DEBUG_LOGF("[ALARM][WAIT MASTER] panel=%s seq=%lu esperando ACK (%lums)\n",
                       pendingMasterPanel.targetUid.c_str(),
                       static_cast<unsigned long>(pendingMasterPanel.seq),
                       tick - pendingMasterPanel.createdMs);
  }
}

void scheduleSourcePublish(PendingSourcePublishKind kind,
                           uint16_t sourceAddr,
                           const String& sourceUid,
                           const String& type,
                           bool activeState,
                           uint32_t seq) {
  pendingSourcePublish.active = true;
  pendingSourcePublish.kind = kind;
  pendingSourcePublish.sourceAddr = sourceAddr;
  pendingSourcePublish.sourceUid = sourceUid;
  pendingSourcePublish.type = type;
  pendingSourcePublish.activeState = activeState;
  pendingSourcePublish.seq = seq;
  pendingSourcePublish.quietUntilMs = millis() + PANEL_CONFIRM_SETTLE_MS;
}

void servicePendingSourcePublish(unsigned long now) {
  if (!pendingSourcePublish.active) return;
  if ((long)(now - pendingSourcePublish.quietUntilMs) < 0) return;

  if (pendingSourcePublish.kind == PENDING_SOURCE_EVENT) {
    sendPanelStatusToMaster(pendingSourcePublish.sourceAddr,
                            pendingSourcePublish.sourceUid,
                            pendingSourcePublish.activeState,
                            pendingSourcePublish.type,
                            "event",
                            true);
  } else if (pendingSourcePublish.kind == PENDING_SOURCE_HEARTBEAT) {
    sendPanelStatusToMaster(pendingSourcePublish.sourceAddr,
                            pendingSourcePublish.sourceUid,
                            pendingSourcePublish.activeState,
                            pendingSourcePublish.type,
                            "heartbeat");
  }

  pendingSourcePublish = PendingSourcePublish();
}

String buildAckMessage(uint16_t addr, char ackFor, uint32_t seq) {
  const String body = String("K,") + String(addr)
                    + "," + String(ackFor)
                    + "," + String(seq);
  return buildProtocolFrame(body);
}

void sendEventAck(uint16_t sourceAddr, uint32_t seq) {
  if (sourceAddr == 0) return;
  const String ack = buildAckMessage(sourceAddr, 'E', seq);
  sendAddressed(sourceAddr, ack);
  CENTRAL_DEBUG_LOGF("[ALARM][ACK->%u] %s\n", sourceAddr, ack.c_str());
}

void sendHeartbeatAck(uint16_t sourceAddr, uint32_t seq) {
  if (sourceAddr == 0) return;
  const String ack = buildAckMessage(sourceAddr, 'H', seq);
  sendAddressed(sourceAddr, ack);
  CENTRAL_DEBUG_LOGF("[ALARM][HB ACK->%u] %s\n", sourceAddr, ack.c_str());
}

String buildPanelSyncMessage(uint16_t addr, const String& type, bool active, uint32_t syncSeq) {
  const String body = String("S,") + String(addr)
                    + "," + String(alarmTypeToCode(type))
                    + "," + (active ? "1" : "0")
                    + "," + String(syncSeq);
  return buildProtocolFrame(body);
}

uint8_t controlResultCode(bool ok, const String& reason) {
  if (ok) return 1;
  if (reason.equalsIgnoreCase("uid_mismatch")) return 2;
  if (reason.equalsIgnoreCase("type_mismatch")) return 3;
  return 0;
}

String buildMasterControlAck(uint16_t addr, const String& type, bool ok, const String& reason, uint32_t req) {
  const String body = String("U,") + String(addr)
                    + "," + String(alarmTypeToCode(type))
                    + "," + String(controlResultCode(ok, reason))
                    + "," + String(req);
  return buildProtocolFrame(body);
}

void schedulePanelSync(uint16_t targetAddr, const String& uid, const String& type, bool active) {
  if (targetAddr == 0 || uid.isEmpty()) return;

  const uint32_t syncSeq = nextSyncSequence++;
  pendingSync.active = true;
  pendingSync.targetAddr = targetAddr;
  pendingSync.targetUid = uid;
  pendingSync.type = type;
  pendingSync.desiredState = active;
  pendingSync.syncSeq = syncSeq;
  pendingSync.sendCount = 0;
  pendingSync.createdMs = millis();
  pendingSync.lastSendMs = 0;
  pendingSync.lastLogMs = pendingSync.createdMs;
  pendingSync.payload = buildPanelSyncMessage(targetAddr, type, active, syncSeq);

  Serial.printf("[ALARM][SYNC] %s -> %s (%u)\n",
                active ? "Activacion" : "Desactivacion",
                uid.c_str(),
                targetAddr);
}

void servicePendingSync(unsigned long now) {
  if (!pendingSync.active) return;
  const unsigned long tick = millis();

  if (!pendingSync.desiredState) {
    const unsigned long sinceCreated = tick - pendingSync.createdMs;
    const bool initialDelayDone = sinceCreated >= SYNC_CLEAR_INITIAL_DELAY_MS;
    if (initialDelayDone && (pendingSync.sendCount == 0 || (tick - pendingSync.lastSendMs) >= SYNC_CLEAR_BURST_INTERVAL_MS)) {
      sendAddressed(pendingSync.targetAddr, pendingSync.payload);
      pendingSync.sendCount++;
      pendingSync.lastSendMs = tick;
      CENTRAL_DEBUG_LOGF("[ALARM][SYNC OFF TX #%lu] %s\n",
                         static_cast<unsigned long>(pendingSync.sendCount),
                         pendingSync.payload.c_str());
    }

    if (pendingSync.sendCount >= SYNC_CLEAR_BURST_COUNT) {
      Serial.printf("[ALARM][SYNC OFF DONE] %s | envios=%lu\n",
                    pendingSync.targetUid.c_str(),
                    static_cast<unsigned long>(pendingSync.sendCount));
      pendingSync = PendingSyncMessage();
    }
    return;
  }

  if (pendingSync.sendCount == 0 || (tick - pendingSync.lastSendMs) >= SYNC_ACK_RETRY_MS) {
    sendAddressed(pendingSync.targetAddr, pendingSync.payload);
    pendingSync.sendCount++;
    pendingSync.lastSendMs = tick;
    CENTRAL_DEBUG_LOGF("[ALARM][SYNC TX #%lu] %s\n",
                       static_cast<unsigned long>(pendingSync.sendCount),
                       pendingSync.payload.c_str());
  }

  if ((tick - pendingSync.lastLogMs) >= SYNC_WAIT_LOG_MS) {
    pendingSync.lastLogMs = tick;
    CENTRAL_DEBUG_LOGF("[ALARM][WAIT SYNC] seq=%lu esperando ACK de %s (%lums)\n",
                       static_cast<unsigned long>(pendingSync.syncSeq),
                       pendingSync.targetUid.c_str(),
                       tick - pendingSync.createdMs);
  }
}

int findOrAllocateSourceTrack(uint16_t sourceAddr, const String& sourceUid) {
  int freeIndex = -1;
  for (size_t i = 0; i < MAX_SOURCE_TRACK; ++i) {
    if (sourceTrack[i].used) {
      const bool sameAddr = (sourceTrack[i].sourceAddr == sourceAddr);
      const bool sameUid = sourceTrack[i].sourceUid.equalsIgnoreCase(sourceUid);
      if (sameAddr || sameUid) return static_cast<int>(i);
    } else if (freeIndex < 0) {
      freeIndex = static_cast<int>(i);
    }
  }

  if (freeIndex >= 0) {
    sourceTrack[freeIndex].used = true;
    sourceTrack[freeIndex].sourceAddr = sourceAddr;
    sourceTrack[freeIndex].sourceUid = sourceUid;
    sourceTrack[freeIndex].lastSeq = 0;
  }
  return freeIndex;
}

bool isDuplicateEvent(uint16_t sourceAddr, const String& sourceUid, uint32_t seq) {
  const int index = findOrAllocateSourceTrack(sourceAddr, sourceUid);
  if (index < 0) return false;

  if (sourceTrack[index].lastSeq == seq) {
    return true;
  }

  sourceTrack[index].sourceAddr = sourceAddr;
  sourceTrack[index].sourceUid = sourceUid;
  sourceTrack[index].lastSeq = seq;
  return false;
}

void updateSourceBattery(uint16_t sourceAddr, const String& sourceUid, int batteryPercent) {
  if (batteryPercent < 0 || batteryPercent > 100) return;
  const int index = findOrAllocateSourceTrack(sourceAddr, sourceUid);
  if (index < 0) return;
  sourceTrack[index].sourceAddr = sourceAddr;
  sourceTrack[index].sourceUid = sourceUid;
  sourceTrack[index].batteryPercent = batteryPercent;
}

void applyCurrentAlarm(uint16_t sourceAddr,
                       const String& sourceUid,
                       const String& type,
                       const String& mode,
                       bool active,
                       bool publishToMasterNow = true) {
  currentAlarm.active = active;
  currentAlarm.sourceAddr = sourceAddr;
  currentAlarm.sourceUid = sourceUid;
  currentAlarm.type = type;
  currentAlarm.mode = mode;

  applyAlarmOutputs(type, active);
  if (publishToMasterNow) {
    scheduleMasterPublish();
  } else {
    masterAlarmPublishArmed = false;
    pendingMasterBursts = 0;
  }
}

void processEmergencyLine(String line) {
  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'E' || fieldCount < 5) return;

  uint32_t addrValue = 0;
  uint32_t typeCodeValue = 0;
  uint32_t activeValue = 0;
  uint32_t seq = 0;
  if (!parseUInt32Field(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (!parseUInt32Field(fields[2], typeCodeValue) || typeCodeValue > 0xFFUL) return;
  if (!parseUInt32Field(fields[3], activeValue)) return;
  if (!parseUInt32Field(fields[4], seq)) return;

  const uint16_t sourceAddr = static_cast<uint16_t>(addrValue);
  const String sourceUid = panelKeyFromAddr(sourceAddr);
  const String type = alarmTypeFromCode(static_cast<uint8_t>(typeCodeValue));
  const String mode = alarmModeForType(type);
  const bool active = activeValue != 0;

  if (isDuplicateEvent(sourceAddr, sourceUid, seq)) {
    CENTRAL_DEBUG_LOGF("[ALARM][DUP] ADDR=%u SEQ=%lu\n",
                       static_cast<unsigned>(sourceAddr),
                       static_cast<unsigned long>(seq));
    sendEventAck(sourceAddr, seq);
    scheduleSourcePublish(PENDING_SOURCE_EVENT, sourceAddr, sourceUid, type, active, seq);
    return;
  }

  if (!active && currentAlarm.active && !currentAlarm.sourceUid.equalsIgnoreCase(sourceUid)) {
    Serial.printf("[ALARM][IGN] clear ignorado de %s; alarma actual=%s\n",
                  sourceUid.c_str(),
                  currentAlarm.sourceUid.c_str());
    sendEventAck(sourceAddr, seq);
    return;
  }

  applyCurrentAlarm(sourceAddr, sourceUid, type, mode, active, false);
  Serial.printf("[ALARM][EVT] ADDR=%u TYPE=%s ACTIVE=%d MODE=%s\n",
                static_cast<unsigned>(sourceAddr),
                type.c_str(),
                active ? 1 : 0,
                mode.c_str());

  sendEventAck(sourceAddr, seq);
  scheduleSourcePublish(PENDING_SOURCE_EVENT, sourceAddr, sourceUid, type, active, seq);
}

void processHeartbeatLine(String line) {
  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'H' || fieldCount < 5) return;

  uint32_t addrValue = 0;
  uint32_t activeValue = 0;
  uint32_t typeCodeValue = 0;
  uint32_t seq = 0;
  int batteryPercent = -1;
  if (!parseUInt32Field(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (!parseUInt32Field(fields[2], activeValue)) return;
  if (!parseUInt32Field(fields[3], typeCodeValue) || typeCodeValue > 0xFFUL) return;
  if (!parseUInt32Field(fields[4], seq)) return;
  if (fieldCount >= 6) {
    uint32_t batteryValue = 0;
    if (parseUInt32Field(fields[5], batteryValue) && batteryValue <= 100UL) {
      batteryPercent = static_cast<int>(batteryValue);
    }
  }

  const uint16_t sourceAddr = static_cast<uint16_t>(addrValue);
  const String sourceUid = panelKeyFromAddr(sourceAddr);
  const bool active = activeValue != 0;
  const String type = alarmTypeFromCode(static_cast<uint8_t>(typeCodeValue));
  updateSourceBattery(sourceAddr, sourceUid, batteryPercent);

  sendHeartbeatAck(sourceAddr, seq);
  scheduleSourcePublish(PENDING_SOURCE_HEARTBEAT, sourceAddr, sourceUid, type, active, seq);
  if (batteryPercent >= 0) {
    Serial.printf("[ALARM][HB] ADDR=%u ACTIVE=%d TYPE=%s BAT=%d%%\n",
                  static_cast<unsigned>(sourceAddr),
                  active ? 1 : 0,
                  type.c_str(),
                  batteryPercent);
  } else {
    Serial.printf("[ALARM][HB] ADDR=%u ACTIVE=%d TYPE=%s\n",
                  static_cast<unsigned>(sourceAddr),
                  active ? 1 : 0,
                  type.c_str());
  }
}

void processSyncAckLine(String line) {
  if (!pendingSync.active || !pendingSync.desiredState) return;

  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'K' || fieldCount < 4) return;

  uint32_t addrValue = 0;
  if (!parseUInt32Field(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (static_cast<uint16_t>(addrValue) != pendingSync.targetAddr) return;
  if (fields[2].length() != 1 || fields[2].charAt(0) != 'S') return;

  uint32_t syncSeq = 0;
  if (!parseUInt32Field(fields[3], syncSeq)) return;
  if (syncSeq != pendingSync.syncSeq) return;

  Serial.printf("[ALARM][SYNC ACK] %s | seq=%lu | %lums | envios=%lu\n",
                pendingSync.targetUid.c_str(),
                static_cast<unsigned long>(syncSeq),
                millis() - pendingSync.createdMs,
                static_cast<unsigned long>(pendingSync.sendCount));

  sendPanelStatusToMaster(pendingSync.targetAddr,
                          pendingSync.targetUid,
                          pendingSync.desiredState,
                          pendingSync.type,
                          "sync");
  pendingSync = PendingSyncMessage();
}

void clearCurrentAlarmFromMaster(uint16_t targetAddr, const String& type, uint32_t reqId) {
  const String sourceKey = panelKeyFromAddr(targetAddr);
  if (resendDuplicateControlAckIfNeeded(targetAddr, type, reqId)) {
    return;
  }

  if (!currentAlarm.active) {
    Serial.printf("[ALARM][CLEAR] req=%lu rechazado | reason=no_active_alarm | addr=%u type=%s\n",
                  static_cast<unsigned long>(reqId),
                  static_cast<unsigned>(targetAddr),
                  type.c_str());
    delay(MASTER_CTRL_ACK_BURST_INTERVAL_MS);
    rememberLastControlAck(sourceKey, type, false, "no_active_alarm", reqId);
    sendMasterControlAckBurst(buildMasterControlAck(targetAddr, type, false, "no_active_alarm", reqId));
    return;
  }

  if (targetAddr != 0 && currentAlarm.sourceAddr != targetAddr) {
    Serial.printf("[ALARM][CLEAR] req=%lu rechazado | reason=addr_mismatch | addr=%u actual=%u\n",
                  static_cast<unsigned long>(reqId),
                  static_cast<unsigned>(targetAddr),
                  static_cast<unsigned>(currentAlarm.sourceAddr));
    delay(MASTER_CTRL_ACK_BURST_INTERVAL_MS);
    rememberLastControlAck(sourceKey, type, false, "uid_mismatch", reqId);
    sendMasterControlAckBurst(buildMasterControlAck(targetAddr, type, false, "uid_mismatch", reqId));
    return;
  }

  if (!type.isEmpty() && !currentAlarm.type.equalsIgnoreCase(type)) {
    Serial.printf("[ALARM][CLEAR] req=%lu rechazado | reason=type_mismatch | type=%s actual=%s\n",
                  static_cast<unsigned long>(reqId),
                  type.c_str(),
                  currentAlarm.type.c_str());
    delay(MASTER_CTRL_ACK_BURST_INTERVAL_MS);
    rememberLastControlAck(sourceKey, type, false, "type_mismatch", reqId);
    sendMasterControlAckBurst(buildMasterControlAck(targetAddr, type, false, "type_mismatch", reqId));
    return;
  }

  AlarmState previous = currentAlarm;
  schedulePanelSync(previous.sourceAddr, previous.sourceUid, previous.type, false);
  applyCurrentAlarm(previous.sourceAddr, previous.sourceUid, previous.type, previous.mode, false);
  Serial.printf("[ALARM][CLEAR] req=%lu aceptado | addr=%u type=%s\n",
                static_cast<unsigned long>(reqId),
                static_cast<unsigned>(previous.sourceAddr),
                previous.type.c_str());
  delay(MASTER_CTRL_ACK_BURST_INTERVAL_MS);
  rememberLastControlAck(panelKeyFromAddr(previous.sourceAddr), previous.type, true, "accepted", reqId);
  sendMasterControlAckBurst(buildMasterControlAck(previous.sourceAddr, previous.type, true, "accepted", reqId));
}

bool readInputPressed(size_t index) {
  return !digitalRead(INPUT_PINS[index]);
}

void handleLocalInputChange(size_t index, bool pressed) {
  const String sourceUid = String("LOCAL-IN") + String(index + 1);
  const String type = ALARM_TYPES[index];
  const String mode = alarmModeForType(type);

  if (pressed) {
    applyCurrentAlarm(0, sourceUid, type, mode, true);
    Serial.printf("[ALARM][LOCAL] %s activada\n", sourceUid.c_str());
    return;
  }

  if (currentAlarm.active &&
      currentAlarm.sourceAddr == 0 &&
      currentAlarm.sourceUid.equalsIgnoreCase(sourceUid) &&
      currentAlarm.type.equalsIgnoreCase(type)) {
    applyCurrentAlarm(0, sourceUid, type, mode, false);
    Serial.printf("[ALARM][LOCAL] %s liberada\n", sourceUid.c_str());
  }
}

void scanLocalInputs() {
  if (!ENABLE_LOCAL_INPUTS) return;

  const unsigned long now = millis();
  if ((now - lastInputScanMs) < INPUT_SCAN_MS) return;
  lastInputScanMs = now;

  for (size_t i = 0; i < 4; ++i) {
    const bool pressed = readInputPressed(i);
    if (pressed == lastStableInputStates[i]) continue;
    if ((now - lastStableChangeMs[i]) < INPUT_DEBOUNCE_MS) continue;

    lastStableInputStates[i] = pressed;
    lastStableChangeMs[i] = now;
    handleLocalInputChange(i, pressed);
  }
}

void processMasterPanelAckLine(String line) {
  if (!pendingMasterPanel.active) return;

  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'K' || fieldCount < 4) return;

  uint32_t addrValue = 0;
  if (!parseUInt32Field(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (!pendingMasterPanel.targetUid.equalsIgnoreCase(panelKeyFromAddr(static_cast<uint16_t>(addrValue)))) return;
  if (fields[2].length() != 1 || fields[2].charAt(0) != 'P') return;

  uint32_t seq = 0;
  if (!parseUInt32Field(fields[3], seq)) return;
  if (seq != pendingMasterPanel.seq) return;

  Serial.printf("[ALARM][MASTER ACK] %s | seq=%lu | %lums | envios=%lu\n",
                pendingMasterPanel.targetUid.c_str(),
                static_cast<unsigned long>(seq),
                millis() - pendingMasterPanel.createdMs,
                static_cast<unsigned long>(pendingMasterPanel.sendCount));
  const bool shouldPublishAlarmModule = pendingMasterPanel.publishAlarmModuleOnAck;
  pendingMasterPanel = PendingMasterPanelMessage();
  if (shouldPublishAlarmModule) {
    scheduleMasterPublish();
  }
}

void processMasterModuleAckLine(String line) {
  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'K' || fieldCount < 4) return;

  uint32_t addrValue = 0;
  if (!parseUInt32Field(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (static_cast<uint16_t>(addrValue) != currentAlarm.sourceAddr) return;
  if (fields[2].length() != 1 || fields[2].charAt(0) != 'M') return;

  uint32_t seq = 0;
  if (!parseUInt32Field(fields[3], seq)) return;

  if (masterAlarmPublishArmed && seq == currentMasterModuleSequence) {
    Serial.printf("[ALARM][MASTER MOD ACK] %s TYPE=%s ACTIVE=%d -> OK, se detiene publicacion\n",
                  currentAlarm.sourceUid.c_str(),
                  currentAlarm.type.c_str(),
                  currentAlarm.active ? 1 : 0);
    masterAlarmPublishArmed = false;
    pendingMasterBursts = 0;
    nextMasterPublishMs = 0;
    lastMasterHeartbeatMs = millis();
    return;
  }

  Serial.printf("[ALARM][MASTER MOD ACK] %s TYPE=%s ACTIVE=%d -> ignorado\n",
                panelKeyFromAddr(static_cast<uint16_t>(addrValue)).c_str(),
                currentAlarm.type.c_str(),
                currentAlarm.active ? 1 : 0);
}

void processControlLine(String line) {
  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'C' || fieldCount < 4) return;

  uint32_t addrValue = 0;
  uint32_t typeCodeValue = 0;
  uint32_t reqId = 0;
  if (!parseUInt32Field(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (!parseUInt32Field(fields[2], typeCodeValue) || typeCodeValue > 0xFFUL) return;
  if (!parseUInt32Field(fields[3], reqId)) return;

  clearCurrentAlarmFromMaster(static_cast<uint16_t>(addrValue),
                              alarmTypeFromCode(static_cast<uint8_t>(typeCodeValue)),
                              reqId);
}

void handleRadioRx() {
  while (RadioSerial.available() > 0) {
    feedTaskWatchdog();
    const char c = static_cast<char>(RadioSerial.read());
    if (c == '\r') continue;

    if (c == '\n') {
      if (!radioOverflow && !radioLine.isEmpty()) {
        CENTRAL_DEBUG_LOGF("[ALARM][RX RAW] %s\n", debugRadioText(radioLine).c_str());
        processSyncAckLine(radioLine);
        processMasterPanelAckLine(radioLine);
        processMasterModuleAckLine(radioLine);
        processControlLine(radioLine);
        processHeartbeatLine(radioLine);
        processEmergencyLine(radioLine);
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

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[ALARM] Iniciando central de alarma LoRa");

  pinMode(PIN_E220_MOM1, OUTPUT);
  pinMode(PIN_ALARM_MAIN, OUTPUT);
  for (size_t i = 0; i < 4; ++i) {
    pinMode(RELAY_PINS[i], OUTPUT);
    setRelayPhysical(i, false);
    pinMode(INPUT_PINS[i], INPUT_PULLUP);
    lastStableInputStates[i] = readInputPressed(i);
    lastStableChangeMs[i] = millis();
  }
  clearAlarmOutputs();

  const bool e220Ok = bootConfigureIfEnabled();
  Serial.printf("[E220] config=%s\n", e220Ok ? "OK" : "ERROR");
  setModeNormal();
  RadioSerial.begin(9600, SERIAL_8N1, PIN_E220_RX, PIN_E220_TX);
  initTaskWatchdog();

  Serial.printf("[ALARM] ADDR=%u MASTER=%u CH=%u\n",
                THIS_MODULE_ADDR,
                MASTER_ADDR,
                LORA_CHANNEL);
  Serial.println("[ALARM] Relays: 23,5,4,13 | Inputs: 25,26,27,33");
  Serial.printf("[ALARM] Entradas locales: %s\n", ENABLE_LOCAL_INPUTS ? "ACTIVAS" : "DESACTIVADAS");
}

void loop() {
  feedTaskWatchdog();
  const unsigned long now = millis();
  handleRadioRx();
  scanLocalInputs();
  serviceAlarmPattern();
  servicePendingSync(now);
  servicePendingSourcePublish(now);
  servicePendingMasterPanelAck(now);
  serviceMasterPublish(now);
  feedTaskWatchdog();
}
