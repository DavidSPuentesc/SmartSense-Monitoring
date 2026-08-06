#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <LittleFS.h> 
#include <SPI.h>
#include <algorithm>
#include <map>
#include <vector>
#include <math.h>
#include <time.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include "Display_ST7789.h"
#include "LCD_Text.h"
#include "SD_Card.h"

// =====================================================
// WIFI - MODO ACCESS POINT
// =====================================================
const char* AP_SSID = "Sensores_Temp";
// AP_PASS en secrets.h (no versionado; copiar secrets.example.h como secrets.h)
#include "secrets.h"

// =====================================================
// IP FIJA DEL MAESTRO (en modo AP)
// =====================================================
IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);


static constexpr int PIN_ALARMA_CRITICA = 16;
// =====================================================
// E220
// =====================================================
#define AUTO_CONFIGURE_ON_BOOT 0
#define ENABLE_LITTLEFS_DIAGNOSTICS 0

static constexpr bool LOG_DEBUG = false;

#define MASTER_DEBUG_LOGF(...) do { if (LOG_DEBUG) Serial.printf(__VA_ARGS__); } while (0)
#define MASTER_DEBUG_LOGLN(msg) do { if (LOG_DEBUG) Serial.println(msg); } while (0)

// ESP32-C6-Touch-LCD-1.47 reserves GPIO18-23 for touch/LCD/backlight.
// Move E220 to free pins on the side headers.
static constexpr int PIN_E220_RX = 8; // TX del sensor
static constexpr int PIN_E220_TX = 7; // RX del sensor
static constexpr int PIN_E220_M0 = 9;
static constexpr int PIN_E220_M1 = 9;

static constexpr uint16_t MASTER_ADDR = 1;
static constexpr uint16_t ALARM_MODULE_ADDR = 2;
static constexpr uint8_t MASTER_CH = 23;

static constexpr int UART_BAUD_CODE = 3;
static constexpr int UART_PARITY_CODE = 0;
static constexpr int AIR_RATE_CODE = 2;
static constexpr int TRANS_MODE_CODE = 1;
static constexpr int PACKET_LEN_CODE = 0;
static constexpr int URXT_BYTE_TIME = 3;
static constexpr int POWER_CODE = 0;
static constexpr int KEY_CODE = 0;
static constexpr int LBT_CODE = 0;



// =====================================================
// SERVIDOR
// =====================================================
AsyncWebServer server(80);
AsyncEventSource events("/events");
DNSServer dnsServer;
const byte DNS_PORT = 53;

// =====================================================
// CONFIG
// =====================================================
static const int MAX_DAYS_HISTORY = 30;
static const uint32_t DEFAULT_SLEEP_TIME = 60;
static const float DEFAULT_TEMP_OFFSET = 0.0f;
static const char* DEFAULT_SENSOR_NAME = "SENSOR_1";
static const char* KNOWN_NODES_FILE = "/known_nodes.txt";
static const char* KNOWN_ALARM_PANELS_FILE = "/known_alarm_panels.txt";
static const float DEFAULT_THRESHOLD_LOW = 50.0f;
static const float DEFAULT_THRESHOLD_HIGH = 60.0f;
static uint32_t g_timeBaseEpoch = 0;
static uint32_t g_timeBaseMillis = 0;
static const int32_t TZ_OFFSET_SECONDS = -5 * 3600;

// =====================================================
// PREFERENCES
// =====================================================
Preferences prefs;

// =====================================================
// MODELO DE DATOS
// =====================================================
struct NodeData {
  String mac;
  String name;
  String machine;
  float temperature = NAN;
  float rawTemperature = NAN;
  float battery = NAN;
  unsigned long lastSeen = 0;

  String remoteIp = "LORA";
  int rssi = 0;

  float tempOffset = DEFAULT_TEMP_OFFSET;
  float offsetBaseRaw = NAN;
  float offsetCalibrationRaw = NAN;
  uint32_t sleepSec = DEFAULT_SLEEP_TIME;
  float thresholdLow = DEFAULT_THRESHOLD_LOW;
  float thresholdHigh = DEFAULT_THRESHOLD_HIGH;

  uint16_t nodeId = 0;

  float todayMin = NAN;
  float todayMax = NAN;
  float todaySum = 0;
  int todaySamples = 0;
  uint32_t lastSampleDay = 0;
  uint32_t lastSavedTimestamp = 0;
  bool alarmSilenced = false;
};

struct StoredDaySummary {
  bool valid = false;
  String date;
  float lastTemp = NAN;
  float minTemp = NAN;
  float maxTemp = NAN;
  float sumTemp = 0;
  int samples = 0;
  uint32_t lastSecOfDay = 0;
};

std::map<String, NodeData> nodes;

struct AlarmPanelInfo {
  String uid;
  uint16_t radioId = 0;
  String name;
  String zone;
  String location;
  int batteryPercent = -1;
  bool active = false;
  String activeType;
  uint32_t lastSeenEpoch = 0;
  unsigned long lastSeenMs = 0;
};

struct EmergencyState {
  uint32_t eventId = 0;
  String sourceId;
  String zone;
  String type;
  String mode;
  bool active = false;
  uint32_t firstSeenEpoch = 0;
  uint32_t updatedEpoch = 0;
};

struct EmergencyHistoryEntry {
  uint32_t eventId = 0;
  String sourceId;
  String zone;
  String type;
  String mode;
  bool active = false;
  uint32_t timestampEpoch = 0;
  uint32_t clearedEpoch = 0;
  uint32_t updatedEpoch = 0;
};

std::map<String, AlarmPanelInfo> alarmPanels;
std::map<String, EmergencyState> emergencyStates;
std::vector<EmergencyHistoryEntry> emergencyHistory;
static uint32_t g_nextEmergencyEventId = 1;
static uint32_t g_nextEmergencyControlReqId = 1;
static constexpr uint32_t ALARM_PANEL_OFFLINE_THRESHOLD_SEC = 6UL * 60UL;
static constexpr unsigned long ALARM_PANEL_OFFLINE_THRESHOLD_MS = 6UL * 60UL * 1000UL;

// =====================================================
// TIMERS
// =====================================================
static unsigned long lastBroadcastMs = 0;
static unsigned long lastStatusPrintMs = 0;
static bool g_sdMounted = false;

FS& historyFS() {
  return g_sdMounted ? static_cast<FS&>(SD) : static_cast<FS&>(LittleFS);
}

const char* historyStorageLabel() {
  return g_sdMounted ? "SD" : "LFS";
}

uint64_t historyTotalBytes() {
  return g_sdMounted ? SD.totalBytes() : LittleFS.totalBytes();
}

uint64_t historyUsedBytes() {
  return g_sdMounted ? SD.usedBytes() : LittleFS.usedBytes();
}

uint64_t historyFreeBytes() {
  uint64_t total = historyTotalBytes();
  uint64_t used = historyUsedBytes();
  return (total > used) ? (total - used) : 0;
}



// =====================================================
// HELPERS GENERALES
// =====================================================
bool masterTimeIsValid() {
  return g_timeBaseEpoch != 0;
}

uint32_t masterNowEpoch() {
  if (!masterTimeIsValid()) return 0;
  return g_timeBaseEpoch + ((millis() - g_timeBaseMillis) / 1000UL);
}

uint32_t epochToLocal(uint32_t epochUtc) {
  int64_t local = (int64_t)epochUtc + (int64_t)TZ_OFFSET_SECONDS;
  if (local < 0) local = 0;
  return (uint32_t)local;
}

void localEpochToDateTime(uint32_t epochUtc,
                          uint32_t& year,
                          uint32_t& month,
                          uint32_t& day,
                          uint32_t& hour,
                          uint32_t& minute,
                          uint32_t& second) {
  uint32_t localEpoch = epochToLocal(epochUtc);

  uint32_t days = localEpoch / 86400UL;
  uint32_t daySec = localEpoch % 86400UL;

  hour = daySec / 3600UL;
  minute = (daySec % 3600UL) / 60UL;
  second = daySec % 60UL;

  uint32_t z = days + 719468UL;
  uint32_t era = z / 146097UL;
  uint32_t doe = z - era * 146097UL;
  uint32_t yoe = (doe - doe / 1460UL + doe / 36524UL - doe / 146096UL) / 365UL;
  uint32_t y = yoe + era * 400UL;
  uint32_t doy = doe - (365UL * yoe + yoe / 4UL - yoe / 100UL);
  uint32_t mp = (5UL * doy + 2UL) / 153UL;
  uint32_t d = doy - (153UL * mp + 2UL) / 5UL + 1UL;
  uint32_t m = mp + (mp < 10 ? 3 : -9);
  y += (m <= 2);

  year = y;
  month = m;
  day = d;
}

String formatTimeAmPmFromEpoch(uint32_t epochUtc) {
  if (epochUtc == 0) return "--:--:-- --";

  uint32_t localEpoch = epochToLocal(epochUtc);
  uint32_t daySec = localEpoch % 86400UL;

  uint32_t hh24 = daySec / 3600UL;
  uint32_t mm = (daySec % 3600UL) / 60UL;
  uint32_t ss = daySec % 60UL;

  const char* ampm = (hh24 >= 12) ? "PM" : "AM";
  uint32_t hh12 = hh24 % 12;
  if (hh12 == 0) hh12 = 12;

  char buf[20];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu %s",
           (unsigned long)hh12, (unsigned long)mm, (unsigned long)ss, ampm);
  return String(buf);
}

String formatDateFromEpoch(uint32_t epochUtc) {
  if (epochUtc == 0) return "";

  uint32_t year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  localEpochToDateTime(epochUtc, year, month, day, hour, minute, second);

  char buf[16];
  snprintf(buf, sizeof(buf), "%04lu-%02lu-%02lu",
           (unsigned long)year,
           (unsigned long)month,
           (unsigned long)day);
  return String(buf);
}

uint32_t getDayStart(uint32_t epochUtc) {
  uint32_t localEpoch = epochToLocal(epochUtc);
  return localEpoch - (localEpoch % 86400UL);
}

String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '\"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}

String normalizeMac(String mac) {
  mac.trim();
  mac.toUpperCase();

  String hex;
  hex.reserve(12);

  for (size_t i = 0; i < mac.length(); i++) {
    char c = mac[i];
    bool isHex =
      (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');

    if (isHex) {
      hex += c;
    }
  }

  // Debe tener exactamente 12 hex
  if (hex.length() != 12) {
    return "";
  }

  // Formato estándar AA:BB:CC:DD:EE:FF
  String out;
  out.reserve(17);
  for (int i = 0; i < 12; i++) {
    out += hex[i];
    if (i % 2 == 1 && i < 11) out += ':';
  }

  return out;
}

String macToNs(String mac) {
  mac = normalizeMac(mac);
  String ns;
  ns.reserve(mac.length());
  for (size_t i = 0; i < mac.length(); i++) {
    char c = mac[i];
    if (c != ':') ns += c;
  }
  if (ns.length() > 15) {
    ns = ns.substring(ns.length() - 15);
  }
  return ns;
}

String formatUptime() {
  uint32_t s = millis() / 1000;
  uint32_t days = s / 86400;
  s %= 86400;
  uint8_t hours = s / 3600;
  s %= 3600;
  uint8_t mins = s / 60;
  s %= 60;

  char buf[32];
  snprintf(buf, sizeof(buf), "%lu d %02u:%02u:%02lu",
           (unsigned long)days, hours, mins, (unsigned long)s);
  return String(buf);
}

String pathBaseName(const String& path) {
  int slash = path.lastIndexOf('/');
  if (slash < 0) return path;
  return path.substring(slash + 1);
}

String defaultNameForMac(const String& mac) {
  String ns = macToNs(mac);
  if (ns.length() >= 4) {
    return "SENSOR_" + ns.substring(ns.length() - 4);
  }
  return DEFAULT_SENSOR_NAME;
}

String resolvedNodeName(const String& mac, String reportedName) {
  reportedName.trim();
  if (reportedName.isEmpty() || reportedName == DEFAULT_SENSOR_NAME) {
    return defaultNameForMac(mac);
  }
  return reportedName;
}

static constexpr float TEMP_OFFSET_EPSILON = 0.05f;
static constexpr float TEMP_OFFSET_MIN_SPAN = 0.25f;

bool hasEffectiveOffset(float offset) {
  return isfinite(offset) && fabsf(offset) > TEMP_OFFSET_EPSILON;
}

void captureColdReference(NodeData& n, float rawTemp) {
  if (!isfinite(rawTemp)) return;
  if (hasEffectiveOffset(n.tempOffset)) return;

  if (!isfinite(n.offsetBaseRaw) || rawTemp < n.offsetBaseRaw) {
    n.offsetBaseRaw = rawTemp;
  }
}

void updateCompensationCalibration(NodeData& n, float newOffset) {
  if (!isfinite(n.rawTemperature)) {
    if (!hasEffectiveOffset(newOffset)) {
      n.offsetCalibrationRaw = NAN;
    }
    return;
  }

  if (!hasEffectiveOffset(newOffset)) {
    n.offsetBaseRaw = n.rawTemperature;
    n.offsetCalibrationRaw = NAN;
    return;
  }

  if (!isfinite(n.offsetBaseRaw)) {
    n.offsetBaseRaw = n.rawTemperature;
  }

  if (n.rawTemperature <= n.offsetBaseRaw + TEMP_OFFSET_MIN_SPAN) {
    n.offsetCalibrationRaw = NAN;
    return;
  }

  n.offsetCalibrationRaw = n.rawTemperature;
}

float applyNodeOffset(const NodeData& n, float rawTemp) {
  if (!isfinite(rawTemp)) return rawTemp;
  if (!hasEffectiveOffset(n.tempOffset)) return rawTemp;

  if (!isfinite(n.offsetBaseRaw) || !isfinite(n.offsetCalibrationRaw)) {
    return rawTemp + n.tempOffset;
  }

  const float span = n.offsetCalibrationRaw - n.offsetBaseRaw;
  if (span <= TEMP_OFFSET_MIN_SPAN) {
    return rawTemp + n.tempOffset;
  }

  if (rawTemp <= n.offsetBaseRaw) return rawTemp;
  if (rawTemp >= n.offsetCalibrationRaw) return rawTemp + n.tempOffset;

  const float ratio = (rawTemp - n.offsetBaseRaw) / span;
  return rawTemp + (n.tempOffset * constrain(ratio, 0.0f, 1.0f));
}

bool captureColdReferenceNow(NodeData& n) {
  if (!isfinite(n.rawTemperature)) return false;

  n.offsetBaseRaw = n.rawTemperature;
  if (isfinite(n.offsetCalibrationRaw) && n.offsetCalibrationRaw <= (n.offsetBaseRaw + TEMP_OFFSET_MIN_SPAN)) {
    n.offsetCalibrationRaw = NAN;
  }

  n.temperature = applyNodeOffset(n, n.rawTemperature);
  return true;
}

void resetNodeCompensationCalibration(NodeData& n) {
  n.offsetBaseRaw = NAN;
  n.offsetCalibrationRaw = NAN;
  if (isfinite(n.rawTemperature)) {
    n.temperature = applyNodeOffset(n, n.rawTemperature);
  }
}

String nsToMac(String ns) {
  ns.trim();
  ns.toUpperCase();

  if (ns.length() != 12) return "";

  for (size_t i = 0; i < ns.length(); i++) {
    char c = ns[i];
    bool isHex =
      (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
    if (!isHex) return "";
  }

  String mac;
  mac.reserve(17);
  for (int i = 0; i < 12; i++) {
    mac += ns[i];
    if (i % 2 == 1 && i < 11) mac += ':';
  }
  return mac;
}

bool vectorContainsMac(const std::vector<String>& items, const String& mac) {
  return std::find(items.begin(), items.end(), mac) != items.end();
}

bool addUniqueMac(std::vector<String>& items, const String& mac) {
  if (mac.isEmpty() || vectorContainsMac(items, mac)) return false;
  items.push_back(mac);
  return true;
}

bool vectorContainsString(const std::vector<String>& items, const String& value) {
  return std::find(items.begin(), items.end(), value) != items.end();
}

bool addUniqueString(std::vector<String>& items, const String& value) {
  if (value.isEmpty() || vectorContainsString(items, value)) return false;
  items.push_back(value);
  return true;
}

// =====================================================
// DIAGNÓSTICO LittleFS
// =====================================================

// Función recursiva para listar directorios
void listDir(File dir, int level) {
  if (!dir) {
    Serial.println("ERROR: No se puede abrir directorio");
    return;
  }

  if (!dir.isDirectory()) {
    Serial.println("ERROR: No es un directorio");
    return;
  }

  File file = dir.openNextFile();
  int fileCount = 0;
  int dirCount = 0;

  while (file) {
    // Indentación según nivel
    for (int i = 0; i < level; i++) {
      Serial.print("  ");
    }

    if (file.isDirectory()) {
      dirCount++;
      Serial.printf("[DIR]  %s/\n", file.name());

      // Recursivamente listar contenido del subdirectorio
      File subDir = LittleFS.open(file.path());
      listDir(subDir, level + 1);
      subDir.close();
    } else {
      fileCount++;
      size_t fileSize = file.size();

      if (fileSize > 1024) {
        Serial.printf("[FILE] %s (%.2f KB)\n", file.name(), fileSize / 1024.0);
      } else {
        Serial.printf("[FILE] %s (%u bytes)\n", file.name(), fileSize);
      }
    }

    file.close();
    file = dir.openNextFile();
  }

  if (level == 0) {
    if (fileCount == 0 && dirCount == 0) {
      Serial.println("  (vacío)");
    } else {
      Serial.printf("\nTotal: %d archivos, %d directorios\n", fileCount, dirCount);
    }
  }
}
void diagnoseLittleFS() {
  Serial.println("\n=== DIAGNÓSTICO LittleFS ===");
  Serial.printf("Total: %u bytes\n", LittleFS.totalBytes());
  Serial.printf("Usado: %u bytes\n", LittleFS.usedBytes());
  Serial.printf("Libre: %u bytes\n", LittleFS.totalBytes() - LittleFS.usedBytes());

  Serial.println("\nContenido del LittleFS:");
  listDir(LittleFS.open("/"), 0);
  Serial.println("===========================\n");
}



// =====================================================
// LittleFS - MANEJO DE ARCHIVOS
// =====================================================
bool initLittleFS() {
  Serial.println("[LittleFS] Inicializando sistema de archivos...");

  if (LittleFS.begin(true)) {
    Serial.printf("[LittleFS] Montado. Total: %u KB, Usado: %u KB\n",
                  LittleFS.totalBytes() / 1024, LittleFS.usedBytes() / 1024);
    diagnoseLittleFS();
    return true;
  }

  Serial.println("[LittleFS] Error al montar");
  return false;
}

bool initHistoryStorage() {
  Serial.println("[SD] Inicializando microSD...");
  g_sdMounted = SD_Init();
  Flash_test();
  if (!g_sdMounted) {
    Serial.println("[SD] No disponible. El historial seguira en LittleFS");
    return false;
  }
  return true;
}

std::vector<String> readKnownNodeMacs() {
  std::vector<String> macs;

  if (!LittleFS.exists(KNOWN_NODES_FILE)) {
    return macs;
  }

  File file = LittleFS.open(KNOWN_NODES_FILE, "r");
  if (!file) {
    Serial.println("[KNOWN] No se pudo abrir known_nodes.txt");
    return macs;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    String mac = normalizeMac(line);
    addUniqueMac(macs, mac);
  }

  file.close();
  return macs;
}

bool writeKnownNodeMacs(const std::vector<String>& macs) {
  File file = LittleFS.open(KNOWN_NODES_FILE, "w");
  if (!file) {
    Serial.println("[KNOWN] No se pudo escribir known_nodes.txt");
    return false;
  }

  for (const String& mac : macs) {
    file.println(mac);
  }

  file.close();
  return true;
}

std::vector<String> scanKnownNodeMacsFromHistoryStorage() {
  std::vector<String> macs;
  FS& fs = historyFS();
  File root = fs.open("/");
  if (!root || !root.isDirectory()) {
    return macs;
  }

  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String base = pathBaseName(entry.name());
      if (base.startsWith("sensor_")) {
        String mac = nsToMac(base.substring(7));
        addUniqueMac(macs, mac);
      }
    }
    entry.close();
    entry = root.openNextFile();
  }

  root.close();
  return macs;
}

void rememberKnownNode(const String& mac) {
  String normalized = normalizeMac(mac);
  if (normalized.isEmpty()) return;

  std::vector<String> macs = readKnownNodeMacs();
  if (!addUniqueMac(macs, normalized)) return;

  if (writeKnownNodeMacs(macs)) {
    Serial.printf("[KNOWN] Registrado sensor %s\n", normalized.c_str());
  }
}

void forgetKnownNode(const String& mac) {
  String normalized = normalizeMac(mac);
  if (normalized.isEmpty()) return;

  std::vector<String> macs = readKnownNodeMacs();
  auto it = std::remove(macs.begin(), macs.end(), normalized);
  if (it == macs.end()) return;

  macs.erase(it, macs.end());
  if (writeKnownNodeMacs(macs)) {
    Serial.printf("[KNOWN] Eliminado sensor %s del registro\n", normalized.c_str());
  }
}

void ensureSensorDirectory(const String& mac) {
  String ns = macToNs(mac);
  String dirPath = "/sensor_" + ns;
  FS& fs = historyFS();

  if (!fs.exists(dirPath)) {
    if (fs.mkdir(dirPath)) {
      Serial.printf("[%s] Creado directorio: %s\n", historyStorageLabel(), dirPath.c_str());
    } else {
      Serial.printf("[%s] ERROR: No se pudo crear directorio: %s\n", historyStorageLabel(), dirPath.c_str());
    }
  }
}

String getLatestAvailableDate(const String& mac) {
  String ns = macToNs(mac);
  String dirPath = "/sensor_" + ns;
  FS& fs = historyFS();

  if (!fs.exists(dirPath)) return "";

  File dir = fs.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return "";
  }

  String latestDate = "";
  File file = dir.openNextFile();

  while (file) {
    String name = pathBaseName(file.name());
    if (!file.isDirectory() && name.endsWith(".txt")) {
      String date = name.substring(0, name.length() - 4);
      if (latestDate.isEmpty() || date > latestDate) {
        latestDate = date;
      }
    }
    file.close();
    file = dir.openNextFile();
  }

  dir.close();
  return latestDate;
}

bool loadStoredDaySummary(const String& mac, const String& dateStr, StoredDaySummary& summary) {
  summary = StoredDaySummary();
  summary.date = dateStr;

  if (dateStr.isEmpty()) return false;

  String ns = macToNs(mac);
  String fileName = "/sensor_" + ns + "/" + dateStr + ".txt";
  FS& fs = historyFS();

  if (!fs.exists(fileName)) return false;

  File file = fs.open(fileName, "r");
  if (!file) return false;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;

    int comma1 = line.indexOf(',');
    if (comma1 <= 0) continue;

    String secStr = line.substring(0, comma1);
    String tempStr = line.substring(comma1 + 1);

    uint32_t secOfDay = (uint32_t)secStr.toInt();
    float temp = tempStr.toInt() / 10.0f;

    if (!summary.valid) {
      summary.valid = true;
      summary.minTemp = temp;
      summary.maxTemp = temp;
      summary.lastTemp = temp;
      summary.lastSecOfDay = secOfDay;
      summary.sumTemp = temp;
      summary.samples = 1;
      continue;
    }

    if (temp < summary.minTemp) summary.minTemp = temp;
    if (temp > summary.maxTemp) summary.maxTemp = temp;
    if (secOfDay >= summary.lastSecOfDay) {
      summary.lastSecOfDay = secOfDay;
      summary.lastTemp = temp;
    }

    summary.sumTemp += temp;
    summary.samples++;
  }

  file.close();
  return summary.valid && summary.samples > 0;
}

void restoreNodeStateFromHistory(NodeData& n) {
  StoredDaySummary summary;
  String latestDate = getLatestAvailableDate(n.mac);

  if (!loadStoredDaySummary(n.mac, latestDate, summary)) {
    return;
  }

  n.temperature = summary.lastTemp;
  n.rawTemperature = NAN;
  n.todayMin = summary.minTemp;
  n.todayMax = summary.maxTemp;
  n.todaySum = summary.sumTemp;
  n.todaySamples = summary.samples;
  n.lastSavedTimestamp = 0;

  Serial.printf("[RESTORE] %s <- %s last=%.1f min=%.1f max=%.1f samples=%d\n",
                n.mac.c_str(),
                summary.date.c_str(),
                summary.lastTemp,
                summary.minTemp,
                summary.maxTemp,
                summary.samples);
}

void loadKnownNodesFromStorage() {
  std::vector<String> macs = readKnownNodeMacs();
  std::vector<String> fromFs = scanKnownNodeMacsFromHistoryStorage();
  bool changed = false;

  for (const String& mac : fromFs) {
    changed = addUniqueMac(macs, mac) || changed;
  }

  if (changed || (!LittleFS.exists(KNOWN_NODES_FILE) && !macs.empty())) {
    writeKnownNodeMacs(macs);
  }

  for (const String& mac : macs) {
    NodeData& n = nodes[mac];
    n.mac = mac;
    if (n.name.isEmpty()) {
      loadNodeConfig(mac, n, defaultNameForMac(mac));
    }
    restoreNodeStateFromHistory(n);
  }

  Serial.printf("[KNOWN] Sensores persistidos cargados: %u\n", (unsigned)macs.size());
}

String getDailyFileName(const String& mac, uint32_t epochUtc) {
  String ns = macToNs(mac);

  uint32_t year, month, day, hour, minute, second;
  localEpochToDateTime(epochUtc, year, month, day, hour, minute, second);

  char dateStr[16];
  snprintf(dateStr, sizeof(dateStr), "%04lu-%02lu-%02lu",
           (unsigned long)year,
           (unsigned long)month,
           (unsigned long)day);

  return "/sensor_" + ns + "/" + String(dateStr) + ".txt";
}

void cleanOldDailyFiles(const String& mac) {
  String ns = macToNs(mac);
  String dirPath = "/sensor_" + ns;
  FS& fs = historyFS();

  File dir = fs.open(dirPath);
  if (!dir || !dir.isDirectory()) return;

  std::vector<String> files;
  File file = dir.openNextFile();

  while (file) {
    String name = pathBaseName(file.name());
    if (name.endsWith(".txt")) {
      files.push_back(name);
    }
    file.close();
    file = dir.openNextFile();
  }

  std::sort(files.begin(), files.end());

  if (files.size() > MAX_DAYS_HISTORY) {
    int toDelete = files.size() - MAX_DAYS_HISTORY;
    for (int i = 0; i < toDelete; i++) {
      String fullPath = dirPath + "/" + files[i];
      if (fs.remove(fullPath)) {
        Serial.printf("[%s] Eliminado archivo antiguo: %s\n", historyStorageLabel(), fullPath.c_str());
      } else {
        Serial.printf("[%s] ERROR eliminando archivo antiguo: %s\n", historyStorageLabel(), fullPath.c_str());
      }
    }
  }

  dir.close();
}

void saveTemperatureSample(const String& mac, float temp, float bat, float vbat) {
  if (!masterTimeIsValid()) return;
  if (!isfinite(temp)) return;

  ensureSensorDirectory(mac);

  uint32_t nowEpoch = masterNowEpoch();
  uint32_t localEpoch = epochToLocal(nowEpoch);

  NodeData& n = nodes[mac];
  if (n.lastSavedTimestamp == localEpoch) return;
  n.lastSavedTimestamp = localEpoch;

  String fileName = getDailyFileName(mac, nowEpoch);

  uint32_t secOfDay = localEpoch % 86400UL;
  int16_t temp_x10 = (int16_t)lroundf(temp * 10.0f);

  uint32_t year, month, day, hour, minute, second;
  localEpochToDateTime(nowEpoch, year, month, day, hour, minute, second);
  FS& fs = historyFS();

  File file = fs.open(fileName, FILE_APPEND);
  if (!file) {
    file = fs.open(fileName, FILE_WRITE);
    if (!file) {
      Serial.printf("[%s] Error abriendo/creando: %s\n", historyStorageLabel(), fileName.c_str());
      return;
    }
  }

  size_t written = file.printf("%lu,%d\n",
                               (unsigned long)secOfDay,
                               (int)temp_x10);

  if (written > 0) {
    Serial.printf("[SAVE] %s -> %04lu-%02lu-%02lu %02lu:%02lu:%02lu -> %lu,%d\n",
                  fileName.c_str(),
                  (unsigned long)year,
                  (unsigned long)month,
                  (unsigned long)day,
                  (unsigned long)hour,
                  (unsigned long)minute,
                  (unsigned long)second,
                  (unsigned long)secOfDay,
                  (int)temp_x10);
  } else {
    Serial.printf("[%s] Error escribiendo en: %s\n", historyStorageLabel(), fileName.c_str());
  }

  file.close();
  cleanOldDailyFiles(mac);
}

String loadDailyData(const String& mac, const String& dateStr) {
  String ns = macToNs(mac);
  String fileName = "/sensor_" + ns + "/" + dateStr + ".txt";
  FS& fs = historyFS();

  if (!fs.exists(fileName)) return "[]";

  File file = fs.open(fileName, "r");
  if (!file) return "[]";

  DynamicJsonDocument doc(32768);
  JsonArray data = doc.to<JsonArray>();

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;

    int comma1 = line.indexOf(',');
    if (comma1 <= 0) continue;

    String secStr = line.substring(0, comma1);
    String tempStr = line.substring(comma1 + 1);

    uint32_t secOfDay = (uint32_t)secStr.toInt();
    int temp_x10 = tempStr.toInt();

    uint32_t hh = secOfDay / 3600UL;
    uint32_t mm = (secOfDay % 3600UL) / 60UL;
    uint32_t ss = secOfDay % 60UL;

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu:%02lu",
             (unsigned long)hh,
             (unsigned long)mm,
             (unsigned long)ss);

    JsonObject sample = data.createNestedObject();
    sample["time"] = String(timeBuf);
    sample["temp"] = temp_x10 / 10.0f;
  }

  file.close();

  String json;
  serializeJson(doc, json);
  return json;
}

String getAvailableDatesNew(const String& mac) {
  String ns = macToNs(mac);
  String dirPath = "/sensor_" + ns;
  FS& fs = historyFS();

  DynamicJsonDocument doc(4096);
  JsonArray dates = doc.createNestedArray("dates");

  if (!fs.exists(dirPath)) {
    String json;
    serializeJson(doc, json);
    return json;
  }

  File dir = fs.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    String json;
    serializeJson(doc, json);
    return json;
  }

  std::vector<String> dateList;
  File file = dir.openNextFile();

  while (file) {
    String name = pathBaseName(file.name());
    if (name.endsWith(".txt")) {
      String date = name.substring(0, name.length() - 4);
      dateList.push_back(date);
    }
    file.close();
    file = dir.openNextFile();
  }

  dir.close();

  std::sort(dateList.begin(), dateList.end(), std::greater<String>());

  for (const String& date : dateList) {
    dates.add(date);
  }

  String json;
  serializeJson(doc, json);
  return json;
}

String getDailyStats(const String& mac, const String& dateStr) {
  String data = loadDailyData(mac, dateStr);

  DynamicJsonDocument doc(1024);
  DynamicJsonDocument dataDoc(32768);
  deserializeJson(dataDoc, data);

  JsonArray samples = dataDoc.as<JsonArray>();

  if (samples.size() == 0) {
    doc["date"] = dateStr;
    doc["min"] = 0.0f;
    doc["max"] = 0.0f;
    doc["avg"] = 0.0f;
    doc["samples"] = 0;
  } else {
    float minTemp = 999.0f;
    float maxTemp = -999.0f;
    float sumTemp = 0.0f;
    int count = 0;

    for (JsonObject sample : samples) {
      float temp = sample["temp"];
      if (temp < minTemp) minTemp = temp;
      if (temp > maxTemp) maxTemp = temp;
      sumTemp += temp;
      count++;
    }

    doc["date"] = dateStr;
    doc["min"] = minTemp;
    doc["max"] = maxTemp;
    doc["avg"] = sumTemp / count;
    doc["samples"] = count;
  }

  String json;
  serializeJson(doc, json);
  return json;
}

// Agrega esta función auxiliar al final del código
String getDateStrFromTimestamp(uint32_t epochUtc) {
  uint32_t year, month, day, hour, minute, second;
  localEpochToDateTime(epochUtc, year, month, day, hour, minute, second);

  char dateStr[16];
  snprintf(dateStr, sizeof(dateStr), "%04lu-%02lu-%02lu",
           (unsigned long)year,
           (unsigned long)month,
           (unsigned long)day);

  return String(dateStr);
}

void checkAndSaveDailyStats(NodeData& n) {
  if (!masterTimeIsValid()) return;
  if (!isfinite(n.temperature)) return;

  uint32_t nowEpoch = masterNowEpoch();
  String dateStr = getDateStrFromTimestamp(nowEpoch);
  String statsJson = getDailyStats(n.mac, dateStr);

  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, statsJson);

  if (err || doc["samples"].as<int>() <= 0) {
    n.todayMin = n.temperature;
    n.todayMax = n.temperature;
    n.todaySum = n.temperature;
    n.todaySamples = 1;
    n.lastSampleDay = getDayStart(nowEpoch);
    return;
  }

  n.todayMin = doc["min"].as<float>();
  n.todayMax = doc["max"].as<float>();
  n.todaySamples = doc["samples"].as<int>();
  n.todaySum = doc["avg"].as<float>() * n.todaySamples;
  n.lastSampleDay = getDayStart(nowEpoch);
}



// =====================================================
// HELPERS PARSEO
// =====================================================
String getFieldValue(const String& line, const String& key) {
  String pattern = key + "=";
  int pos = -1;
  if (line.startsWith(pattern)) {
    pos = 0;
  } else {
    pos = line.indexOf("," + pattern);
    if (pos >= 0) pos += 1;
  }
  if (pos < 0) return "";

  pos += static_cast<int>(pattern.length());
  int end = line.indexOf(',', pos);
  if (end < 0) {
    return line.substring(pos);
  }
  return line.substring(pos, end);
}

bool fieldToFloat(const String& line, const String& key, float& out) {
  String s = getFieldValue(line, key);
  if (s.isEmpty()) return false;
  out = s.toFloat();
  return true;
}

bool fieldToUInt32(const String& line, const String& key, uint32_t& out) {
  String s = getFieldValue(line, key);
  if (s.isEmpty()) return false;
  out = (uint32_t)s.toInt();
  return true;
}

bool fieldToUInt16(const String& line, const String& key, uint16_t& out) {
  String s = getFieldValue(line, key);
  if (s.isEmpty()) return false;
  out = (uint16_t)s.toInt();
  return true;
}

String normalizeAlarmPanelUid(String uid) {
  uid.trim();
  bool digitsOnly = !uid.isEmpty();
  String alnum;
  alnum.reserve(uid.length());
  for (size_t i = 0; i < uid.length(); ++i) {
    const char c = uid[i];
    if (isalnum(static_cast<unsigned char>(c))) {
      const char upper = static_cast<char>(toupper(static_cast<unsigned char>(c)));
      alnum += upper;
      if (!isdigit(static_cast<unsigned char>(upper))) digitsOnly = false;
    }
  }

  if (digitsOnly && !alnum.isEmpty()) {
    uint32_t value = static_cast<uint32_t>(strtoul(alnum.c_str(), nullptr, 10));
    if (value <= 0xFFFFUL) {
      return String(value);
    }
  }

  return alnum;
}

bool isValidAlarmPanelUid(const String& uid) {
  if (uid.isEmpty()) return false;
  bool digitsOnly = true;
  for (size_t i = 0; i < uid.length(); ++i) {
    const char c = uid[i];
    if (!isdigit(static_cast<unsigned char>(c))) {
      digitsOnly = false;
      break;
    }
  }
  if (digitsOnly) {
    uint32_t value = static_cast<uint32_t>(strtoul(uid.c_str(), nullptr, 10));
    return value <= 0xFFFFUL;
  }
  if (uid.length() != 12) return false;
  for (size_t i = 0; i < uid.length(); ++i) {
    const char c = uid[i];
    if (!isxdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

bool alarmPanelKeyToAddr(const String& key, uint16_t& out) {
  const String cleaned = normalizeAlarmPanelUid(key);
  if (cleaned.isEmpty()) return false;
  for (size_t i = 0; i < cleaned.length(); ++i) {
    if (!isdigit(static_cast<unsigned char>(cleaned[i]))) {
      return false;
    }
  }
  const unsigned long parsed = strtoul(cleaned.c_str(), nullptr, 10);
  if (parsed > 0xFFFFUL) return false;
  out = static_cast<uint16_t>(parsed);
  return true;
}

String alarmPanelUidToNs(const String& uid) {
  String cleaned = normalizeAlarmPanelUid(uid);
  if (cleaned.length() > 14) cleaned = cleaned.substring(0, 14);
  return "p" + cleaned;
}

String defaultAlarmPanelName(const String& uid) {
  String cleaned = normalizeAlarmPanelUid(uid);
  if (cleaned.isEmpty()) return "Panel alarma";
  bool digitsOnly = true;
  for (size_t i = 0; i < cleaned.length(); ++i) {
    if (!isdigit(static_cast<unsigned char>(cleaned[i]))) {
      digitsOnly = false;
      break;
    }
  }
  if (digitsOnly) return "Panel " + cleaned;
  if (cleaned.length() <= 4) return "Panel " + cleaned;
  return "Panel " + cleaned.substring(cleaned.length() - 4);
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

bool parseProtocolUInt(const String& value, uint32_t& out) {
  if (value.isEmpty()) return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') return false;
  out = static_cast<uint32_t>(parsed);
  return true;
}

void loadAlarmPanelConfig(const String& uid, AlarmPanelInfo& panel) {
  String ns = alarmPanelUidToNs(uid);
  prefs.begin(ns.c_str(), true);
  panel.name = prefs.getString("name", "");
  panel.zone = prefs.getString("zone", "");
  panel.location = prefs.getString("loc", "");
  prefs.end();
  panel.uid = normalizeAlarmPanelUid(uid);
}

void saveAlarmPanelConfig(const String& uid,
                          const String& name,
                          const String& zone,
                          const String& location) {
  String ns = alarmPanelUidToNs(uid);
  prefs.begin(ns.c_str(), false);
  prefs.putString("name", name);
  prefs.putString("zone", zone);
  prefs.putString("loc", location);
  prefs.end();
}

std::vector<String> readKnownAlarmPanelUids() {
  std::vector<String> uids;

  if (!LittleFS.exists(KNOWN_ALARM_PANELS_FILE)) {
    return uids;
  }

  File file = LittleFS.open(KNOWN_ALARM_PANELS_FILE, "r");
  if (!file) {
    Serial.println("[ALARM][PANELS] No se pudo abrir known_alarm_panels.txt");
    return uids;
  }

  while (file.available()) {
    String line = file.readStringUntil('\n');
    String uid = normalizeAlarmPanelUid(line);
    if (!isValidAlarmPanelUid(uid)) {
      continue;
    }
    addUniqueString(uids, uid);
  }

  file.close();
  return uids;
}

bool writeKnownAlarmPanelUids(const std::vector<String>& uids) {
  File file = LittleFS.open(KNOWN_ALARM_PANELS_FILE, "w");
  if (!file) {
    Serial.println("[ALARM][PANELS] No se pudo escribir known_alarm_panels.txt");
    return false;
  }

  for (const String& uid : uids) {
    file.println(uid);
  }

  file.close();
  return true;
}

void rememberKnownAlarmPanel(const String& uid) {
  const String cleaned = normalizeAlarmPanelUid(uid);
  if (cleaned.isEmpty()) return;

  std::vector<String> uids = readKnownAlarmPanelUids();
  if (!addUniqueString(uids, cleaned)) return;

  if (writeKnownAlarmPanelUids(uids)) {
    Serial.printf("[ALARM][PANELS] Registrado panel %s\n", cleaned.c_str());
  }
}

void forgetKnownAlarmPanel(const String& uid) {
  const String cleaned = normalizeAlarmPanelUid(uid);
  if (cleaned.isEmpty()) return;

  std::vector<String> uids = readKnownAlarmPanelUids();
  const auto newEnd = std::remove_if(uids.begin(), uids.end(), [&](const String& item) {
    return normalizeAlarmPanelUid(item).equalsIgnoreCase(cleaned);
  });
  if (newEnd == uids.end()) return;
  uids.erase(newEnd, uids.end());

  if (writeKnownAlarmPanelUids(uids)) {
    Serial.printf("[ALARM][PANELS] Eliminado panel %s\n", cleaned.c_str());
  }
}

void loadKnownAlarmPanelsFromStorage() {
  std::vector<String> uids = readKnownAlarmPanelUids();

  for (const String& uid : uids) {
    AlarmPanelInfo& panel = alarmPanels[uid];
    if (panel.uid.isEmpty()) {
      loadAlarmPanelConfig(uid, panel);
    }
    panel.uid = uid;
    panel.active = false;
    panel.activeType = "";
    panel.lastSeenEpoch = 0;
    panel.lastSeenMs = 0;
  }

  Serial.printf("[ALARM][PANELS] Persistidos cargados: %u\n", static_cast<unsigned>(uids.size()));
}

AlarmPanelInfo& ensureAlarmPanel(const String& uid) {
  const String cleaned = normalizeAlarmPanelUid(uid);
  AlarmPanelInfo& panel = alarmPanels[cleaned];
  if (panel.uid.isEmpty()) {
    loadAlarmPanelConfig(cleaned, panel);
    rememberKnownAlarmPanel(cleaned);
  }
  return panel;
}

bool isAlarmPanelOnline(const AlarmPanelInfo& panel) {
  if (masterTimeIsValid() && panel.lastSeenEpoch != 0) {
    const uint32_t nowEpoch = masterNowEpoch();
    return nowEpoch >= panel.lastSeenEpoch
      ? (nowEpoch - panel.lastSeenEpoch) <= ALARM_PANEL_OFFLINE_THRESHOLD_SEC
      : true;
  }

  if (panel.lastSeenMs == 0) return false;
  return (millis() - panel.lastSeenMs) <= ALARM_PANEL_OFFLINE_THRESHOLD_MS;
}

String alarmPanelDisplayName(const String& uidOrSource) {
  const String uid = normalizeAlarmPanelUid(uidOrSource);
  if (uid.isEmpty() || !isValidAlarmPanelUid(uid)) {
    String source = uidOrSource;
    source.trim();
    return source.isEmpty() ? "Panel alarma" : source;
  }

  auto it = alarmPanels.find(uid);
  if (it != alarmPanels.end() && !it->second.name.isEmpty()) {
    return it->second.name;
  }
  return defaultAlarmPanelName(uid);
}

String alarmPanelDisplayZone(const String& uid, String fallbackZone) {
  const String cleaned = normalizeAlarmPanelUid(uid);
  if (!cleaned.isEmpty() && isValidAlarmPanelUid(cleaned)) {
    auto it = alarmPanels.find(cleaned);
    if (it != alarmPanels.end() && !it->second.zone.isEmpty()) {
      return it->second.zone;
    }
  }

  fallbackZone.trim();
  return fallbackZone.isEmpty() ? "Zona sin asignar" : fallbackZone;
}

String alarmPanelDisplayLocation(const String& uid) {
  const String cleaned = normalizeAlarmPanelUid(uid);
  if (!isValidAlarmPanelUid(cleaned)) return "";
  auto it = alarmPanels.find(cleaned);
  if (it == alarmPanels.end()) return "";
  return it->second.location;
}

void touchAlarmPanelStatus(const String& uid,
                           uint16_t radioId,
                           bool active,
                           const String& activeType,
                           int batteryPercent = -1) {
  const String cleaned = normalizeAlarmPanelUid(uid);
  if (cleaned.isEmpty()) return;

  AlarmPanelInfo& panel = ensureAlarmPanel(cleaned);
  panel.uid = cleaned;
  if (radioId != 0) panel.radioId = radioId;
  if (batteryPercent >= 0 && batteryPercent <= 100) panel.batteryPercent = batteryPercent;
  panel.active = active;
  panel.activeType = activeType;
  panel.lastSeenMs = millis();
  if (masterTimeIsValid()) {
    panel.lastSeenEpoch = masterNowEpoch();
  }
}

// =====================================================
// PERSISTENCIA
// =====================================================
void loadNodeConfig(const String& mac, NodeData& n, const String& fallbackName) {
  String ns = macToNs(mac);
  prefs.begin(ns.c_str(), true);

  String cfgName = prefs.getString("name", fallbackName.length() ? fallbackName : DEFAULT_SENSOR_NAME);
  String cfgMachine = prefs.getString("machine", "");
  float cfgOffset = prefs.getFloat("offset", DEFAULT_TEMP_OFFSET);
  float cfgOffsetBaseRaw = prefs.getFloat("off_base", NAN);
  float cfgOffsetCalibrationRaw = prefs.getFloat("off_cal", NAN);
  uint32_t cfgSleep = prefs.getUInt("sleep", DEFAULT_SLEEP_TIME);
  float cfgThresholdLow = prefs.getFloat("th_low", DEFAULT_THRESHOLD_LOW);
  float cfgThresholdHigh = prefs.getFloat("th_high", DEFAULT_THRESHOLD_HIGH);
  bool cfgAlarmSilenced = prefs.getBool("alm_sil", false);

  prefs.end();

  if (cfgSleep < 5) cfgSleep = 5;
  if (cfgSleep > 7200) cfgSleep = 7200;

  if (cfgThresholdLow >= cfgThresholdHigh) {
    cfgThresholdLow = DEFAULT_THRESHOLD_LOW;
    cfgThresholdHigh = DEFAULT_THRESHOLD_HIGH;
  }

  n.name = cfgName;
  n.machine = cfgMachine;  // Correcto
  n.tempOffset = cfgOffset;
  n.offsetBaseRaw = cfgOffsetBaseRaw;
  n.offsetCalibrationRaw = cfgOffsetCalibrationRaw;
  n.sleepSec = cfgSleep;
  n.thresholdLow = cfgThresholdLow;
  n.thresholdHigh = cfgThresholdHigh;
  n.alarmSilenced = cfgAlarmSilenced;
}

void saveNodeConfig(const String& mac, const String& name, const String& machine, float offset, float offsetBaseRaw, float offsetCalibrationRaw, uint32_t sleepSec, float thresholdLow, float thresholdHigh) {
  ensureSensorDirectory(mac);
  rememberKnownNode(mac);

  String ns = macToNs(mac);
  prefs.begin(ns.c_str(), false);
  prefs.putString("name", name);
  prefs.putString("machine", machine);
  prefs.putFloat("offset", offset);
  prefs.putFloat("off_base", offsetBaseRaw);
  prefs.putFloat("off_cal", offsetCalibrationRaw);
  prefs.putUInt("sleep", sleepSec);
  prefs.putFloat("th_low", thresholdLow);
  prefs.putFloat("th_high", thresholdHigh);
  prefs.end();
}

void saveNodeAlarmSilence(const String& mac, bool silenced) {
  String ns = macToNs(mac);
  prefs.begin(ns.c_str(), false);
  prefs.putBool("alm_sil", silenced);
  prefs.end();
}

void deleteAlarmPanelConfig(const String& uid) {
  const String cleaned = normalizeAlarmPanelUid(uid);
  if (!isValidAlarmPanelUid(cleaned)) return;
  String ns = alarmPanelUidToNs(cleaned);
  prefs.begin(ns.c_str(), false);
  prefs.clear();
  prefs.end();
}

void deleteNodeConfig(const String& mac) {
  String ns = macToNs(mac);
  prefs.begin(ns.c_str(), false);
  prefs.clear();
  prefs.end();
}

bool deleteSensorHistory(const String& mac) {
  String ns = macToNs(mac);
  String dirPath = "/sensor_" + ns;
  FS& fs = historyFS();

  if (!fs.exists(dirPath)) {
    return true;
  }

  File dir = fs.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  std::vector<String> pathsToDelete;
  File file = dir.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      pathsToDelete.push_back(String(file.path()));
    }
    file.close();
    file = dir.openNextFile();
  }
  dir.close();

  bool ok = true;
  for (const String& path : pathsToDelete) {
    if (!fs.remove(path)) {
      Serial.printf("[DELETE] No se pudo eliminar archivo: %s\n", path.c_str());
      ok = false;
    }
  }

  if (!fs.rmdir(dirPath.c_str())) {
    Serial.printf("[DELETE] No se pudo eliminar directorio: %s\n", dirPath.c_str());
    ok = false;
  }

  return ok;
}

bool deleteKnownNode(const String& mac) {
  String normalized = normalizeMac(mac);
  if (normalized.isEmpty()) return false;

  bool historyOk = deleteSensorHistory(normalized);
  deleteNodeConfig(normalized);
  forgetKnownNode(normalized);
  nodes.erase(normalized);
  updateCriticalAlarmOutput();

  return historyOk;
}

bool deleteKnownAlarmPanel(const String& uid) {
  const String cleaned = normalizeAlarmPanelUid(uid);
  if (!isValidAlarmPanelUid(cleaned)) return false;

  deleteAlarmPanelConfig(cleaned);
  forgetKnownAlarmPanel(cleaned);
  alarmPanels.erase(cleaned);
  return true;
}

// =====================================================
// ONLINE
// =====================================================
uint32_t offlineThresholdMs(const NodeData& n) {
  uint32_t s = n.sleepSec;
  if (s < 5) s = DEFAULT_SLEEP_TIME;
  return s * 1000UL + 60000UL;
}

bool isNodeOnline(const NodeData& n) {
  if (n.lastSeen == 0) return false;
  return (millis() - n.lastSeen) <= offlineThresholdMs(n);
}

bool isNodeInCriticalRange(const NodeData& n) {
  if (!isNodeOnline(n)) return false;
  if (!isfinite(n.temperature)) return false;
  return n.temperature >= n.thresholdHigh;
}

bool isNodeInWarningRange(const NodeData& n) {
  if (!isNodeOnline(n)) return false;
  if (!isfinite(n.temperature)) return false;
  return n.temperature >= n.thresholdLow && n.temperature < n.thresholdHigh;
}

bool isNodeCritical(const NodeData& n) {
  return isNodeInCriticalRange(n) && !n.alarmSilenced;
}

void updateCriticalAlarmOutput() {
  bool anyCritical = false;

  for (const auto& kv : nodes) {
    const NodeData& n = kv.second;
    if (isNodeCritical(n)) {
      anyCritical = true;
      break;
    }
  }

  setAlarmIndicators(anyCritical);
}

int countOnlineNodes() {
  int onlineCount = 0;
  for (const auto& kv : nodes) {
    if (isNodeOnline(kv.second)) {
      onlineCount++;
    }
  }
  return onlineCount;
}

int countCriticalNodes() {
  int criticalCount = 0;
  for (const auto& kv : nodes) {
    if (isNodeCritical(kv.second)) {
      criticalCount++;
    }
  }
  return criticalCount;
}

String formatBytesCompact(uint64_t bytes) {
  if (bytes >= (1024ULL * 1024ULL)) {
    return String((double)bytes / (1024.0 * 1024.0), 1) + "M";
  }
  return String((bytes + 1023ULL) / 1024ULL) + "K";
}

String uint64ToString(uint64_t value) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
  return String(buf);
}

String normalizeEmergencyType(String rawType) {
  rawType.trim();
  rawType.toUpperCase();
  rawType.replace("Á", "A");
  rawType.replace("É", "E");
  rawType.replace("Í", "I");
  rawType.replace("Ó", "O");
  rawType.replace("Ú", "U");

  if (rawType == "FIRE" || rawType == "INCENDIO") return "FIRE";
  if (rawType == "ACCIDENT" || rawType == "ACCIDENTE") return "ACCIDENT";
  if (rawType == "EVAC" || rawType == "EVACUATION" || rawType == "EVACUACION") return "EVACUATION";
  if (rawType == "MEDICAL" || rawType == "MEDIC" || rawType == "AUXILIO" || rawType == "AUXILIO_MEDICO") return "MEDICAL";
  return rawType.isEmpty() ? "UNKNOWN" : rawType;
}

bool isKnownEmergencyType(const String& normalizedType) {
  return normalizedType == "FIRE"
      || normalizedType == "ACCIDENT"
      || normalizedType == "EVACUATION"
      || normalizedType == "MEDICAL";
}

uint8_t emergencyTypeCode(const String& normalizedType) {
  if (normalizedType == "FIRE") return 1;
  if (normalizedType == "ACCIDENT") return 2;
  if (normalizedType == "EVACUATION") return 3;
  if (normalizedType == "MEDICAL") return 4;
  return 0;
}

String emergencyTypeFromCode(uint8_t code) {
  switch (code) {
    case 1: return "FIRE";
    case 2: return "ACCIDENT";
    case 3: return "EVACUATION";
    case 4: return "MEDICAL";
    default: return "NONE";
  }
}

String emergencyModeForType(const String& normalizedType) {
  if (normalizedType == "FIRE") return "PATRON_1";
  if (normalizedType == "ACCIDENT") return "PATRON_2";
  if (normalizedType == "EVACUATION") return "PATRON_3";
  if (normalizedType == "MEDICAL") return "PATRON_4";
  return "PATRON_0";
}

String emergencyModeFromCode(uint8_t code) {
  switch (code) {
    case 1: return "PATRON_1";
    case 2: return "PATRON_2";
    case 3: return "PATRON_3";
    case 4: return "PATRON_4";
    default: return "PATRON_0";
  }
}

String controlAckReasonFromCode(uint8_t code) {
  switch (code) {
    case 1: return "accepted";
    case 2: return "uid_mismatch";
    case 3: return "type_mismatch";
    default: return "no_active_alarm";
  }
}

String emergencyDisplayZone(String zone) {
  zone.trim();
  return zone.isEmpty() ? "Zona desconocida" : zone;
}

String emergencyDisplaySource(String sourceId) {
  sourceId.trim();
  return sourceId.isEmpty() ? "MOD-EMG" : sourceId;
}

String emergencyStateKey(const String& uid, const String& sourceId, const String& zone) {
  const String cleanedUid = normalizeAlarmPanelUid(uid);
  if (!cleanedUid.isEmpty() && isValidAlarmPanelUid(cleanedUid)) return cleanedUid;
  return emergencyDisplaySource(sourceId) + "|" + emergencyDisplayZone(zone);
}

uint32_t emergencyNowEpoch() {
  return masterTimeIsValid() ? masterNowEpoch() : 0;
}

bool parseEmergencyActiveValue(String rawValue, bool& out) {
  rawValue.trim();
  rawValue.toUpperCase();
  if (rawValue == "1" || rawValue == "ON" || rawValue == "TRUE" || rawValue == "ACTIVE") {
    out = true;
    return true;
  }
  if (rawValue == "0" || rawValue == "OFF" || rawValue == "FALSE" || rawValue == "CLEAR" || rawValue == "IDLE") {
    out = false;
    return true;
  }
  return false;
}

String emergencyDirectoryPath() {
  return "/emergencias";
}

void ensureEmergencyDirectory() {
  FS& fs = historyFS();
  String dirPath = emergencyDirectoryPath();
  if (!fs.exists(dirPath)) {
    if (fs.mkdir(dirPath)) {
      Serial.printf("[%s] Creado directorio: %s\n", historyStorageLabel(), dirPath.c_str());
    } else {
      Serial.printf("[%s] ERROR: No se pudo crear directorio: %s\n", historyStorageLabel(), dirPath.c_str());
    }
  }
}

String getEmergencyDailyFileName(uint32_t epochUtc) {
  String date = formatDateFromEpoch(epochUtc);
  if (date.isEmpty()) date = formatDateFromEpoch(masterNowEpoch());
  if (date.isEmpty()) date = "sin_fecha";
  return emergencyDirectoryPath() + "/" + date + ".txt";
}

String emergencyPipeEscape(String value) {
  value.replace("|", "/");
  value.replace("\r", " ");
  value.replace("\n", " ");
  value.trim();
  return value;
}

String pipeField(const String& line, uint8_t index) {
  int start = 0;
  for (uint8_t i = 0; i < index; i++) {
    int pos = line.indexOf('|', start);
    if (pos < 0) return "";
    start = pos + 1;
  }
  int end = line.indexOf('|', start);
  if (end < 0) end = line.length();
  return line.substring(start, end);
}

void persistEmergencyHistoryEntry(const EmergencyHistoryEntry& entry) {
  ensureEmergencyDirectory();
  FS& fs = historyFS();
  uint32_t fileEpoch = entry.timestampEpoch != 0 ? entry.timestampEpoch : emergencyNowEpoch();
  String fileName = getEmergencyDailyFileName(fileEpoch);
  File file = fs.open(fileName, FILE_APPEND);
  if (!file) {
    Serial.printf("[%s] ERROR: No se pudo abrir historial emergencia: %s\n", historyStorageLabel(), fileName.c_str());
    return;
  }

  file.printf("%lu|%s|%s|%s|%s|%u|%lu|%lu|%lu\n",
              (unsigned long)entry.eventId,
              emergencyPipeEscape(entry.sourceId).c_str(),
              emergencyPipeEscape(entry.zone).c_str(),
              emergencyPipeEscape(entry.type).c_str(),
              emergencyPipeEscape(entry.mode).c_str(),
              entry.active ? 1 : 0,
              (unsigned long)entry.timestampEpoch,
              (unsigned long)entry.clearedEpoch,
              (unsigned long)entry.updatedEpoch);
  file.close();
}

EmergencyHistoryEntry parseEmergencyHistoryLine(String line) {
  line.trim();
  EmergencyHistoryEntry entry;
  entry.eventId = (uint32_t)pipeField(line, 0).toInt();
  entry.sourceId = pipeField(line, 1);
  entry.zone = pipeField(line, 2);
  entry.type = pipeField(line, 3);
  entry.mode = pipeField(line, 4);
  entry.active = pipeField(line, 5).toInt() != 0;
  entry.timestampEpoch = (uint32_t)pipeField(line, 6).toInt();
  entry.clearedEpoch = (uint32_t)pipeField(line, 7).toInt();
  entry.updatedEpoch = (uint32_t)pipeField(line, 8).toInt();
  return entry;
}

void mergeEmergencyEntry(std::vector<EmergencyHistoryEntry>& entries, const EmergencyHistoryEntry& incoming) {
  if (incoming.eventId == 0) return;
  for (EmergencyHistoryEntry& existing : entries) {
    if (existing.eventId == incoming.eventId) {
      if (!incoming.sourceId.isEmpty()) existing.sourceId = incoming.sourceId;
      if (!incoming.zone.isEmpty()) existing.zone = incoming.zone;
      if (!incoming.type.isEmpty()) existing.type = incoming.type;
      if (!incoming.mode.isEmpty()) existing.mode = incoming.mode;
      existing.active = incoming.active;
      if (incoming.timestampEpoch != 0) existing.timestampEpoch = incoming.timestampEpoch;
      if (incoming.clearedEpoch != 0) existing.clearedEpoch = incoming.clearedEpoch;
      if (incoming.updatedEpoch != 0) existing.updatedEpoch = incoming.updatedEpoch;
      return;
    }
  }
  entries.insert(entries.begin(), incoming);
}

String emergencyHistoryEntryJson(const EmergencyHistoryEntry& e) {
  String uid = normalizeAlarmPanelUid(e.sourceId);
  if (!isValidAlarmPanelUid(uid)) uid = "";
  const String sourceLabel = alarmPanelDisplayName(uid.isEmpty() ? e.sourceId : uid);
  const String zoneLabel = alarmPanelDisplayZone(uid, e.zone);
  const String location = alarmPanelDisplayLocation(uid);
  String out = "{";
  out += "\"id\":" + String(e.eventId) + ",";
  out += "\"sourceId\":\"" + jsonEscape(e.sourceId) + "\",";
  out += "\"uid\":\"" + jsonEscape(uid) + "\",";
  out += "\"sourceLabel\":\"" + jsonEscape(sourceLabel) + "\",";
  out += "\"zone\":\"" + jsonEscape(zoneLabel) + "\",";
  out += "\"location\":\"" + jsonEscape(location) + "\",";
  out += "\"type\":\"" + jsonEscape(e.type) + "\",";
  out += "\"mode\":\"" + jsonEscape(e.mode) + "\",";
  out += "\"active\":";
  out += (e.active ? "true" : "false");
  out += ",";
  out += "\"timestampEpoch\":" + String(e.timestampEpoch) + ",";
  out += "\"clearedEpoch\":" + String(e.clearedEpoch) + ",";
  out += "\"updatedEpoch\":" + String(e.updatedEpoch);
  out += "}";
  return out;
}

String getEmergencyDatesJson() {
  ensureEmergencyDirectory();
  FS& fs = historyFS();
  File dir = fs.open(emergencyDirectoryPath());
  if (!dir || !dir.isDirectory()) {
    return "{\"dates\":[]}";
  }

  std::vector<String> dates;
  File file = dir.openNextFile();
  while (file) {
    String name = pathBaseName(file.name());
    if (!file.isDirectory() && name.endsWith(".txt")) {
      dates.push_back(name.substring(0, name.length() - 4));
    }
    file.close();
    file = dir.openNextFile();
  }
  dir.close();
  std::sort(dates.begin(), dates.end(), std::greater<String>());

  String out = "{\"dates\":[";
  for (size_t i = 0; i < dates.size(); i++) {
    if (i) out += ",";
    out += "\"" + jsonEscape(dates[i]) + "\"";
  }
  out += "]}";
  return out;
}

String getEmergencyHistoryJson(const String& date) {
  ensureEmergencyDirectory();
  FS& fs = historyFS();
  String fileName = emergencyDirectoryPath() + "/" + date + ".txt";
  File file = fs.open(fileName, FILE_READ);
  if (!file) {
    return "[]";
  }

  std::vector<EmergencyHistoryEntry> entries;
  while (file.available()) {
    mergeEmergencyEntry(entries, parseEmergencyHistoryLine(file.readStringUntil('\n')));
  }
  file.close();

  std::sort(entries.begin(), entries.end(), [](const EmergencyHistoryEntry& a, const EmergencyHistoryEntry& b) {
    uint32_t au = a.updatedEpoch != 0 ? a.updatedEpoch : a.timestampEpoch;
    uint32_t bu = b.updatedEpoch != 0 ? b.updatedEpoch : b.timestampEpoch;
    if (au != bu) return au > bu;
    return a.eventId > b.eventId;
  });

  String out = "[";
  for (size_t i = 0; i < entries.size(); i++) {
    if (i) out += ",";
    out += emergencyHistoryEntryJson(entries[i]);
  }
  out += "]";
  return out;
}

void appendEmergencyHistory(const EmergencyState& state, bool active) {
  EmergencyHistoryEntry entry;
  entry.eventId = state.eventId;
  entry.sourceId = state.sourceId;
  entry.zone = state.zone;
  entry.type = state.type;
  entry.mode = state.mode;
  entry.active = active;
  entry.timestampEpoch = state.firstSeenEpoch != 0 ? state.firstSeenEpoch : emergencyNowEpoch();
  entry.clearedEpoch = active ? 0 : emergencyNowEpoch();
  entry.updatedEpoch = active ? state.updatedEpoch : entry.clearedEpoch;

  mergeEmergencyEntry(emergencyHistory, entry);
  persistEmergencyHistoryEntry(entry);

  if (emergencyHistory.size() > 20) {
    emergencyHistory.resize(20);
  }
}

int countActiveEmergencies() {
  int count = 0;
  for (const auto& kv : emergencyStates) {
    if (kv.second.active) count++;
  }
  return count;
}

void processAlarmPanelLine(const String& line) {
  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'P' || fieldCount < 6) return;

  uint32_t addrValue = 0;
  uint32_t activeValue = 0;
  uint32_t typeCodeValue = 0;
  uint32_t seq = 0;
  int batteryPercent = -1;
  if (!parseProtocolUInt(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (!parseProtocolUInt(fields[2], activeValue)) return;
  if (!parseProtocolUInt(fields[3], typeCodeValue) || typeCodeValue > 0xFFUL) return;
  if (!parseProtocolUInt(fields[5], seq)) return;
  if (fieldCount >= 7) {
    uint32_t batteryValue = 0;
    if (parseProtocolUInt(fields[6], batteryValue) && batteryValue <= 100UL) {
      batteryPercent = static_cast<int>(batteryValue);
    }
  }

  const uint16_t radioId = static_cast<uint16_t>(addrValue);
  const String panelKey = normalizeAlarmPanelUid(String(radioId));
  if (!isValidAlarmPanelUid(panelKey)) return;

  const bool active = activeValue != 0;
  String type = normalizeEmergencyType(emergencyTypeFromCode(static_cast<uint8_t>(typeCodeValue)));
  if (active && !isKnownEmergencyType(type)) return;
  if (type.isEmpty() || type == "NONE") type = "";

  touchAlarmPanelStatus(panelKey, radioId, active, type, batteryPercent);
  if (batteryPercent >= 0) {
    Serial.printf("[ALARM][PANEL] ADDR=%u ACTIVE=%d TYPE=%s BAT=%d%%\n",
                  static_cast<unsigned>(radioId),
                  active ? 1 : 0,
                  type.isEmpty() ? "NONE" : type.c_str(),
                  batteryPercent);
  } else {
    Serial.printf("[ALARM][PANEL] ADDR=%u ACTIVE=%d TYPE=%s\n",
                  static_cast<unsigned>(radioId),
                  active ? 1 : 0,
                  type.isEmpty() ? "NONE" : type.c_str());
  }

  if (seq != 0) {
    const String ack = buildProtocolFrame(String("K,") + String(radioId) + ",P," + String(seq));
    sendToNode(ALARM_MODULE_ADDR, MASTER_CH, ack);
    MASTER_DEBUG_LOGF("[ALARM][PANEL ACK] %s\n", ack.c_str());
  }
  pushSnapshot();
}

void processAlarmModuleLine(const String& line) {
  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'M' || fieldCount < 6) return;

  uint32_t addrValue = 0;
  uint32_t typeCodeValue = 0;
  uint32_t modeCodeValue = 0;
  uint32_t activeValue = 0;
  uint32_t seq = 0;
  if (!parseProtocolUInt(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (!parseProtocolUInt(fields[2], typeCodeValue) || typeCodeValue > 0xFFUL) return;
  if (!parseProtocolUInt(fields[3], modeCodeValue) || modeCodeValue > 0xFFUL) return;
  if (!parseProtocolUInt(fields[4], activeValue)) return;
  if (!parseProtocolUInt(fields[5], seq)) return;

  const uint16_t sourceNumeric = static_cast<uint16_t>(addrValue);
  const String panelUid = sourceNumeric == 0 ? String("") : normalizeAlarmPanelUid(String(sourceNumeric));
  if (sourceNumeric != 0 && !isValidAlarmPanelUid(panelUid)) return;

  String type = normalizeEmergencyType(emergencyTypeFromCode(static_cast<uint8_t>(typeCodeValue)));
  if (!isKnownEmergencyType(type)) return;
  String mode = emergencyModeFromCode(static_cast<uint8_t>(modeCodeValue));
  bool active = activeValue != 0;

  if (sourceNumeric != 0) {
    touchAlarmPanelStatus(panelUid, sourceNumeric, active, type);
  }
  const String rawSource = sourceNumeric == 0 ? String("CENTRAL") : panelUid;
  const String zone = sourceNumeric == 0 ? String("Central") : alarmPanelDisplayZone(panelUid, "");
  const String stateKey = emergencyStateKey(panelUid, rawSource, zone);
  if (stateKey.isEmpty()) return;

  auto existing = emergencyStates.find(stateKey);
  const uint32_t nowEpoch = emergencyNowEpoch();

  if (active) {
    EmergencyState state;
    if (existing != emergencyStates.end()) {
      state = existing->second;
    }

    const bool needsNewEventId = !state.active || state.type != type || state.zone != zone;
    const bool shouldRecordActive = needsNewEventId || state.eventId == 0;
    if (needsNewEventId || state.eventId == 0) {
      state.eventId = g_nextEmergencyEventId++;
      state.firstSeenEpoch = nowEpoch;
    }

    state.sourceId = rawSource;
    state.zone = zone;
    state.type = type;
    state.mode = mode;
    state.active = true;
    state.updatedEpoch = nowEpoch;
    emergencyStates[stateKey] = state;
    if (shouldRecordActive) {
      appendEmergencyHistory(state, true);
    }
  } else {
    if (existing == emergencyStates.end()) {
      return;
    }

    EmergencyState cleared = existing->second;
    cleared.updatedEpoch = nowEpoch;
    appendEmergencyHistory(cleared, false);
    emergencyStates.erase(existing);
  }

  Serial.printf("[ALARM][MODULE] ADDR=%u ACTIVE=%d TYPE=%s MODE=%s\n",
                static_cast<unsigned>(sourceNumeric),
                active ? 1 : 0,
                type.isEmpty() ? "NONE" : type.c_str(),
                mode.isEmpty() ? "NONE" : mode.c_str());

  if (seq != 0) {
    const String ack = buildProtocolFrame(String("K,") + String(sourceNumeric) + ",M," + String(seq));
    for (uint8_t i = 0; i < 3; ++i) {
      sendToNode(ALARM_MODULE_ADDR, MASTER_CH, ack);
      MASTER_DEBUG_LOGF("[ALARM][MODULE ACK #%u] %s\n", static_cast<unsigned>(i + 1), ack.c_str());
      if ((i + 1) < 3) delay(300);
    }
  }

  pushSnapshot();
}

void processAlarmControlAckLine(const String& line) {
  String fields[8];
  int fieldCount = 0;
  char cmd = '\0';
  if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;
  if (cmd != 'U' || fieldCount < 5) return;

  uint32_t addrValue = 0;
  uint32_t typeCodeValue = 0;
  uint32_t resultCodeValue = 0;
  uint32_t reqId = 0;
  if (!parseProtocolUInt(fields[1], addrValue) || addrValue > 0xFFFFUL) return;
  if (!parseProtocolUInt(fields[2], typeCodeValue) || typeCodeValue > 0xFFUL) return;
  if (!parseProtocolUInt(fields[3], resultCodeValue) || resultCodeValue > 0xFFUL) return;
  if (!parseProtocolUInt(fields[4], reqId)) return;

  const uint16_t sourceAddr = static_cast<uint16_t>(addrValue);
  const String sourceId = sourceAddr == 0 ? String("CENTRAL") : normalizeAlarmPanelUid(String(sourceAddr));
  const String zone = sourceAddr == 0 ? String("Central") : alarmPanelDisplayZone(sourceId, "");
  const String type = normalizeEmergencyType(emergencyTypeFromCode(static_cast<uint8_t>(typeCodeValue)));
  const String reason = controlAckReasonFromCode(static_cast<uint8_t>(resultCodeValue));
  const bool ok = static_cast<uint8_t>(resultCodeValue) == 1;
  const bool treatAsCleared = ok || static_cast<uint8_t>(resultCodeValue) == 0;

  Serial.printf("[ALARM][CTRL ACK] req=%lu ok=%d src=%s zone=%s type=%s reason=%s\n",
                static_cast<unsigned long>(reqId),
                ok ? 1 : 0,
                sourceId.c_str(),
                zone.c_str(),
                type.c_str(),
                reason.c_str());

  if (!treatAsCleared) {
    return;
  }

  const uint32_t nowEpoch = emergencyNowEpoch();
  bool changed = false;

  if (!sourceId.isEmpty()) {
    auto panelIt = alarmPanels.find(sourceId);
    if (panelIt != alarmPanels.end()) {
      if (type.isEmpty() || panelIt->second.activeType.equalsIgnoreCase(type)) {
        panelIt->second.active = false;
        panelIt->second.activeType = "";
        changed = true;
      }
    }
  }

  for (auto it = emergencyStates.begin(); it != emergencyStates.end(); ) {
    EmergencyState& state = it->second;
    const String stateUid = normalizeAlarmPanelUid(state.sourceId);
    const bool uidMatches = !sourceId.isEmpty()
      && (stateUid.equalsIgnoreCase(sourceId) || state.sourceId.equalsIgnoreCase(sourceId));
    const bool zoneMatches = zone.isEmpty() || state.zone.equalsIgnoreCase(zone);
    const bool typeMatches = type.isEmpty() || state.type.equalsIgnoreCase(type);

    if (uidMatches && zoneMatches && typeMatches) {
      EmergencyState cleared = state;
      cleared.updatedEpoch = nowEpoch;
      appendEmergencyHistory(cleared, false);
      it = emergencyStates.erase(it);
      changed = true;
      continue;
    }

    ++it;
  }

  if (changed) {
    pushSnapshot();
  }
}

// =====================================================
// JSON SNAPSHOT
// =====================================================
String buildJsonSnapshot() {
  String out;
  out.reserve(12288);

  const int onlineCount = countOnlineNodes();
  const int emergencyActiveCount = countActiveEmergencies();
  const uint64_t activeStorageTotal = historyTotalBytes();
  const uint64_t activeStorageUsed = historyUsedBytes();
  const uint64_t activeStorageFree = historyFreeBytes();

  out += "{";
  out += "\"ip\":\"sensores.local\",";
  out += "\"ip_numerica\":\"" + WiFi.softAPIP().toString() + "\",";
  out += "\"ssid\":\"" + jsonEscape(String(AP_SSID)) + "\",";
  out += "\"uptime\":\"" + formatUptime() + "\",";
  out += "\"nowEpoch\":" + String(masterNowEpoch()) + ",";
  out += "\"nowDate\":\"" + jsonEscape(formatDateFromEpoch(masterNowEpoch())) + "\",";
  out += "\"nowLabel\":\"" + jsonEscape(formatTimeAmPmFromEpoch(masterNowEpoch())) + "\",";
  out += "\"timeSynced\":";
  out += (masterTimeIsValid() ? "true" : "false");
  out += ",";
  out += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  // Compatibility alias: the current web UI reads littlefs* to render the
  // storage card. When the history backend moves to SD, expose the active
  // storage there too so the page immediately shows the SD capacity.
  out += "\"littlefsTotal\":" + uint64ToString(activeStorageTotal) + ",";
  out += "\"littlefsUsed\":" + uint64ToString(activeStorageUsed) + ",";
  out += "\"littlefsFree\":" + uint64ToString(activeStorageFree) + ",";
  out += "\"littlefsPhysicalTotal\":" + String(LittleFS.totalBytes()) + ",";
  out += "\"littlefsPhysicalUsed\":" + String(LittleFS.usedBytes()) + ",";
  out += "\"littlefsPhysicalFree\":" + String(LittleFS.totalBytes() - LittleFS.usedBytes()) + ",";
  out += "\"sdMounted\":";
  out += (g_sdMounted ? "true" : "false");
  out += ",";
  out += "\"historyStorage\":\"" + String(historyStorageLabel()) + "\",";
  out += "\"historyTotal\":" + uint64ToString(activeStorageTotal) + ",";
  out += "\"historyUsed\":" + uint64ToString(activeStorageUsed) + ",";
  out += "\"historyFree\":" + uint64ToString(activeStorageFree) + ",";
  out += "\"emergencyActiveCount\":" + String(emergencyActiveCount) + ",";
  out += "\"emergenciesActive\":[";

  std::vector<const EmergencyState*> activeEmergencies;
  activeEmergencies.reserve(emergencyStates.size());
  for (const auto& kv : emergencyStates) {
    if (kv.second.active) {
      activeEmergencies.push_back(&kv.second);
    }
  }
  std::sort(activeEmergencies.begin(), activeEmergencies.end(), [](const EmergencyState* a, const EmergencyState* b) {
    if (a->updatedEpoch != b->updatedEpoch) return a->updatedEpoch > b->updatedEpoch;
    return a->eventId > b->eventId;
  });

  bool firstEmergency = true;
  for (const EmergencyState* e : activeEmergencies) {
    String uid = normalizeAlarmPanelUid(e->sourceId);
    if (!isValidAlarmPanelUid(uid)) uid = "";
    const String sourceLabel = alarmPanelDisplayName(uid.isEmpty() ? e->sourceId : uid);
    const String zoneLabel = alarmPanelDisplayZone(uid, e->zone);
    const String location = alarmPanelDisplayLocation(uid);
    if (!firstEmergency) out += ",";
    firstEmergency = false;
    out += "{";
    out += "\"id\":" + String(e->eventId) + ",";
    out += "\"sourceId\":\"" + jsonEscape(e->sourceId) + "\",";
    out += "\"uid\":\"" + jsonEscape(uid) + "\",";
    out += "\"sourceLabel\":\"" + jsonEscape(sourceLabel) + "\",";
    out += "\"zone\":\"" + jsonEscape(zoneLabel) + "\",";
    out += "\"location\":\"" + jsonEscape(location) + "\",";
    out += "\"type\":\"" + jsonEscape(e->type) + "\",";
    out += "\"mode\":\"" + jsonEscape(e->mode) + "\",";
    out += "\"active\":true,";
    out += "\"firstSeenEpoch\":" + String(e->firstSeenEpoch) + ",";
    out += "\"updatedEpoch\":" + String(e->updatedEpoch);
    out += "}";
  }
  out += "],";

  out += "\"emergencyHistory\":[";
  bool firstEmergencyHistory = true;
  for (const EmergencyHistoryEntry& e : emergencyHistory) {
    if (!firstEmergencyHistory) out += ",";
    firstEmergencyHistory = false;
    out += emergencyHistoryEntryJson(e);
  }
  out += "],";

  out += "\"alarmPanels\":[";
  std::vector<const AlarmPanelInfo*> panelList;
  panelList.reserve(alarmPanels.size());
  for (const auto& kv : alarmPanels) {
    panelList.push_back(&kv.second);
  }
  std::sort(panelList.begin(), panelList.end(), [](const AlarmPanelInfo* a, const AlarmPanelInfo* b) {
    const String aName = !a->name.isEmpty() ? a->name : defaultAlarmPanelName(a->uid);
    const String bName = !b->name.isEmpty() ? b->name : defaultAlarmPanelName(b->uid);
    return aName < bName;
  });

  bool firstPanel = true;
  for (const AlarmPanelInfo* panel : panelList) {
    if (!firstPanel) out += ",";
    firstPanel = false;
    out += "{";
    out += "\"uid\":\"" + jsonEscape(panel->uid) + "\",";
    out += "\"radioId\":" + String(panel->radioId) + ",";
    out += "\"name\":\"" + jsonEscape(panel->name.isEmpty() ? defaultAlarmPanelName(panel->uid) : panel->name) + "\",";
    out += "\"nameRaw\":\"" + jsonEscape(panel->name) + "\",";
    out += "\"zone\":\"" + jsonEscape(panel->zone.isEmpty() ? "Zona sin asignar" : panel->zone) + "\",";
    out += "\"zoneRaw\":\"" + jsonEscape(panel->zone) + "\",";
    out += "\"location\":\"" + jsonEscape(panel->location) + "\",";
    out += "\"configured\":";
    out += ((!panel->name.isEmpty() || !panel->zone.isEmpty() || !panel->location.isEmpty()) ? "true" : "false");
    out += ",";
    out += "\"active\":";
    out += (panel->active ? "true" : "false");
    out += ",";
    out += "\"activeType\":\"" + jsonEscape(panel->activeType) + "\",";
    out += "\"battery\":";
    out += (panel->batteryPercent >= 0 && panel->batteryPercent <= 100) ? String(panel->batteryPercent) : "null";
    out += ",";
    out += "\"online\":";
    out += (isAlarmPanelOnline(*panel) ? "true" : "false");
    out += ",";
    out += "\"lastSeenEpoch\":" + String(panel->lastSeenEpoch);
    out += "}";
  }
  out += "],";

  out += "\"nodesTotal\":" + String((int)nodes.size()) + ",";
  out += "\"nodesOnline\":" + String(onlineCount) + ",";
  out += "\"nodes\":[";

  bool firstNode = true;
  for (const auto& kv : nodes) {
    const NodeData& n = kv.second;
    const bool criticalRange = isNodeInCriticalRange(n);
    const bool alarmActive = criticalRange && !n.alarmSilenced;

    if (!firstNode) out += ",";
    firstNode = false;

    out += "{";
    out += "\"mac\":\"" + jsonEscape(n.mac) + "\",";
    out += "\"name\":\"" + jsonEscape(n.name) + "\",";
    out += "\"machine\":\"" + jsonEscape(n.machine) + "\",";
    out += "\"nodeId\":" + String(n.nodeId) + ",";
    out += "\"temperature\":";
    out += isfinite(n.temperature) ? String(n.temperature, 2) : "null";
    out += ",";
    out += "\"rawTemperature\":";
    out += isfinite(n.rawTemperature) ? String(n.rawTemperature, 2) : "null";
    out += ",";
    out += "\"battery\":";
    out += isfinite(n.battery) ? String(n.battery, 1) : "null";
    out += ",";
    out += "\"rssi\":" + String(n.rssi) + ",";
    out += "\"tempOffset\":" + String(n.tempOffset, 3) + ",";
    out += "\"offsetBaseRaw\":";
    out += isfinite(n.offsetBaseRaw) ? String(n.offsetBaseRaw, 2) : "null";
    out += ",";
    out += "\"offsetCalibrationRaw\":";
    out += isfinite(n.offsetCalibrationRaw) ? String(n.offsetCalibrationRaw, 2) : "null";
    out += ",";
    out += "\"sleepSec\":" + String(n.sleepSec) + ",";
    out += "\"thresholdLow\":" + String(n.thresholdLow, 1) + ",";
    out += "\"thresholdHigh\":" + String(n.thresholdHigh, 1) + ",";
    out += "\"remoteIp\":\"" + jsonEscape(n.remoteIp) + "\",";
    out += "\"online\":";
    out += (isNodeOnline(n) ? "true" : "false");
    out += ",";
    out += "\"critical\":";
    out += (criticalRange ? "true" : "false");
    out += ",";
    out += "\"alarmSilenced\":";
    out += (n.alarmSilenced ? "true" : "false");
    out += ",";
    out += "\"alarmActive\":";
    out += (alarmActive ? "true" : "false");
    out += ",";
    out += "\"todayMin\":";
    out += isfinite(n.todayMin) ? String(n.todayMin, 2) : "null";
    out += ",";
    out += "\"todayMax\":";
    out += isfinite(n.todayMax) ? String(n.todayMax, 2) : "null";
    out += ",";
    out += "\"todayAvg\":";
    out += (n.todaySamples > 0) ? String(n.todaySum / n.todaySamples, 2) : "null";
    out += "}";
  }

  out += "]";
  out += "}";
  return out;
}

void pushSnapshot() {
  String payload = buildJsonSnapshot();
  events.send(payload.c_str(), "snapshot", millis());
}

// =====================================================
// WIFI - MODO ACCESS POINT
// =====================================================
bool setupAccessPoint() {
  WiFi.mode(WIFI_AP);

  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("[AP] Error configurando IP fija");
    return false;
  }

  bool apStarted;
  if (strlen(AP_PASS) >= 8) {
    apStarted = WiFi.softAP(AP_SSID, AP_PASS);
  } else {
    apStarted = WiFi.softAP(AP_SSID);
  }

  if (!apStarted) {
    Serial.println("[AP] Error al iniciar el Access Point");
    return false;
  }

  Serial.println("[AP] Access Point iniciado exitosamente");
  Serial.print("[AP] SSID: ");
  Serial.println(AP_SSID);
  Serial.print("[AP] IP: ");
  Serial.println(WiFi.softAPIP());

  return true;
}

void ensureWiFiConnected() {
  if (WiFi.getMode() != WIFI_AP || WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    Serial.println("[AP] Reconfigurando modo AP...");
    setupAccessPoint();
  }
}

void setupMDNS() {
  const char* host = "sensores";

  if (!MDNS.begin(host)) {
    Serial.println("[mDNS] Error iniciando mDNS");
    return;
  }

  MDNS.addService("http", "tcp", 80);
  MDNS.setInstanceName("Monitor de Temperatura ESP32");
  Serial.printf("[mDNS] Servicio iniciado: http://%s.local\n", host);
}

void setupDNSServer() {
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.println("[DNS] Portal cautivo activado");
}

void printAccessInfo() {
  Serial.println("\n\n============================================");
  Serial.println("     MONITOR DE TEMPERATURA - ACCESO WEB");
  Serial.println("============================================");
  Serial.printf(" WiFi: %s\n", AP_SSID);
  Serial.printf(" Pass: %s\n", strlen(AP_PASS) ? AP_PASS : "(abierta)");
  Serial.println("--------------------------------------------");
  Serial.println(" DIRECCIONES DE ACCESO:");
  Serial.println(" - http://Sensores.local");
  Serial.printf(" - http://%s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("============================================\n");
}



// =====================================================
// E220
// =====================================================
void setModeConfig() {

  digitalWrite(PIN_E220_M0, HIGH);
  digitalWrite(PIN_E220_M1, HIGH);
}

void setModeNormal() {

  digitalWrite(PIN_E220_M0, LOW);
  digitalWrite(PIN_E220_M1, LOW);
}

void clearRadioInput() {
  while (Serial1.available() > 0) Serial1.read();
}

String debugRadioText(const String& text) {
  String out;
  out.reserve(text.length() * 4);

  for (size_t i = 0; i < text.length(); ++i) {
    const uint8_t b = static_cast<uint8_t>(text[i]);
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

  if (out.isEmpty()) {
    out = "<sin respuesta>";
  }
  return out;
}

#if AUTO_CONFIGURE_ON_BOOT
String readATResponse(uint32_t timeoutMs = 800) {
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
  Serial.printf("[E220][RX] %s\n", debugRadioText(out).c_str());
  return out;
}

bool sendAT(const String& cmd, const char* expected = "=OK", uint32_t timeoutMs = 800) {
  clearRadioInput();
  Serial.printf("[E220][TX] %s\n", cmd.c_str());
  Serial1.print(cmd);
  Serial1.print("\r\n");
  String resp = readATResponse(timeoutMs);
  if (!expected || expected[0] == '\0') {
    Serial.println("[E220][CHK] OK (sin validacion)");
    return true;
  }
  const bool ok = resp.indexOf(expected) >= 0;
  Serial.printf("[E220][CHK] %s esperado=%s\n", ok ? "OK" : "FAIL", expected);
  return ok;
}

bool queryAT(const String& cmd, uint32_t timeoutMs = 800) {
  clearRadioInput();
  Serial.printf("[E220][TX] %s\n", cmd.c_str());
  Serial1.print(cmd);
  Serial1.print("\r\n");
  String resp = readATResponse(timeoutMs);
  const bool ok = resp.length() > 0;
  Serial.printf("[E220][CHK] %s consulta\n", ok ? "OK" : "FAIL");
  return ok;
}

bool configureE220() {
  bool ok = true;
  ok &= queryAT("AT+DEVTYPE=?");
  ok &= queryAT("AT+FWCODE=?");
  ok &= sendAT("AT+ADDR=" + String(MASTER_ADDR));
  ok &= sendAT("AT+CHANNEL=" + String(MASTER_CH));
  ok &= sendAT("AT+UART=" + String(UART_BAUD_CODE) + "," + String(UART_PARITY_CODE));
  ok &= sendAT("AT+RATE=" + String(AIR_RATE_CODE));
  ok &= sendAT("AT+TRANS=" + String(TRANS_MODE_CODE));
  ok &= sendAT("AT+PACKET=" + String(PACKET_LEN_CODE));
  ok &= sendAT("AT+URXT=" + String(URXT_BYTE_TIME));
  ok &= sendAT("AT+KEY=" + String(KEY_CODE));
  ok &= sendAT("AT+LBT=" + String(LBT_CODE));
  ok &= sendAT("AT+POWER=" + String(POWER_CODE));
  return ok;
}

bool bootConfigureIfEnabled() {
  if (!AUTO_CONFIGURE_ON_BOOT) return true;
  Serial.println("[E220] Entrando en modo configuracion...");
  setModeConfig();
  delay(150);
  Serial1.begin(9600, SERIAL_8N1, PIN_E220_RX, PIN_E220_TX);
  delay(200);
  bool ok = configureE220();
  Serial.printf("[E220] Configuracion final: %s\n", ok ? "OK" : "ERROR");
  setModeNormal();
  delay(150);
  Serial1.end();
  delay(50);
  return ok;
}
#else
bool bootConfigureIfEnabled() {
  return true;
}
#endif

// =====================================================
// RADIO TX/RX
// =====================================================
void sendToNode(uint16_t destAddr, uint8_t destCh, const String& msg) {
  const uint8_t addrH = (destAddr >> 8) & 0xFF;
  const uint8_t addrL = destAddr & 0xFF;
  const uint8_t ch = destCh;
  Serial1.write(addrH);
  Serial1.write(addrL);
  Serial1.write(ch);
  Serial1.print(msg);
  Serial1.print("\n");
  Serial1.flush();
  delay(30);
}

String buildAlarmControlClearMessage(const String& uid, const String& type, uint32_t reqId) {
  uint16_t addr = 0;
  alarmPanelKeyToAddr(uid, addr);
  const String body = String("C,") + String(addr)
                    + "," + String(emergencyTypeCode(normalizeEmergencyType(type)))
                    + "," + String(reqId);
  return buildProtocolFrame(body);
}

void sendAlarmControlToCentral(const String& payload, uint8_t repeats = 3) {
  for (uint8_t i = 0; i < repeats; ++i) {
    sendToNode(ALARM_MODULE_ADDR, MASTER_CH, payload);
    MASTER_DEBUG_LOGF("[ALARM][TX->CENTRAL #%u] %s\n", static_cast<unsigned>(i + 1), payload.c_str());
    if ((i + 1) < repeats) delay(90);
  }
}

bool requestClearForEmergency(const EmergencyState& state, uint32_t* outReqId = nullptr) {
  const String stateUid = normalizeAlarmPanelUid(state.sourceId);
  if (!isValidAlarmPanelUid(stateUid)) {
    return false;
  }

  const String type = normalizeEmergencyType(state.type);
  if (!isKnownEmergencyType(type)) {
    return false;
  }

  const uint32_t reqId = g_nextEmergencyControlReqId++;
  const String payload = buildAlarmControlClearMessage(stateUid, type, reqId);
  sendAlarmControlToCentral(payload);

  if (outReqId) {
    *outReqId = reqId;
  }
  return true;
}

NodeData& getOrCreateNode(const String& mac, const String& fallbackName, uint16_t nodeId) {
  ensureSensorDirectory(mac);
  rememberKnownNode(mac);

  NodeData& n = nodes[mac];
  n.mac = mac;
  if (n.name.isEmpty()) {
    loadNodeConfig(mac, n, fallbackName);
  }
  if (nodeId != 0) {
    n.nodeId = nodeId;
  }
  return n;
}

void replyConfigToNode(NodeData& n) {
  if (n.nodeId == 0) return;
  String msg = String("CFG,SLEEP=") + String(n.sleepSec);
  sendToNode(n.nodeId, MASTER_CH, msg);
}

static String radioLine;
static bool radioOverflow = false;

void processCfgReq(const String& line) {
  uint16_t nodeId = 0;
  fieldToUInt16(line, "ID", nodeId);
  String mac = normalizeMac(getFieldValue(line, "MAC"));
  if (mac.isEmpty()) return;

  String fallbackName = resolvedNodeName(mac, getFieldValue(line, "NAME"));
  NodeData& n = getOrCreateNode(mac, fallbackName, nodeId);
  n.lastSeen = millis();
  n.remoteIp = "ID " + String(nodeId);
  replyConfigToNode(n);
  pushSnapshot();
}

void processDataLine(const String& line) {
  uint16_t nodeId = 0;
  fieldToUInt16(line, "ID", nodeId);
  String mac = normalizeMac(getFieldValue(line, "MAC"));
  if (mac.isEmpty()) return;

  String fallbackName = resolvedNodeName(mac, getFieldValue(line, "NAME"));
  NodeData& n = getOrCreateNode(mac, fallbackName, nodeId);
  n.lastSeen = millis();
  n.remoteIp = "ID " + String(nodeId);

  float ftmp = NAN, fbat = NAN;

  if (fieldToFloat(line, "TEMP", ftmp)) {
    n.rawTemperature = ftmp;
    captureColdReference(n, ftmp);
    n.temperature = applyNodeOffset(n, ftmp);
  }
  if (fieldToFloat(line, "BAT", fbat)) n.battery = fbat;

  if (isfinite(ftmp)) {
    saveTemperatureSample(mac, n.temperature, 0, 0);  // Los últimos dos parámetros no se usan
    checkAndSaveDailyStats(n);
  }

  updateCriticalAlarmOutput();
  pushSnapshot();
}

int findNextRadioTokenPos(const String& line, int fromIndex) {
  static const char* TOKENS[] = {
    "CFG_REQ,",
    "DATA,",
    "!"
  };

  int best = -1;
  for (const char* token : TOKENS) {
    const int pos = line.indexOf(token, fromIndex);
    if (pos >= 0 && (best < 0 || pos < best)) {
      best = pos;
    }
  }
  return best;
}

bool startsWithTokenAt(const String& line, int index, const char* token) {
  if (index < 0) return false;
  const size_t tokenLen = strlen(token);
  if ((index + static_cast<int>(tokenLen)) > line.length()) return false;
  for (size_t i = 0; i < tokenLen; ++i) {
    if (line[index + static_cast<int>(i)] != token[i]) return false;
  }
  return true;
}

void processSingleRadioLine(String line) {
  line.trim();
  if (line.isEmpty()) return;

  if (line.startsWith("CFG_REQ,")) {
    processCfgReq(line);
    return;
  }
  if (line.startsWith("DATA,")) {
    processDataLine(line);
    return;
  }
  if (line.startsWith("!")) {
    String fields[8];
    int fieldCount = 0;
    char cmd = '\0';
    if (!parseProtocolFrame(line, cmd, fields, fieldCount)) return;

    if (cmd == 'M') {
      processAlarmModuleLine(line);
      return;
    }
    if (cmd == 'P') {
      processAlarmPanelLine(line);
      return;
    }
    if (cmd == 'U') {
      processAlarmControlAckLine(line);
      return;
    }
  }
}

void processRadioLine(String line) {
  line.trim();
  if (line.isEmpty()) return;

  int search = 0;
  bool handledAny = false;

  while (search < line.length()) {
    const int tokenPos = findNextRadioTokenPos(line, search);
    if (tokenPos < 0) break;

    if (startsWithTokenAt(line, tokenPos, "CFG_REQ,") || startsWithTokenAt(line, tokenPos, "DATA,")) {
      const int nextTokenPos = findNextRadioTokenPos(line, tokenPos + 1);
      String segment = nextTokenPos >= 0
        ? line.substring(tokenPos, nextTokenPos)
        : line.substring(tokenPos);
      segment.trim();
      if (!segment.isEmpty()) {
        processSingleRadioLine(segment);
        handledAny = true;
      }
      search = nextTokenPos >= 0 ? nextTokenPos : line.length();
      continue;
    }

    if (line.charAt(tokenPos) == '!') {
      const int star = line.indexOf('*', tokenPos + 1);
      if (star < 0) {
        search = tokenPos + 1;
        continue;
      }

      const int frameEnd = star + 5;
      if (frameEnd > line.length()) {
        search = tokenPos + 1;
        continue;
      }

      String segment = line.substring(tokenPos, frameEnd);
      segment.trim();
      if (!segment.isEmpty()) {
        processSingleRadioLine(segment);
        handledAny = true;
      }
      search = frameEnd;
      continue;
    }

    search = tokenPos + 1;
  }

  if (!handledAny) {
    processSingleRadioLine(line);
  }
}

void handleRadioRx() {
  while (Serial1.available() > 0) {
    char c = (char)Serial1.read();

    if (c == '\r') continue;

    if (c == '\n') {
      if (!radioOverflow) {
        if (!radioLine.isEmpty()) {
          MASTER_DEBUG_LOGF("[RADIO][RX RAW] %s\n", debugRadioText(radioLine).c_str());
        }
        processRadioLine(radioLine);
      } else {
        Serial.println("[RADIO] Línea descartada por overflow");
      }
      radioLine = "";
      radioOverflow = false;
      continue;
    }

    const uint8_t uc = static_cast<uint8_t>(c);
    if (uc < 32 || uc > 126) {
      continue;
    }

    if (radioOverflow) continue;

    if (radioLine.length() < 220) {
      radioLine += c;
    } else {
      radioOverflow = true;
    }
  }
}

// =====================================================
// ENDPOINTS WEB
// =====================================================
void handleSetTimeBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  String* body = reinterpret_cast<String*>(request->_tempObject);

  if (index == 0) {
    body = new String();
    body->reserve(total + 8);
    request->_tempObject = body;
  }

  if (!body) {
    request->send(500, "application/json", "{\"ok\":false}");
    return;
  }

  for (size_t i = 0; i < len; i++) (*body) += (char)data[i];
  if ((index + len) < total) return;

  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, *body);
  delete body;
  request->_tempObject = nullptr;

  if (err) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"json_invalido\"}");
    return;
  }

  uint32_t epoch = doc["epoch"] | 0;
  if (epoch == 0) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"epoch_requerido\"}");
    return;
  }

  g_timeBaseEpoch = epoch;
  g_timeBaseMillis = millis();
  request->send(200, "application/json", "{\"ok\":true}");
  Serial.printf("[TIME] Sincronizada. epoch=%lu\n", (unsigned long)g_timeBaseEpoch);
}

void handleSaveConfigBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  String* body = reinterpret_cast<String*>(request->_tempObject);

  if (index == 0) {
    body = new String();
    body->reserve(total + 8);
    request->_tempObject = body;
  }

  if (!body) {
    request->send(500, "application/json", "{\"ok\":false}");
    return;
  }

  for (size_t i = 0; i < len; i++) (*body) += (char)data[i];
  if ((index + len) < total) return;

  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, *body);
  delete body;
  request->_tempObject = nullptr;

  if (err) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"json_invalido\"}");
    return;
  }

  String mac = normalizeMac(doc["mac"] | "");
  String newName = doc["name"] | "";
  String newMachine = doc["machine"] | "";
  float tempOffset = doc["tempOffset"] | DEFAULT_TEMP_OFFSET;
  uint32_t sleepSec = doc["sleepSec"] | DEFAULT_SLEEP_TIME;
  float thresholdLow = doc["thresholdLow"] | DEFAULT_THRESHOLD_LOW;
  float thresholdHigh = doc["thresholdHigh"] | DEFAULT_THRESHOLD_HIGH;

  if (mac.isEmpty()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"mac_requerida\"}");
    return;
  }

  if (newName.isEmpty()) newName = DEFAULT_SENSOR_NAME;
  if (sleepSec < 5) sleepSec = 5;
  if (sleepSec > 7200) sleepSec = 7200;
  if (thresholdLow >= thresholdHigh) {
    thresholdLow = DEFAULT_THRESHOLD_LOW;
    thresholdHigh = DEFAULT_THRESHOLD_HIGH;
  }

  NodeData& n = nodes[mac];
  uint32_t previousSleep = n.sleepSec;
  n.mac = mac;
  n.name = newName;
  n.machine = newMachine;
  updateCompensationCalibration(n, tempOffset);
  n.tempOffset = tempOffset;
  n.sleepSec = sleepSec;
  n.thresholdLow = thresholdLow;
  n.thresholdHigh = thresholdHigh;
  if (isfinite(n.rawTemperature)) {
    n.temperature = applyNodeOffset(n, n.rawTemperature);
  }

  saveNodeConfig(mac, newName, newMachine, tempOffset, n.offsetBaseRaw, n.offsetCalibrationRaw, sleepSec, thresholdLow, thresholdHigh);

  if (n.nodeId != 0 && sleepSec != previousSleep) replyConfigToNode(n);

  request->send(200, "application/json", "{\"ok\":true}");
  pushSnapshot();
}

void handleSaveAlarmPanelConfigBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  String* body = reinterpret_cast<String*>(request->_tempObject);

  if (index == 0) {
    body = new String();
    body->reserve(total + 8);
    request->_tempObject = body;
  }

  if (!body) {
    request->send(500, "application/json", "{\"ok\":false}");
    return;
  }

  for (size_t i = 0; i < len; i++) (*body) += (char)data[i];
  if ((index + len) < total) return;

  DynamicJsonDocument doc(384);
  DeserializationError err = deserializeJson(doc, *body);
  delete body;
  request->_tempObject = nullptr;

  if (err) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"json_invalido\"}");
    return;
  }

  String uid = normalizeAlarmPanelUid(String(doc["uid"] | ""));
  String name = String(doc["name"] | "");
  String zone = String(doc["zone"] | "");
  String location = String(doc["location"] | "");
  name.trim();
  zone.trim();
  location.trim();

  if (!isValidAlarmPanelUid(uid)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"uid_requerido\"}");
    return;
  }

  AlarmPanelInfo& panel = ensureAlarmPanel(uid);
  panel.name = name;
  panel.zone = zone;
  panel.location = location;
  rememberKnownAlarmPanel(uid);
  saveAlarmPanelConfig(uid, name, zone, location);

  request->send(200, "application/json", "{\"ok\":true}");
  pushSnapshot();
}

void handleNodeCalibrationBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  String* body = reinterpret_cast<String*>(request->_tempObject);

  if (index == 0) {
    body = new String();
    body->reserve(total + 8);
    request->_tempObject = body;
  }

  if (!body) {
    request->send(500, "application/json", "{\"ok\":false}");
    return;
  }

  for (size_t i = 0; i < len; i++) (*body) += (char)data[i];
  if ((index + len) < total) return;

  DynamicJsonDocument doc(384);
  DeserializationError err = deserializeJson(doc, *body);
  delete body;
  request->_tempObject = nullptr;

  if (err) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"json_invalido\"}");
    return;
  }

  String mac = normalizeMac(doc["mac"] | "");
  String action = doc["action"] | "";
  action.trim();

  if (mac.isEmpty()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"mac_requerida\"}");
    return;
  }

  if (action.isEmpty()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"accion_requerida\"}");
    return;
  }

  auto it = nodes.find(mac);
  if (it == nodes.end()) {
    request->send(404, "application/json", "{\"ok\":false,\"error\":\"nodo_no_encontrado\"}");
    return;
  }

  NodeData& n = it->second;
  bool ok = false;

  if (action == "captureCold") {
    ok = captureColdReferenceNow(n);
    if (!ok) {
      request->send(409, "application/json", "{\"ok\":false,\"error\":\"sin_lectura_cruda\"}");
      return;
    }
  } else if (action == "reset") {
    resetNodeCompensationCalibration(n);
    ok = true;
  } else {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"accion_invalida\"}");
    return;
  }

  saveNodeConfig(mac, n.name, n.machine, n.tempOffset, n.offsetBaseRaw, n.offsetCalibrationRaw, n.sleepSec, n.thresholdLow, n.thresholdHigh);
  request->send(200, "application/json", "{\"ok\":true}");
  pushSnapshot();
}

void handleDeleteNodeBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  String* body = reinterpret_cast<String*>(request->_tempObject);

  if (index == 0) {
    body = new String();
    body->reserve(total + 8);
    request->_tempObject = body;
  }

  if (!body) {
    request->send(500, "application/json", "{\"ok\":false}");
    return;
  }

  for (size_t i = 0; i < len; i++) (*body) += (char)data[i];
  if ((index + len) < total) return;

  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, *body);
  delete body;
  request->_tempObject = nullptr;

  if (err) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"json_invalido\"}");
    return;
  }

  String mac = normalizeMac(doc["mac"] | "");
  if (mac.isEmpty()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"mac_requerida\"}");
    return;
  }

  bool historyOk = deleteKnownNode(mac);

  if (!historyOk) {
    request->send(500, "application/json", "{\"ok\":false,\"error\":\"no_se_pudo_eliminar_historial\"}");
    pushSnapshot();
    return;
  }

  Serial.printf("[DELETE] Sensor eliminado: %s\n", mac.c_str());
  request->send(200, "application/json", "{\"ok\":true}");
  pushSnapshot();
}

void handleDeleteAlarmPanelBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  String* body = reinterpret_cast<String*>(request->_tempObject);

  if (index == 0) {
    body = new String();
    body->reserve(total + 8);
    request->_tempObject = body;
  }

  if (!body) {
    request->send(500, "application/json", "{\"ok\":false}");
    return;
  }

  for (size_t i = 0; i < len; i++) (*body) += static_cast<char>(data[i]);
  if ((index + len) < total) return;

  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, *body);
  delete body;
  request->_tempObject = nullptr;

  if (err) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"json_invalido\"}");
    return;
  }

  String uid = normalizeAlarmPanelUid(String(doc["uid"] | ""));
  if (!isValidAlarmPanelUid(uid)) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"uid_requerido\"}");
    return;
  }

  if (!deleteKnownAlarmPanel(uid)) {
    request->send(500, "application/json", "{\"ok\":false,\"error\":\"no_se_pudo_eliminar_panel\"}");
    pushSnapshot();
    return;
  }

  Serial.printf("[ALARM][PANELS] Panel eliminado desde web: %s\n", uid.c_str());
  request->send(200, "application/json", "{\"ok\":true}");
  pushSnapshot();
}

void handleClearEmergencyBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  String* body = reinterpret_cast<String*>(request->_tempObject);

  if (index == 0) {
    body = new String();
    body->reserve(total + 8);
    request->_tempObject = body;
  }

  if (!body) {
    request->send(500, "application/json", "{\"ok\":false}");
    return;
  }

  for (size_t i = 0; i < len; i++) (*body) += (char)data[i];
  if ((index + len) < total) return;

  DynamicJsonDocument doc(384);
  DeserializationError err = deserializeJson(doc, *body);
  delete body;
  request->_tempObject = nullptr;

  if (err) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"json_invalido\"}");
    return;
  }

  const uint32_t requestedId = doc["id"] | 0;
  String rawUid = normalizeAlarmPanelUid(String(doc["uid"] | ""));
  String rawSource = String(doc["sourceId"] | "");
  String rawZone = String(doc["zone"] | "");
  rawSource.trim();
  rawZone.trim();
  const String requestedSource = rawSource.isEmpty() ? "" : emergencyDisplaySource(rawSource);
  const String requestedZone = rawZone.isEmpty() ? "" : emergencyDisplayZone(rawZone);
  const bool clearAll = requestedId == 0 && rawUid.isEmpty() && rawSource.isEmpty() && rawZone.isEmpty();

  uint16_t requestedCount = 0;

  for (const auto& kv : emergencyStates) {
    const EmergencyState& state = kv.second;
    const String stateUid = normalizeAlarmPanelUid(state.sourceId);
    const bool idMatches = requestedId != 0 && state.eventId == requestedId;
    const bool uidMatches = !rawUid.isEmpty() && stateUid == rawUid;
    const bool sourceMatches = !requestedSource.isEmpty() && state.sourceId == requestedSource;
    const bool zoneMatches = !requestedZone.isEmpty() && state.zone == requestedZone;
    const bool sourceZoneMatches = sourceMatches && (requestedZone.isEmpty() || zoneMatches);

    if (clearAll || idMatches || uidMatches || sourceZoneMatches) {
      if (requestClearForEmergency(state)) {
        requestedCount++;
      }
    }
  }

  if (requestedCount == 0) {
    request->send(404, "application/json", "{\"ok\":false,\"error\":\"emergencia_no_encontrada\"}");
    return;
  }

  Serial.printf("[EMERGENCY] Solicitudes de desactivacion enviadas: %u\n", requestedCount);
  request->send(200, "application/json", String("{\"ok\":true,\"requested\":") + String(requestedCount) + "}");
}

void handleSetAlarmSilenceBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  String* body = reinterpret_cast<String*>(request->_tempObject);

  if (index == 0) {
    body = new String();
    body->reserve(total + 8);
    request->_tempObject = body;
  }

  if (!body) {
    request->send(500, "application/json", "{\"ok\":false}");
    return;
  }

  for (size_t i = 0; i < len; i++) (*body) += (char)data[i];
  if ((index + len) < total) return;

  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, *body);
  delete body;
  request->_tempObject = nullptr;

  if (err) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"json_invalido\"}");
    return;
  }

  String mac = normalizeMac(doc["mac"] | "");
  bool silenced = doc["silenced"] | false;

  if (mac.isEmpty()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"mac_requerida\"}");
    return;
  }

  auto it = nodes.find(mac);
  if (it == nodes.end()) {
    request->send(404, "application/json", "{\"ok\":false,\"error\":\"sensor_no_encontrado\"}");
    return;
  }

  NodeData& n = it->second;
  n.alarmSilenced = silenced;
  saveNodeAlarmSilence(mac, n.alarmSilenced);
  updateCriticalAlarmOutput();

  String response = String("{\"ok\":true,\"alarmSilenced\":") + (n.alarmSilenced ? "true" : "false") + "}";
  request->send(200, "application/json", response);
  pushSnapshot();
}

bool shouldSuppressHttp404Log(const String& url) {
  if (url == "/favicon.ico" ||
      url == "/time/1/current" ||
      url == "/service/update2/json" ||
      url == "/chrome-variations/seed" ||
      url == "/uma/v2") {
    return true;
  }

  if (url.startsWith("/msdownload/") ||
      url.endsWith(".crl") ||
      url.startsWith("/MFEw")) {
    return true;
  }

  return false;
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== INICIANDO MAESTRO LoRa ===");

  pinMode(PIN_ALARMA_CRITICA, OUTPUT);
  setAlarmIndicators(false);

  // -------------------------------------------------
  // LCD local
  // -------------------------------------------------
  displayUiInit();
  initHistoryStorage();

  // -------------------------------------------------
  // LittleFS
  // -------------------------------------------------
  if (!initLittleFS()) {
    Serial.println("[ERROR] No se pudo inicializar LittleFS");
  } else {
    Serial.println("[LittleFS] OK");
    if (LittleFS.exists("/index.html")) {
      Serial.println("[LittleFS] /index.html encontrado");
    } else {
      Serial.println("[LittleFS] ADVERTENCIA: falta /index.html");
    }
    loadKnownNodesFromStorage();
    loadKnownAlarmPanelsFromStorage();
  }

  // -------------------------------------------------
  // Pines E220
  // -------------------------------------------------
    pinMode(PIN_E220_M0, OUTPUT);
    pinMode(PIN_E220_M1, OUTPUT);
    setModeNormal();
   

  // -------------------------------------------------
  // WiFi AP + mDNS + DNS portal cautivo
  // -------------------------------------------------
  if (!setupAccessPoint()) {
    Serial.println("[AP] ERROR iniciando Access Point");
  }

  setupMDNS();
  setupDNSServer();

  // -------------------------------------------------
  // Radio E220
  // -------------------------------------------------
  bool radioCfgOk = bootConfigureIfEnabled();
  Serial.printf("[E220] config=%s\n", radioCfgOk ? "OK" : "ERROR");

  setModeNormal();
  delay(100);
  Serial1.begin(9600, SERIAL_8N1, PIN_E220_RX, PIN_E220_TX);
  delay(100);
  Serial.println("[E220] UART iniciada");

  // -------------------------------------------------
  // SSE
  // -------------------------------------------------
  events.onConnect([](AsyncEventSourceClient* client) {
    Serial.println("[SSE] Cliente web conectado");
    String payload = buildJsonSnapshot();
    client->send(payload.c_str(), "snapshot", millis());
  });
  server.addHandler(&events);

  // -------------------------------------------------
  // Archivos estáticos desde LittleFS
  // -------------------------------------------------
  server.serveStatic("/", LittleFS, "/");

  // -------------------------------------------------
  // Página principal
  // -------------------------------------------------
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    Serial.println("[HTTP] GET /");
    if (LittleFS.exists("/index.html")) {
      request->send(LittleFS, "/index.html", "text/html; charset=utf-8");
    } else {
      request->send(500, "text/plain; charset=utf-8", "Falta /index.html en LittleFS");
    }
  });

  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest* request) {
    Serial.println("[HTTP] GET /index.html");
    if (LittleFS.exists("/index.html")) {
      request->send(LittleFS, "/index.html", "text/html; charset=utf-8");
    } else {
      request->send(500, "text/plain; charset=utf-8", "Falta /index.html en LittleFS");
    }
  });

  // -------------------------------------------------
  // Portal cautivo
  // -------------------------------------------------
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->redirect("http://192.168.4.1/");
  });

  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->redirect("http://192.168.4.1/");
  });

  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->redirect("http://192.168.4.1/");
  });

  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->redirect("http://192.168.4.1/");
  });

  server.on("/redirect", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->redirect("http://192.168.4.1/");
  });

  // -------------------------------------------------
  // API
  // -------------------------------------------------
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "application/json; charset=utf-8", buildJsonSnapshot());
  });

  server.on("/api/emergency/dates", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "application/json; charset=utf-8", getEmergencyDatesJson());
  });

  server.on("/api/emergency/history", HTTP_GET, [](AsyncWebServerRequest* request) {
    String date = request->hasParam("date") ? request->getParam("date")->value() : formatDateFromEpoch(masterNowEpoch());
    date.trim();
    if (date.length() != 10) {
      request->send(400, "application/json", "{\"error\":\"fecha_requerida\"}");
      return;
    }
    request->send(200, "application/json; charset=utf-8", getEmergencyHistoryJson(date));
  });

  server.on(
    "/api/config",
    HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    handleSaveConfigBody);

  server.on(
    "/api/alarm-panel/config",
    HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    handleSaveAlarmPanelConfigBody);

  server.on(
    "/api/node/calibration",
    HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    handleNodeCalibrationBody);

  server.on(
    "/api/node/delete",
    HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    handleDeleteNodeBody);

  server.on(
    "/api/alarm-panel/delete",
    HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    handleDeleteAlarmPanelBody);

  server.on(
    "/api/emergency/clear",
    HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    handleClearEmergencyBody);

  server.on(
    "/api/alarm/silence",
    HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    handleSetAlarmSilenceBody);

  server.on(
    "/api/time",
    HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    handleSetTimeBody);

  server.on("/api/dates", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("mac")) {
      request->send(400, "application/json", "{\"error\":\"mac_requerida\"}");
      return;
    }

    String mac = normalizeMac(request->getParam("mac")->value());
    String json = getAvailableDatesNew(mac);
    request->send(200, "application/json; charset=utf-8", json);
  });

  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("mac")) {
      request->send(400, "application/json", "{\"error\":\"mac_requerida\"}");
      return;
    }

    if (!request->hasParam("date")) {
      request->send(400, "application/json", "{\"error\":\"fecha_requerida\"}");
      return;
    }

    String mac = normalizeMac(request->getParam("mac")->value());
    String date = request->getParam("date")->value();
    String json = loadDailyData(mac, date);
    request->send(200, "application/json; charset=utf-8", json);
  });

  server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("mac")) {
      request->send(400, "application/json", "{\"error\":\"mac_requerida\"}");
      return;
    }

    if (!request->hasParam("date")) {
      request->send(400, "application/json", "{\"error\":\"fecha_requerida\"}");
      return;
    }

    String mac = normalizeMac(request->getParam("mac")->value());
    String date = request->getParam("date")->value();
    String json = getDailyStats(mac, date);
    request->send(200, "application/json; charset=utf-8", json);
  });

  // -------------------------------------------------
  // 404
  // -------------------------------------------------
  server.onNotFound([](AsyncWebServerRequest* request) {
    const String url = request->url();
    if (!shouldSuppressHttp404Log(url)) {
      Serial.printf("[HTTP] 404: %s\n", url.c_str());
    }
    request->send(404, "text/plain; charset=utf-8", "404 - Not Found");
  });

  // -------------------------------------------------
  // Iniciar servidor
  // -------------------------------------------------
  server.begin();
  Serial.println("[HTTP] Servidor iniciado");
  printAccessInfo();
  displayUiStart();
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  // -------------------------------------------------
  // DNS portal cautivo
  // -------------------------------------------------
  dnsServer.processNextRequest();

  // -------------------------------------------------
  // Mantener AP activo
  // -------------------------------------------------
  ensureWiFiConnected();

  // -------------------------------------------------
  // Procesar radio LoRa/E220
  // -------------------------------------------------
  handleRadioRx();

  // -------------------------------------------------
  // Enviar snapshot por SSE cada 1 s
  // -------------------------------------------------
  const unsigned long now = millis();
  updateCriticalAlarmOutput();

  if (now - lastBroadcastMs >= 1000UL) {
    lastBroadcastMs = now;
    pushSnapshot();
  }

  displayUiUpdate(now);

  // -------------------------------------------------
  // Estado por serial cada 60 s
  // -------------------------------------------------
  if (now - lastStatusPrintMs >= 60000UL) {
    lastStatusPrintMs = now;

    int onlineCount = 0;
    for (const auto& kv : nodes) {
      if (isNodeOnline(kv.second)) {
        onlineCount++;
      }
    }

    Serial.printf(
      "[STATUS] nodes=%u online=%d heap=%u littlefs=%u/%u hist=%s:%s/%s ap_ip=%s clients=%d\n",
      (unsigned)nodes.size(),
      onlineCount,
      (unsigned)ESP.getFreeHeap(),
      (unsigned)LittleFS.usedBytes(),
      (unsigned)LittleFS.totalBytes(),
      historyStorageLabel(),
      formatBytesCompact(historyUsedBytes()).c_str(),
      formatBytesCompact(historyTotalBytes()).c_str(),
      WiFi.softAPIP().toString().c_str(),
      WiFi.softAPgetStationNum());
  }

  // -------------------------------------------------
  // Pequeña pausa cooperativa
  // -------------------------------------------------
  delay(2);
}
