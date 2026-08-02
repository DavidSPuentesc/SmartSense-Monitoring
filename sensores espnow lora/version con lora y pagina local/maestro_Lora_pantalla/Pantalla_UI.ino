// =====================================================
// UI LCD / PANTALLA
// =====================================================
static unsigned long lastDisplayRefreshMs = 0;
static constexpr unsigned long DISPLAY_REFRESH_MS = 250UL;
static constexpr unsigned long DISPLAY_PAGE_MS = 5000UL;
static constexpr unsigned long DISPLAY_CRITICAL_BLINK_MS = 500UL;
static constexpr unsigned long DISPLAY_IDLE_SLEEP_MS = 3UL * 60UL * 1000UL;
static constexpr unsigned long DISPLAY_EMERGENCY_CLEAR_HOLD_MS = 5000UL;
static constexpr uint8_t DISPLAY_ACTIVE_BRIGHTNESS = 80;
static constexpr bool DISPLAY_COLOR_TEST_MODE = false;
static constexpr unsigned long COLOR_TEST_LED_STEP_MS = 2000UL;
static constexpr uint8_t DISPLAY_CARD_SLOTS = 4;
static constexpr uint16_t DISPLAY_CARD_X = 10;
static constexpr uint16_t DISPLAY_CARD_Y = 100;
static constexpr uint16_t DISPLAY_CARD_WIDTH = 152;
static constexpr uint16_t DISPLAY_CARD_HEIGHT = 48;
static constexpr uint16_t DISPLAY_CARD_STEP_Y = 54;
static constexpr uint16_t DISPLAY_EMERGENCY_CLEAR_BTN_X = 8;
static constexpr uint16_t DISPLAY_EMERGENCY_CLEAR_BTN_Y = 284;
static constexpr uint16_t DISPLAY_EMERGENCY_CLEAR_BTN_W = 156;
static constexpr uint16_t DISPLAY_EMERGENCY_CLEAR_BTN_H = 28;

enum DisplayScreenMode : uint8_t {
  DISPLAY_SCREEN_OVERVIEW = 0,
  DISPLAY_SCREEN_DETAIL = 1
};

struct DisplayCardCache {
  bool occupied = false;
  String signature;
};

struct DisplayHistoryPoint {
  uint32_t secOfDay = 0;
  float temp = NAN;
};

struct DisplayHistoryStats {
  bool valid = false;
  float minTemp = NAN;
  float maxTemp = NAN;
  float avgTemp = NAN;
  float lastTemp = NAN;
  uint16_t samples = 0;
};

static bool g_displayLayoutReady = false;
static String g_displaySummaryKey;
static String g_displayTimeKey;
static bool g_displayShowingEmpty = false;
static size_t g_displayCurrentPage = SIZE_MAX;
static DisplayCardCache g_displayCardCache[DISPLAY_CARD_SLOTS];
static bool g_colorTestScreenReady = false;
static bool g_displayAwake = true;
static bool g_displayWakeRequested = false;
static unsigned long g_displayLastTouchActivityMs = 0;
static unsigned long g_colorTestLedLastStepMs = 0;
static uint8_t g_colorTestLedIndex = 0;
static bool g_displayTouchDown = false;
static DisplayScreenMode g_displayScreenMode = DISPLAY_SCREEN_OVERVIEW;
static String g_displaySelectedMac;
static String g_displayDetailSignature;
static String g_displayDetailHistoryKey;
static String g_displayDetailDate;
static std::vector<DisplayHistoryPoint> g_displayDetailPoints;
static DisplayHistoryStats g_displayDetailStats;
static String g_displayEmergencySignature;
static String g_displayEmergencyButtonSignature;
static uint32_t g_displayLastWakeEmergencyId = 0;
static String g_displayWakeReason = "toque";
static String g_displayEmergencyClearHoldKey;
static unsigned long g_displayEmergencyClearHoldStartMs = 0;
static bool g_displayEmergencyClearTriggered = false;

struct ColorTestEntry {
  uint8_t index;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static constexpr ColorTestEntry COLOR_TEST_ENTRIES[] = {
  {1, 255, 0, 0},
  {2, 0, 255, 0},
  {3, 0, 0, 255},
  {4, 255, 255, 255},
  {5, 255, 255, 0},
  {6, 0, 255, 255},
  {7, 255, 0, 255},
  {8, 255, 128, 0},
};

void renderDisplayStatus();
void handleDisplayTouchTap(uint16_t touchX, uint16_t touchY);
void openDisplayDetailForNode(const String& mac);
void closeDisplayDetail();

void writeMappedStatusRgb(uint8_t red, uint8_t green, uint8_t blue) {
  // Observed LED mapping on hardware:
  // requested R -> shown G
  // requested G -> shown R
  // requested B -> shown B
  //rgbLedWrite(PIN_STATUS_RGB_LED, green, red, blue);
}

void drawDisplayTextCentered(uint16_t centerX, uint16_t y, const String& text, uint16_t color, uint8_t scale = 1) {
  const uint16_t width = LCD_TextWidth(text, scale);
  const uint16_t x = (centerX > (width / 2U)) ? static_cast<uint16_t>(centerX - (width / 2U)) : 0;
  LCD_DrawText(x, y, text, color, scale);
}

void requestDisplayWake(const String& reason) {
  g_displayWakeReason = reason;
  if (!g_displayAwake) {
    g_displayWakeRequested = true;
  }
}

String displayEmergencyHoldKey(const EmergencyState& state) {
  return String(state.eventId) + "|" + state.sourceId + "|" + state.type;
}

void resetDisplayEmergencyClearHold() {
  g_displayEmergencyClearHoldKey = "";
  g_displayEmergencyClearHoldStartMs = 0;
  g_displayEmergencyClearTriggered = false;
  g_displayEmergencyButtonSignature = "";
}

void setAlarmIndicators(bool active) {
  if (DISPLAY_COLOR_TEST_MODE) {
    digitalWrite(PIN_ALARMA_CRITICA, LOW);
    return;
  }
  digitalWrite(PIN_ALARMA_CRITICA, active ? HIGH : LOW);
  writeMappedStatusRgb(active ? 64 : 0, 0, 0);
}

void drawDisplayTextRight(uint16_t right, uint16_t y, const String& text, uint16_t color, uint8_t scale = 1) {
  const uint16_t width = LCD_TextWidth(text, scale);
  const uint16_t x = (right > width) ? (right - width) : 0;
  LCD_DrawText(x, y, text, color, scale);
}

void drawDisplayTextDelta(uint16_t x, uint16_t y, const String& previous, const String& current,
                          uint16_t color, uint16_t background, uint8_t scale = 1) {
  if (scale == 0) {
    scale = 1;
  }

  const uint16_t step = 6U * scale;
  const uint16_t height = LCD_TextHeight(scale);
  const size_t maxLen = std::max(previous.length(), current.length());

  for (size_t i = 0; i < maxLen; ++i) {
    const char prevChar = (i < previous.length()) ? previous[i] : '\0';
    const char currChar = (i < current.length()) ? current[i] : '\0';
    if (prevChar == currChar) {
      continue;
    }

    const uint16_t cellX = static_cast<uint16_t>(x + i * step);
    LCD_FillRect(cellX, y, step, height, background);
    if (currChar != '\0' && currChar != ' ') {
      LCD_DrawChar(cellX, y, currChar, color, scale);
    }
  }
}

void drawDisplayDot(uint16_t centerX, uint16_t centerY, uint8_t radius, uint16_t color) {
  const int16_t r = radius;
  for (int16_t dy = -r; dy <= r; ++dy) {
    for (int16_t dx = -r; dx <= r; ++dx) {
      if ((dx * dx) + (dy * dy) <= (r * r)) {
        LCD_DrawPixel(static_cast<uint16_t>(centerX + dx), static_cast<uint16_t>(centerY + dy), color);
      }
    }
  }
}

void drawDisplayRoundedFill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t radius, uint16_t color) {
  if (width == 0 || height == 0) {
    return;
  }

  if (radius < 2 || width <= (radius * 2U) || height <= (radius * 2U)) {
    LCD_FillRect(x, y, width, height, color);
    return;
  }

  static const uint8_t insetPattern[] = {3, 2, 1, 1, 0, 0, 0, 0};
  const uint8_t maxPatternIndex = sizeof(insetPattern) / sizeof(insetPattern[0]) - 1;

  LCD_FillRect(x, static_cast<uint16_t>(y + radius), width, static_cast<uint16_t>(height - 2U * radius), color);

  for (uint8_t row = 0; row < radius; ++row) {
    const uint8_t inset = insetPattern[(row <= maxPatternIndex) ? row : maxPatternIndex];
    const uint16_t rowWidth = (width > (2U * inset)) ? static_cast<uint16_t>(width - 2U * inset) : width;
    LCD_FillRect(static_cast<uint16_t>(x + inset), static_cast<uint16_t>(y + row), rowWidth, 1, color);
    LCD_FillRect(static_cast<uint16_t>(x + inset), static_cast<uint16_t>(y + height - 1U - row), rowWidth, 1, color);
  }
}

bool displayCardBlinkPhase() {
  return ((millis() / DISPLAY_CRITICAL_BLINK_MS) % 2UL) == 0UL;
}

String displayCardTitle(const NodeData& n) {
  String label = !n.name.isEmpty() ? n.name : n.machine;
  if (label.isEmpty()) {
    label = "Sensor";
  }
  label.replace("_", " ");
  label.trim();
  if (label.length() > 12) {
    label = label.substring(0, 12);
  }
  return label;
}

String displayCardTemp(const NodeData& n) {
  return isfinite(n.temperature) ? String(n.temperature, 1) + "C" : "--.-C";
}

String displayCardMeta(const NodeData& n) {
  if (isfinite(n.battery)) {
    return String((int)roundf(n.battery)) + "%";
  }
  return "";
}

uint16_t displayCardStatusColor(const NodeData& n) {
  return isNodeOnline(n) ? LCD_Color565(0, 255, 0) : LCD_Color565(255, 0, 0);
}

uint16_t displayCardAccentColor(const NodeData& n) {
  if (!isNodeOnline(n)) {
    return 0x0000;
  }
  if (isNodeInCriticalRange(n)) {
    return LCD_Color565(255, 0, 0);
  }
  if (isNodeInWarningRange(n)) {
    return LCD_Color565(255, 176, 0);
  }
  return LCD_Color565(0, 255, 0);
}

uint16_t displayCardTempColor(const NodeData& n) {
  if (!isNodeOnline(n)) {
    return 0x0000;
  }
  if (isNodeInCriticalRange(n)) {
    return LCD_Color565(255, 0, 0);
  }
  if (isNodeInWarningRange(n)) {
    return LCD_Color565(255, 176, 0);
  }
  return LCD_Color565(0, 255, 0);
}

uint16_t displayCardBackgroundColor(const NodeData& n) {
  if (isNodeInCriticalRange(n) && displayCardBlinkPhase()) {
    return LCD_Color565(255, 196, 196);
  }
  return 0xFFFF;
}

uint16_t displayCardBorderColor(const NodeData&) {
  return 0x0000;
}

String displayCardSignature(const NodeData& n) {
  return n.mac + "|" + displayCardTitle(n) + "|" + displayCardTemp(n) + "|" + displayCardMeta(n)
         + "|" + (isNodeOnline(n) ? "1" : "0") + "|" + (isNodeCritical(n) ? "1" : "0")
         + "|" + (isNodeInCriticalRange(n) ? "C" : (isNodeInWarningRange(n) ? "W" : "N"))
         + "|" + (isNodeInCriticalRange(n) ? String(displayCardBlinkPhase() ? "1" : "0") : "S");
}

void resetDisplayCardCache() {
  for (uint8_t i = 0; i < DISPLAY_CARD_SLOTS; ++i) {
    g_displayCardCache[i].occupied = false;
    g_displayCardCache[i].signature = "";
  }
}

void resetDisplayOverviewCache() {
  g_displayLayoutReady = false;
  g_displaySummaryKey = "";
  g_displayTimeKey = "";
  g_displayShowingEmpty = false;
  g_displayCurrentPage = SIZE_MAX;
  g_displayEmergencySignature = "";
  g_displayEmergencyButtonSignature = "";
  resetDisplayEmergencyClearHold();
  resetDisplayCardCache();
}

void resetDisplayDetailCache() {
  g_displayDetailSignature = "";
  g_displayDetailHistoryKey = "";
  g_displayDetailDate = "";
  g_displayDetailPoints.clear();
  g_displayDetailStats = DisplayHistoryStats();
}

void drawDisplayLine(int x0, int y0, int x1, int y1, uint16_t color) {
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    if (x0 >= 0 && x0 < LCD_WIDTH && y0 >= 0 && y0 < LCD_HEIGHT) {
      LCD_DrawPixel(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0), color);
    }

    if (x0 == x1 && y0 == y1) {
      break;
    }

    const int e2 = err * 2;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

std::vector<const NodeData*> buildOrderedDisplayNodes() {
  std::vector<const NodeData*> orderedNodes;
  orderedNodes.reserve(nodes.size());
  for (const auto& kv : nodes) {
    orderedNodes.push_back(&kv.second);
  }

  std::sort(orderedNodes.begin(), orderedNodes.end(), [](const NodeData* a, const NodeData* b) {
    const bool aCritical = isNodeInCriticalRange(*a);
    const bool bCritical = isNodeInCriticalRange(*b);
    if (aCritical != bCritical) return aCritical > bCritical;

    const bool aOnline = isNodeOnline(*a);
    const bool bOnline = isNodeOnline(*b);
    if (aOnline != bOnline) return aOnline > bOnline;

    if (a->nodeId != b->nodeId) return a->nodeId < b->nodeId;
    return a->mac < b->mac;
  });

  return orderedNodes;
}

const NodeData* findDisplaySelectedNode() {
  if (g_displaySelectedMac.isEmpty()) {
    return nullptr;
  }

  auto it = nodes.find(g_displaySelectedMac);
  if (it == nodes.end()) {
    return nullptr;
  }
  return &it->second;
}

String displayDetailDateForNode(const NodeData& n) {
  if (masterTimeIsValid()) {
    const String today = getDateStrFromTimestamp(masterNowEpoch());
    if (!today.isEmpty()) {
      return today;
    }
  }
  return getLatestAvailableDate(n.mac);
}

bool loadDisplayHistoryForDate(const String& mac, const String& dateStr) {
  g_displayDetailPoints.clear();
  g_displayDetailStats = DisplayHistoryStats();
  if (dateStr.isEmpty()) {
    return false;
  }

  const String ns = macToNs(mac);
  const String fileName = "/sensor_" + ns + "/" + dateStr + ".txt";
  FS& fs = historyFS();
  if (!fs.exists(fileName)) {
    return false;
  }

  File file = fs.open(fileName, "r");
  if (!file) {
    return false;
  }

  float sumTemp = 0.0f;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;

    const int comma1 = line.indexOf(',');
    if (comma1 <= 0) continue;

    const uint32_t secOfDay = static_cast<uint32_t>(line.substring(0, comma1).toInt());
    const float temp = line.substring(comma1 + 1).toInt() / 10.0f;

    DisplayHistoryPoint point;
    point.secOfDay = secOfDay;
    point.temp = temp;
    g_displayDetailPoints.push_back(point);

    if (!g_displayDetailStats.valid) {
      g_displayDetailStats.valid = true;
      g_displayDetailStats.minTemp = temp;
      g_displayDetailStats.maxTemp = temp;
      g_displayDetailStats.lastTemp = temp;
    } else {
      if (temp < g_displayDetailStats.minTemp) g_displayDetailStats.minTemp = temp;
      if (temp > g_displayDetailStats.maxTemp) g_displayDetailStats.maxTemp = temp;
      g_displayDetailStats.lastTemp = temp;
    }

    sumTemp += temp;
    g_displayDetailStats.samples++;
  }

  file.close();

  if (!g_displayDetailStats.valid || g_displayDetailStats.samples == 0) {
    g_displayDetailStats = DisplayHistoryStats();
    g_displayDetailPoints.clear();
    return false;
  }

  g_displayDetailStats.avgTemp = sumTemp / static_cast<float>(g_displayDetailStats.samples);
  return true;
}

void ensureDisplayDetailHistory(const NodeData& n) {
  const String dateStr = displayDetailDateForNode(n);
  const String historyKey = n.mac + "|" + dateStr + "|" + String(n.todaySamples) + "|" + String(n.lastSavedTimestamp);
  if (historyKey == g_displayDetailHistoryKey) {
    return;
  }

  g_displayDetailHistoryKey = historyKey;
  g_displayDetailDate = dateStr;
  g_displayDetailPoints.clear();
  g_displayDetailStats = DisplayHistoryStats();

  if (!loadDisplayHistoryForDate(n.mac, dateStr) && isfinite(n.temperature)) {
    DisplayHistoryPoint point;
    point.secOfDay = masterTimeIsValid() ? (epochToLocal(masterNowEpoch()) % 86400UL) : 0UL;
    point.temp = n.temperature;
    g_displayDetailPoints.push_back(point);
    g_displayDetailStats.valid = true;
    g_displayDetailStats.minTemp = isfinite(n.todayMin) ? n.todayMin : n.temperature;
    g_displayDetailStats.maxTemp = isfinite(n.todayMax) ? n.todayMax : n.temperature;
    g_displayDetailStats.avgTemp = (n.todaySamples > 0) ? (n.todaySum / static_cast<float>(n.todaySamples)) : n.temperature;
    g_displayDetailStats.lastTemp = n.temperature;
    g_displayDetailStats.samples = (n.todaySamples > 0) ? static_cast<uint16_t>(n.todaySamples) : 1U;
  }
}

String displayDetailSignature(const NodeData& n) {
  const String title = displayCardTitle(n);
  const String temp = displayCardTemp(n);
  const String meta = displayCardMeta(n);
  return n.mac + "|" + title + "|" + temp + "|" + meta + "|" + String(n.nodeId)
         + "|" + (isNodeOnline(n) ? "1" : "0")
         + "|" + String(n.todaySamples)
         + "|" + String(n.lastSavedTimestamp)
         + "|" + g_displayDetailDate
         + "|" + String(g_displayDetailStats.samples)
         + "|" + String(isfinite(g_displayDetailStats.minTemp) ? g_displayDetailStats.minTemp : -999.0f, 1)
         + "|" + String(isfinite(g_displayDetailStats.maxTemp) ? g_displayDetailStats.maxTemp : -999.0f, 1);
}

void openDisplayDetailForNode(const String& mac) {
  g_displaySelectedMac = mac;
  g_displayScreenMode = DISPLAY_SCREEN_DETAIL;
  resetDisplayDetailCache();
  renderDisplayStatus();
}

void closeDisplayDetail() {
  g_displayScreenMode = DISPLAY_SCREEN_OVERVIEW;
  g_displaySelectedMac = "";
  resetDisplayDetailCache();
  resetDisplayOverviewCache();
  renderDisplayStatus();
}

void noteDisplayTouchActivity(unsigned long now) {
  g_displayLastTouchActivityMs = now;
  g_displayWakeReason = "toque";
  if (!g_displayAwake) {
    g_displayWakeRequested = true;
  }
}

void updateDisplayTouchState(unsigned long now) {
  uint16_t touchX = 0;
  uint16_t touchY = 0;
  if (!LCD_TouchRead(&touchX, &touchY)) {
    g_displayTouchDown = false;
    if (!g_displayEmergencyClearHoldKey.isEmpty() && !g_displayEmergencyClearTriggered) {
      resetDisplayEmergencyClearHold();
    }
    return;
  }

  if (!g_displayAwake) {
    Serial.printf("[LCD] Toque detectado en (%u,%u)\n", touchX, touchY);
  }
  noteDisplayTouchActivity(now);

  const EmergencyState* activeEmergency = findDisplayPriorityEmergency();
  if (g_displayAwake && activeEmergency) {
    serviceDisplayEmergencyClearTouch(*activeEmergency, touchX, touchY, now);
    g_displayTouchDown = true;
    return;
  }

  if (g_displayAwake && !g_displayTouchDown) {
    handleDisplayTouchTap(touchX, touchY);
  }
  g_displayTouchDown = true;
}

void wakeDisplayIfRequested() {
  if (!g_displayWakeRequested) {
    return;
  }

  Set_Backlight(DISPLAY_ACTIVE_BRIGHTNESS);
  g_displayAwake = true;
  g_displayWakeRequested = false;
  g_displayLastTouchActivityMs = millis();
  g_displayLayoutReady = false;
  g_displaySummaryKey = "";
  g_displayTimeKey = "";
  g_displayShowingEmpty = false;
  g_displayCurrentPage = SIZE_MAX;
  g_colorTestScreenReady = false;
  resetDisplayCardCache();
  renderDisplayStatus();
  lastDisplayRefreshMs = millis();
  Serial.println("[LCD] Pantalla encendida");
  Serial.printf("[LCD] Motivo: %s\n", g_displayWakeReason.c_str());
}

void updateDisplaySleepState(unsigned long now) {
  if (DISPLAY_COLOR_TEST_MODE || !g_displayAwake || g_displayWakeRequested) {
    return;
  }

  if (countActiveEmergencies() > 0) {
    return;
  }

  if (now < g_displayLastTouchActivityMs) {
    return;
  }

  if ((now - g_displayLastTouchActivityMs) >= DISPLAY_IDLE_SLEEP_MS) {
    Set_Backlight(0);
    g_displayAwake = false;
    Serial.println("[LCD] Pantalla apagada");
    Serial.println("[LCD] Motivo: inactividad tactil");
  }
}

void drawDisplayBaseLayout() {
  static const uint16_t colorBg = 0xFFFF;
  static const uint16_t colorPanel = 0xFFFF;
  static const uint16_t colorTitle = 0x0000;
  static const uint16_t colorAccent = 0x0000;
  static const uint16_t colorDivider = 0x0000;

  LCD_FillScreen(colorBg);
  LCD_FillRect(10, 42, 150, 44, colorPanel);
  LCD_FillRect(10, 90, 150, 2, colorDivider);
  LCD_DrawText(40, 16, "MAESTRO", colorTitle, 2);
  LCD_DrawText(18, 48, "Web: SENSORES.LOCAL", colorAccent, 1);
  LCD_DrawText(18, 72, "HORA", colorAccent, 1);
  g_displayLayoutReady = true;
  g_displaySummaryKey = "";
  g_displayTimeKey = "";
  g_displayShowingEmpty = false;
  g_displayCurrentPage = SIZE_MAX;
  g_displayEmergencySignature = "";
  g_displayEmergencyButtonSignature = "";
  resetDisplayCardCache();
}

void clearDisplayCardSlot(uint8_t slotIndex) {
  static const uint16_t colorBg = 0xFFFF;
  const uint16_t x = 10;
  const uint16_t y = static_cast<uint16_t>(100 + slotIndex * 54);
  LCD_FillRect(x, y, 152, 48, colorBg);
}

void drawSensorCard(uint8_t slotIndex, const NodeData& n) {
  static const uint16_t colorText = 0x0000;
  static const uint16_t colorMeta = 0x0000;

  const uint16_t x = 10;
  const uint16_t y = static_cast<uint16_t>(100 + slotIndex * 54);
  const uint16_t colorBg = displayCardBackgroundColor(n);
  const uint16_t colorBorder = displayCardBorderColor(n);
  const uint16_t statusColor = displayCardStatusColor(n);
  const String title = displayCardTitle(n);
  const String temp = displayCardTemp(n);
  const String meta = displayCardMeta(n);

  drawDisplayRoundedFill(x, y, 152, 48, 6, colorBorder);
  drawDisplayRoundedFill(x + 1, y + 1, 150, 46, 5, colorBg);
  drawDisplayDot(x + 138, y + 11, 4, statusColor);
  LCD_DrawText(x + 28, y + 8, title, colorText, 1);
  LCD_DrawText(x + 28, y + 23, temp, displayCardTempColor(n), 2);
  if (!meta.isEmpty()) {
    drawDisplayTextRight(x + 144, y + 34, meta, colorMeta, 1);
  }
}

void drawEmptyDisplayState() {
  static const uint16_t colorBg = 0xFFFF;
  static const uint16_t colorBorder = 0x0000;
  static const uint16_t colorText = 0x0000;

  for (uint8_t i = 0; i < DISPLAY_CARD_SLOTS; ++i) {
    clearDisplayCardSlot(i);
  }

  drawDisplayRoundedFill(10, 120, 152, 70, 6, colorBorder);
  drawDisplayRoundedFill(11, 121, 150, 68, 5, colorBg);
  LCD_DrawText(22, 139, "ESPERANDO", colorText, 2);
  LCD_DrawText(32, 163, "SENSORES", colorText, 2);
  resetDisplayCardCache();
}

bool displayPointInRect(uint16_t px, uint16_t py, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
  return px >= x && px < static_cast<uint16_t>(x + width) && py >= y && py < static_cast<uint16_t>(y + height);
}

void drawDisplayDetailScreen(const NodeData& n) {
  static const uint16_t colorBg = 0xFFFF;
  static const uint16_t colorPanel = 0xFFFF;
  static const uint16_t colorBorder = 0x0000;
  static const uint16_t colorText = 0x0000;
  static const uint16_t colorMuted = 0x0000;
  static const uint16_t colorGrid = LCD_Color565(218, 222, 228);

  ensureDisplayDetailHistory(n);

  const String signature = displayDetailSignature(n);
  if (signature == g_displayDetailSignature) {
    return;
  }
  g_displayDetailSignature = signature;

  const uint16_t tempColor = displayCardTempColor(n);
  const uint16_t statusColor = displayCardStatusColor(n);
  const String title = displayCardTitle(n);
  const String temp = displayCardTemp(n);
  const String meta = displayCardMeta(n);
  const String idLabel = (n.nodeId != 0) ? ("ID " + String(n.nodeId)) : "ID --";

  LCD_FillScreen(colorBg);

  drawDisplayRoundedFill(10, 10, 62, 24, 6, colorBorder);
  drawDisplayRoundedFill(11, 11, 60, 22, 5, colorPanel);
  LCD_DrawText(18, 17, "< VOLVER", colorText, 1);

  LCD_DrawText(10, 44, title, colorText, 2);
  LCD_DrawText(10, 68, idLabel, colorMuted, 1);
  if (!meta.isEmpty()) {
    drawDisplayTextRight(160, 68, meta, colorMuted, 1);
  }
  drawDisplayDot(152, 52, 5, statusColor);
  drawDisplayTextRight(160, 20, temp, tempColor, 2);

  drawDisplayRoundedFill(8, 92, 156, 162, 8, colorBorder);
  drawDisplayRoundedFill(9, 93, 154, 160, 7, colorPanel);

  const uint16_t chartX = 38;
  const uint16_t chartY = 104;
  const uint16_t chartW = 118;
  const uint16_t chartH = 124;
  LCD_FillRect(chartX, chartY, chartW, chartH, 0xFFFF);

  for (uint8_t i = 0; i < 5; ++i) {
    const uint16_t gy = static_cast<uint16_t>(chartY + (i * (chartH - 1)) / 4);
    LCD_FillRect(chartX, gy, chartW, 1, colorGrid);
  }
  for (uint8_t i = 0; i < 4; ++i) {
    const uint16_t gx = static_cast<uint16_t>(chartX + (i * (chartW - 1)) / 3);
    LCD_FillRect(gx, chartY, 1, chartH, colorGrid);
  }

  if (g_displayDetailPoints.empty() || !g_displayDetailStats.valid) {
    LCD_DrawText(38, 165, "SIN DATOS", colorText, 2);
  } else {
    float minTemp = g_displayDetailStats.minTemp;
    float maxTemp = g_displayDetailStats.maxTemp;
    if (!isfinite(minTemp) || !isfinite(maxTemp)) {
      minTemp = maxTemp = g_displayDetailPoints.front().temp;
    }
    if (fabsf(maxTemp - minTemp) < 0.2f) {
      minTemp -= 0.5f;
      maxTemp += 0.5f;
    } else {
      minTemp -= 0.4f;
      maxTemp += 0.4f;
    }

    const float midTemp = (minTemp + maxTemp) * 0.5f;
    drawDisplayTextRight(static_cast<uint16_t>(chartX - 4), static_cast<uint16_t>(chartY - 4), String(maxTemp, 1), colorMuted, 1);
    drawDisplayTextRight(static_cast<uint16_t>(chartX - 4), static_cast<uint16_t>(chartY + (chartH / 2U) - 4), String(midTemp, 1), colorMuted, 1);
    drawDisplayTextRight(static_cast<uint16_t>(chartX - 4), static_cast<uint16_t>(chartY + chartH - 8), String(minTemp, 1), colorMuted, 1);
    LCD_DrawText(10, static_cast<uint16_t>(chartY + (chartH / 2U) - 3), "C", colorMuted, 1);

    int prevX = -1;
    int prevY = -1;
    for (const auto& point : g_displayDetailPoints) {
      const float ratioX = static_cast<float>(point.secOfDay) / 86399.0f;
      const float ratioY = (point.temp - minTemp) / (maxTemp - minTemp);
      const int px = chartX + static_cast<int>(ratioX * static_cast<float>(chartW - 1));
      const int py = chartY + chartH - 1 - static_cast<int>(constrain(ratioY, 0.0f, 1.0f) * static_cast<float>(chartH - 1));

      if (prevX >= 0) {
        drawDisplayLine(prevX, prevY, px, py, tempColor);
      } else {
        drawDisplayDot(static_cast<uint16_t>(px), static_cast<uint16_t>(py), 1, tempColor);
      }
      prevX = px;
      prevY = py;
    }

    if (prevX >= 0) {
      drawDisplayDot(static_cast<uint16_t>(prevX), static_cast<uint16_t>(prevY), 2, tempColor);
    }

    LCD_DrawText(chartX, static_cast<uint16_t>(chartY + chartH + 4), "00:00", colorMuted, 1);
    drawDisplayTextRight(static_cast<uint16_t>(chartX + chartW), static_cast<uint16_t>(chartY + chartH + 4), "23:59", colorMuted, 1);
  }

  const float avgTemp = (g_displayDetailStats.valid && isfinite(g_displayDetailStats.avgTemp))
                          ? g_displayDetailStats.avgTemp
                          : ((n.todaySamples > 0) ? (n.todaySum / static_cast<float>(n.todaySamples)) : n.temperature);

  const uint16_t statsY = 264;
  const uint16_t statsW = 48;
  for (uint8_t i = 0; i < 3; ++i) {
    const uint16_t boxX = static_cast<uint16_t>(10 + i * 52);
    drawDisplayRoundedFill(boxX, statsY, statsW, 38, 6, colorBorder);
    drawDisplayRoundedFill(static_cast<uint16_t>(boxX + 1), static_cast<uint16_t>(statsY + 1), static_cast<uint16_t>(statsW - 2), 36, 5, colorPanel);
  }
  LCD_DrawText(20, 271, "MIN", colorMuted, 1);
  LCD_DrawText(74, 271, "PROM", colorMuted, 1);
  LCD_DrawText(130, 271, "MAX", colorMuted, 1);
  LCD_DrawText(18, 286, isfinite(g_displayDetailStats.minTemp) ? String(g_displayDetailStats.minTemp, 1) : "--.-", colorText, 1);
  LCD_DrawText(72, 286, isfinite(avgTemp) ? String(avgTemp, 1) : "--.-", colorText, 1);
  LCD_DrawText(126, 286, isfinite(g_displayDetailStats.maxTemp) ? String(g_displayDetailStats.maxTemp, 1) : "--.-", colorText, 1);
}

void handleDisplayTouchTap(uint16_t touchX, uint16_t touchY) {
  if (g_displayScreenMode == DISPLAY_SCREEN_DETAIL) {
    if (displayPointInRect(touchX, touchY, 10, 10, 62, 24)) {
      closeDisplayDetail();
    }
    return;
  }

  const std::vector<const NodeData*> orderedNodes = buildOrderedDisplayNodes();
  if (orderedNodes.empty()) {
    return;
  }

  const size_t totalPages = (orderedNodes.size() + DISPLAY_CARD_SLOTS - 1) / DISPLAY_CARD_SLOTS;
  const size_t currentPage = (g_displayCurrentPage == SIZE_MAX)
                               ? ((totalPages > 1) ? ((millis() / DISPLAY_PAGE_MS) % totalPages) : 0)
                               : g_displayCurrentPage;
  const size_t startIndex = currentPage * DISPLAY_CARD_SLOTS;

  for (uint8_t slot = 0; slot < DISPLAY_CARD_SLOTS; ++slot) {
    const size_t nodeIndex = startIndex + slot;
    if (nodeIndex >= orderedNodes.size()) {
      continue;
    }

    const uint16_t cardY = static_cast<uint16_t>(DISPLAY_CARD_Y + slot * DISPLAY_CARD_STEP_Y);
    if (displayPointInRect(touchX, touchY, DISPLAY_CARD_X, cardY, DISPLAY_CARD_WIDTH, DISPLAY_CARD_HEIGHT)) {
      openDisplayDetailForNode(orderedNodes[nodeIndex]->mac);
      return;
    }
  }
}

void printColorTestLegend() {
  Serial.println("[COLOR TEST] Reportame asi:");
  Serial.println("[COLOR TEST] pantalla: 1=?, 2=?, 3=?, 4=?, 5=?, 6=?, 7=?, 8=?");
  Serial.println("[COLOR TEST] led: 1=?, 2=?, 3=?, 4=?, 5=?, 6=?, 7=?, 8=?");
  Serial.println("[COLOR TEST] Valores enviados:");
  for (const auto& entry : COLOR_TEST_ENTRIES) {
    Serial.printf("[COLOR TEST] %u -> RGB(%u,%u,%u)\n", entry.index, entry.r, entry.g, entry.b);
  }
}

void renderColorTestScreen() {
  static const uint16_t colorBg = LCD_Color565(10, 12, 16);
  static const uint16_t colorPanel = LCD_Color565(30, 34, 40);
  static const uint16_t colorText = 0xFFFF;
  static const uint16_t colorDim = LCD_Color565(220, 224, 230);

  LCD_FillScreen(colorBg);
  LCD_FillRect(8, 8, 156, 28, colorPanel);
  LCD_DrawText(18, 15, "TEST COLOR", colorText, 2);
  LCD_DrawText(16, 44, "PANTALLA", colorDim, 1);

  constexpr uint16_t swatchW = 68;
  constexpr uint16_t swatchH = 40;
  constexpr uint16_t leftX = 10;
  constexpr uint16_t rightX = 94;
  constexpr uint16_t startY = 60;
  constexpr uint16_t gapY = 48;

  for (uint8_t i = 0; i < 8; ++i) {
    const uint16_t x = (i % 2 == 0) ? leftX : rightX;
    const uint16_t y = static_cast<uint16_t>(startY + (i / 2) * gapY);
    const ColorTestEntry& entry = COLOR_TEST_ENTRIES[i];
    const uint16_t fill = LCD_Color565(entry.r, entry.g, entry.b);
    LCD_FillRect(x, y, swatchW, swatchH, 0xFFFF);
    LCD_FillRect(x + 1, y + 1, swatchW - 2, swatchH - 2, fill);

    const bool darkNumber = (entry.index == 4 || entry.index == 5 || entry.index == 6);
    LCD_DrawText(x + 6, y + 5, String(entry.index), darkNumber ? 0x0000 : 0xFFFF, 2);
  }

  LCD_FillRect(8, 258, 156, 54, colorPanel);
  LCD_DrawText(16, 266, "LED RGB", colorText, 1);
  LCD_DrawText(16, 282, "SECUENCIA 1-8", colorDim, 1);
  g_colorTestScreenReady = true;
}

void updateColorTestLed(bool force = false) {
  if (force || (millis() - g_colorTestLedLastStepMs >= COLOR_TEST_LED_STEP_MS)) {
    g_colorTestLedLastStepMs = millis();
    if (!force) {
      g_colorTestLedIndex = static_cast<uint8_t>((g_colorTestLedIndex + 1) % 8);
    }
  }

  const ColorTestEntry& entry = COLOR_TEST_ENTRIES[g_colorTestLedIndex];
  writeMappedStatusRgb(entry.r, entry.g, entry.b);

  LCD_FillRect(104, 266, 50, 14, LCD_Color565(30, 34, 40));
  LCD_DrawText(104, 266, String(entry.index), 0xFFFF, 2);
}

String displayEmergencyTypeLabel(const String& type) {
  if (type == "FIRE") return "INCENDIO";
  if (type == "ACCIDENT") return "ACCIDENTE";
  if (type == "EVACUATION") return "EVACUACION";
  if (type == "MEDICAL") return "AUXILIO";
  return "EMERGENCIA";
}

uint16_t displayEmergencyAccentColor(const String& type) {
  if (type == "FIRE") return LCD_Color565(220, 38, 38);
  if (type == "ACCIDENT") return LCD_Color565(245, 158, 11);
  if (type == "EVACUATION") return LCD_Color565(234, 179, 8);
  if (type == "MEDICAL") return LCD_Color565(59, 130, 246);
  return LCD_Color565(127, 29, 29);
}

const EmergencyState* findDisplayPriorityEmergency() {
  const EmergencyState* selected = nullptr;
  uint32_t bestEpoch = 0;

  for (const auto& kv : emergencyStates) {
    const EmergencyState& state = kv.second;
    if (!state.active) continue;

    const uint32_t stateEpoch = state.updatedEpoch != 0 ? state.updatedEpoch : state.firstSeenEpoch;
    if (!selected
        || stateEpoch > bestEpoch
        || (stateEpoch == bestEpoch && state.eventId > selected->eventId)) {
      selected = &state;
      bestEpoch = stateEpoch;
    }
  }

  return selected;
}

String displayEmergencyPanelName(const EmergencyState& state) {
  return alarmPanelDisplayName(state.sourceId);
}

String displayEmergencyZoneLabel(const EmergencyState& state) {
  return alarmPanelDisplayZone(state.sourceId, state.zone);
}

String displayEmergencySignature(const EmergencyState& state, int activeCount) {
  return String(state.eventId)
    + "|" + state.type
    + "|" + displayEmergencyPanelName(state)
    + "|" + displayEmergencyZoneLabel(state)
    + "|" + String(activeCount);
}

String displayEmergencyButtonSignature(const EmergencyState& state) {
  const uint8_t holdProgress = displayEmergencyClearProgressStep(state);
  const bool holdTriggered = g_displayEmergencyClearTriggered && g_displayEmergencyClearHoldKey == displayEmergencyHoldKey(state);
  return String(holdProgress) + "|" + String(holdTriggered ? 1 : 0);
}

void updateDisplayEmergencyWakeState() {
  const EmergencyState* activeEmergency = findDisplayPriorityEmergency();
  if (!activeEmergency) {
    g_displayLastWakeEmergencyId = 0;
    resetDisplayEmergencyClearHold();
    return;
  }

  if (activeEmergency->eventId != 0 && activeEmergency->eventId != g_displayLastWakeEmergencyId) {
    g_displayLastWakeEmergencyId = activeEmergency->eventId;
    resetDisplayEmergencyClearHold();
    requestDisplayWake("emergencia");
  }
}

uint8_t displayEmergencyClearProgressStep(const EmergencyState& state) {
  if (g_displayEmergencyClearHoldKey != displayEmergencyHoldKey(state) || g_displayEmergencyClearHoldStartMs == 0) {
    return 0;
  }

  if (g_displayEmergencyClearTriggered) {
    return 20;
  }

  const unsigned long elapsed = millis() - g_displayEmergencyClearHoldStartMs;
  const unsigned long clamped = (elapsed > DISPLAY_EMERGENCY_CLEAR_HOLD_MS) ? DISPLAY_EMERGENCY_CLEAR_HOLD_MS : elapsed;
  return static_cast<uint8_t>((clamped * 20UL) / DISPLAY_EMERGENCY_CLEAR_HOLD_MS);
}

void serviceDisplayEmergencyClearTouch(const EmergencyState& state,
                                       uint16_t touchX,
                                       uint16_t touchY,
                                       unsigned long now) {
  const bool inside = displayPointInRect(
    touchX,
    touchY,
    DISPLAY_EMERGENCY_CLEAR_BTN_X,
    DISPLAY_EMERGENCY_CLEAR_BTN_Y,
    DISPLAY_EMERGENCY_CLEAR_BTN_W,
    DISPLAY_EMERGENCY_CLEAR_BTN_H);

  const String holdKey = displayEmergencyHoldKey(state);
  if (!inside) {
    if (!g_displayEmergencyClearHoldKey.isEmpty() && !g_displayEmergencyClearTriggered) {
      resetDisplayEmergencyClearHold();
    }
    return;
  }

  if (g_displayEmergencyClearHoldKey != holdKey) {
    g_displayEmergencyClearHoldKey = holdKey;
    g_displayEmergencyClearHoldStartMs = now;
    g_displayEmergencyClearTriggered = false;
    g_displayEmergencyButtonSignature = "";
    return;
  }

  if (g_displayEmergencyClearTriggered || g_displayEmergencyClearHoldStartMs == 0) {
    return;
  }

  if ((now - g_displayEmergencyClearHoldStartMs) < DISPLAY_EMERGENCY_CLEAR_HOLD_MS) {
    return;
  }

  uint32_t reqId = 0;
  if (requestClearForEmergency(state, &reqId)) {
    g_displayEmergencyClearTriggered = true;
    g_displayEmergencyButtonSignature = "";
    requestDisplayWake("clear local");
    Serial.printf("[LCD][EMERGENCY] Clear solicitado | addr=%s type=%s req=%lu\n",
                  state.sourceId.c_str(),
                  state.type.c_str(),
                  static_cast<unsigned long>(reqId));
  } else {
    resetDisplayEmergencyClearHold();
    Serial.println("[LCD][EMERGENCY] No se pudo solicitar clear");
  }
}

void drawDisplayEmergencyScreen(const EmergencyState& state, int activeCount) {
  static const uint16_t colorBg = 0xFFFF;
  static const uint16_t colorPanel = 0xFFFF;
  static const uint16_t colorText = 0x0000;
  static const uint16_t colorMuted = LCD_Color565(70, 70, 70);
  const uint16_t accent = displayEmergencyAccentColor(state.type);
  const uint16_t buttonBg = LCD_Color565(28, 28, 28);
  const uint16_t progressColor = accent;

  const String typeLabel = displayEmergencyTypeLabel(state.type);
  const String panelName = displayEmergencyPanelName(state);
  const String zoneLabel = displayEmergencyZoneLabel(state);
  const String seenAt = formatTimeAmPmFromEpoch(state.firstSeenEpoch != 0 ? state.firstSeenEpoch : state.updatedEpoch);
  const String countLabel = activeCount > 1 ? ("+" + String(activeCount - 1) + " MAS") : "ACTIVA";
  const uint8_t holdProgress = displayEmergencyClearProgressStep(state);
  const bool holdTriggered = g_displayEmergencyClearTriggered && g_displayEmergencyClearHoldKey == displayEmergencyHoldKey(state);
  const String signature = displayEmergencySignature(state, activeCount);
  if (signature != g_displayEmergencySignature) {
    g_displayEmergencySignature = signature;
    g_displayEmergencyButtonSignature = "";

    LCD_FillScreen(colorBg);

    drawDisplayRoundedFill(8, 10, 156, 42, 8, accent);
    drawDisplayTextCentered(86, 23, "EMERGENCIA", 0xFFFF, 2);

    drawDisplayRoundedFill(8, 62, 156, 64, 8, LCD_Color565(0, 0, 0));
    drawDisplayRoundedFill(10, 64, 152, 60, 7, colorPanel);
    drawDisplayTextCentered(86, 76, typeLabel, accent, (LCD_TextWidth(typeLabel, 2) <= 140) ? 2 : 1);
    drawDisplayTextCentered(86, 101, countLabel, colorMuted, 1);
    drawDisplayTextCentered(86, 113, seenAt.isEmpty() ? "SIN HORA" : ("Inicio " + seenAt), colorMuted, 1);

    drawDisplayRoundedFill(8, 138, 156, 66, 8, 0x0000);
    drawDisplayRoundedFill(10, 140, 152, 62, 7, colorPanel);
    LCD_DrawText(18, 149, "PANEL", colorMuted, 1);
    if (LCD_TextWidth(panelName, 2) <= 132) {
      drawDisplayTextCentered(86, 167, panelName, colorText, 2);
    } else {
      drawDisplayTextCentered(86, 171, panelName, colorText, 1);
    }

    drawDisplayRoundedFill(8, 214, 156, 66, 8, 0x0000);
    drawDisplayRoundedFill(10, 216, 152, 62, 7, colorPanel);
    LCD_DrawText(18, 225, "ZONA", colorMuted, 1);
    if (LCD_TextWidth(zoneLabel, 2) <= 132) {
      drawDisplayTextCentered(86, 243, zoneLabel, colorText, 2);
    } else {
      drawDisplayTextCentered(86, 247, zoneLabel, colorText, 1);
    }
  }

  const String buttonSignature = displayEmergencyButtonSignature(state);
  if (buttonSignature == g_displayEmergencyButtonSignature) {
    return;
  }
  g_displayEmergencyButtonSignature = buttonSignature;

  drawDisplayRoundedFill(DISPLAY_EMERGENCY_CLEAR_BTN_X,
                         DISPLAY_EMERGENCY_CLEAR_BTN_Y,
                         DISPLAY_EMERGENCY_CLEAR_BTN_W,
                         DISPLAY_EMERGENCY_CLEAR_BTN_H,
                         8,
                         0x0000);
  drawDisplayRoundedFill(static_cast<uint16_t>(DISPLAY_EMERGENCY_CLEAR_BTN_X + 1),
                         static_cast<uint16_t>(DISPLAY_EMERGENCY_CLEAR_BTN_Y + 1),
                         static_cast<uint16_t>(DISPLAY_EMERGENCY_CLEAR_BTN_W - 2),
                         static_cast<uint16_t>(DISPLAY_EMERGENCY_CLEAR_BTN_H - 2),
                         7,
                         buttonBg);

  if (holdProgress > 0) {
    const uint16_t innerWidth = static_cast<uint16_t>((DISPLAY_EMERGENCY_CLEAR_BTN_W - 4U) * holdProgress / 20U);
    if (innerWidth > 0) {
      drawDisplayRoundedFill(static_cast<uint16_t>(DISPLAY_EMERGENCY_CLEAR_BTN_X + 2),
                             static_cast<uint16_t>(DISPLAY_EMERGENCY_CLEAR_BTN_Y + 2),
                             innerWidth,
                             static_cast<uint16_t>(DISPLAY_EMERGENCY_CLEAR_BTN_H - 4),
                             6,
                             progressColor);
    }
  }

  String buttonLabel = "MANTENER 5S";
  if (holdTriggered) {
    buttonLabel = "SOLICITUD ENVIADA";
  } else if (holdProgress > 0) {
    const uint8_t percent = static_cast<uint8_t>(holdProgress * 5U);
    buttonLabel = "DESACTIVAR " + String(percent) + "%";
  }

  drawDisplayTextCentered(86,
                          static_cast<uint16_t>(DISPLAY_EMERGENCY_CLEAR_BTN_Y + 9),
                          buttonLabel,
                          0xFFFF,
                          1);
}

void renderDisplayStatus() {
  if (DISPLAY_COLOR_TEST_MODE) {
    if (!g_colorTestScreenReady) {
      renderColorTestScreen();
    }
    updateColorTestLed();
    return;
  }

  const EmergencyState* activeEmergency = findDisplayPriorityEmergency();
  if (activeEmergency) {
    drawDisplayEmergencyScreen(*activeEmergency, countActiveEmergencies());
    return;
  }

  if (!g_displayEmergencySignature.isEmpty() || !g_displayEmergencyButtonSignature.isEmpty()) {
    g_displayEmergencySignature = "";
    g_displayEmergencyButtonSignature = "";
    g_displayLayoutReady = false;
    g_displaySummaryKey = "";
    g_displayTimeKey = "";
    g_displayShowingEmpty = false;
    g_displayCurrentPage = SIZE_MAX;
    resetDisplayCardCache();
    resetDisplayDetailCache();
  }

  if (g_displayScreenMode == DISPLAY_SCREEN_DETAIL) {
    const NodeData* selectedNode = findDisplaySelectedNode();
    if (!selectedNode) {
      g_displayScreenMode = DISPLAY_SCREEN_OVERVIEW;
      g_displaySelectedMac = "";
      resetDisplayDetailCache();
      resetDisplayOverviewCache();
    } else {
      drawDisplayDetailScreen(*selectedNode);
      return;
    }
  }

  static const uint16_t colorPanel = 0xFFFF;
  static const uint16_t colorText = 0x0000;
  static const uint16_t colorDim = 0x0000;

  if (!g_displayLayoutReady) {
    drawDisplayBaseLayout();
  }

  const std::vector<const NodeData*> orderedNodes = buildOrderedDisplayNodes();

  const int onlineCount = countOnlineNodes();
  const int totalNodes = static_cast<int>(nodes.size());
  const size_t totalPages = orderedNodes.empty() ? 1 : ((orderedNodes.size() + DISPLAY_CARD_SLOTS - 1) / DISPLAY_CARD_SLOTS);
  const size_t page = (totalPages > 1) ? ((millis() / DISPLAY_PAGE_MS) % totalPages) : 0;

  const String summaryLeft = String(onlineCount) + "/" + String(totalNodes) + " ONLINE";
  const String timeText = masterTimeIsValid() ? formatTimeAmPmFromEpoch(masterNowEpoch()) : "SIN SYNC";

  const String summaryKey = summaryLeft;
  if (summaryKey != g_displaySummaryKey) {
    LCD_FillRect(12, 58, 146, 11, colorPanel);
    LCD_DrawText(18, 58, summaryLeft, colorText, 1);
    g_displaySummaryKey = summaryKey;
  }

  const String timeKey = timeText;
  if (timeKey != g_displayTimeKey) {
    drawDisplayTextDelta(50, 72, g_displayTimeKey, timeText, colorDim, colorPanel, 1);
    g_displayTimeKey = timeKey;
  }

  const bool pageChanged = page != g_displayCurrentPage;
  if (pageChanged) {
    g_displayCurrentPage = page;
    g_displayShowingEmpty = false;
    resetDisplayCardCache();
  }

  if (orderedNodes.empty()) {
    if (!g_displayShowingEmpty) {
      drawEmptyDisplayState();
      g_displayShowingEmpty = true;
    }
    return;
  }

  if (g_displayShowingEmpty) {
    LCD_FillRect(10, 100, 152, 216, 0xFFFF);
    resetDisplayCardCache();
  }
  g_displayShowingEmpty = false;
  const size_t startIndex = page * DISPLAY_CARD_SLOTS;
  for (uint8_t slot = 0; slot < DISPLAY_CARD_SLOTS; ++slot) {
    const size_t nodeIndex = startIndex + slot;
    if (nodeIndex >= orderedNodes.size()) {
      if (g_displayCardCache[slot].occupied) {
        clearDisplayCardSlot(slot);
        g_displayCardCache[slot].occupied = false;
        g_displayCardCache[slot].signature = "";
      }
      continue;
    }

    const NodeData& n = *orderedNodes[nodeIndex];
    const String signature = displayCardSignature(n);
    if (!g_displayCardCache[slot].occupied || g_displayCardCache[slot].signature != signature) {
      drawSensorCard(slot, n);
      g_displayCardCache[slot].occupied = true;
      g_displayCardCache[slot].signature = signature;
    }
  }
}

void displayUiInit() {
  if (DISPLAY_COLOR_TEST_MODE) {
    printColorTestLegend();
  }

  LCD_Init();
  Set_Backlight(DISPLAY_ACTIVE_BRIGHTNESS);
  LCD_FillScreen(0x0000);
  LCD_DrawText(18, 16, "MAESTRO", 0xFFFF, 2);
  LCD_DrawText(18, 46, "INICIANDO...", LCD_Color565(120, 220, 255), 2);
  g_displayAwake = true;
  g_displayWakeRequested = false;
  g_displayLastTouchActivityMs = millis();
  g_displayTouchDown = false;
  g_displayScreenMode = DISPLAY_SCREEN_OVERVIEW;
  g_displaySelectedMac = "";
  g_displayEmergencySignature = "";
  g_displayEmergencyButtonSignature = "";
  g_displayLastWakeEmergencyId = 0;
  g_displayWakeReason = "inicio";
  resetDisplayDetailCache();
  resetDisplayOverviewCache();
  if (!LCD_TouchAvailable()) {
    Serial.println("[LCD] Touch no disponible");
  }
  Serial.println("[LCD] Pantalla encendida");
  Serial.println("[LCD] Motivo: inicio");
}

void displayUiStart() {
  renderDisplayStatus();
  if (DISPLAY_COLOR_TEST_MODE) {
    updateColorTestLed(true);
  }
  lastDisplayRefreshMs = millis();
}

void displayUiUpdate(unsigned long now) {
  updateDisplayTouchState(now);
  updateDisplayEmergencyWakeState();
  wakeDisplayIfRequested();
  updateDisplaySleepState(now);

  if (g_displayAwake && (now - lastDisplayRefreshMs >= DISPLAY_REFRESH_MS)) {
    lastDisplayRefreshMs = now;
    renderDisplayStatus();
  }
}
