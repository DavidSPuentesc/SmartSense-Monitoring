#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <map>
#include <math.h>

// =====================================================
// WIFI
// =====================================================
const char* WIFI_SSID = "elyisus";
const char* WIFI_PASS = "4j6vx7gm:DEDOS";

// =====================================================
// IP FIJA DEL MAESTRO
// =====================================================
IPAddress local_IP(192, 168, 0, 33);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

// =====================================================
// SERVIDOR
// =====================================================
AsyncWebServer server(80);
AsyncEventSource events("/events");

// =====================================================
// CONFIG
// =====================================================
static const size_t MAX_TEMP_HISTORY = 600;
static const uint32_t DEFAULT_SLEEP_TIME = 60;
static const float DEFAULT_TEMP_OFFSET = 0.0f;
static const char* DEFAULT_SENSOR_NAME = "SENSOR_1";
static uint32_t g_timeBaseEpoch = 0;                 // epoch recibido desde la web
static uint32_t g_timeBaseMillis = 0;                // millis del maestro cuando se sincronizó
static const int32_t TZ_OFFSET_SECONDS = -5 * 3600;  // Colombia UTC-5
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
  float temperature = NAN;
  float battery = NAN;
  uint32_t uptimeMs = 0;
  unsigned long lastSeen = 0;
  String remoteIp;
  int rssi = 0;

  float tempOffset = DEFAULT_TEMP_OFFSET;
  uint32_t sleepSec = DEFAULT_SLEEP_TIME;

  float tempHistory[MAX_TEMP_HISTORY];
  char timeHistory[MAX_TEMP_HISTORY][12];
  size_t tempCount = 0;
  size_t tempHead = 0;
};

std::map<String, NodeData> nodes;

// =====================================================
// TIMERS
// =====================================================
static unsigned long lastBroadcastMs = 0;
static unsigned long lastStatusPrintMs = 0;
static unsigned long lastWiFiRetryMs = 0;

// =====================================================
// HTML
// =====================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html lang="es">

<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>PACKING</title>
    <style>
        :root {
            --bg: #0b1220;
            --panel: #111827;
            --card: #1f2937;
            --text: #e5e7eb;
            --muted: #94a3b8;
            --green: #22c55e;
            --orange: #f59e0b;
            --red: #ef4444;
            --blue: #3b82f6;
            --border: rgba(255, 255, 255, .08);
            --shadow: 0 12px 28px rgba(0, 0, 0, .28);
            --radius: 18px;
        }

        * {
            box-sizing: border-box
        }

        html,
        body {
            margin: 0;
            padding: 0
        }

        body {
            font-family: Arial, Helvetica, sans-serif;
            background: linear-gradient(180deg, #09111f 0%, #111827 100%);
            color: var(--text);
        }

        .app {
            width: min(1320px, 96%);
            margin: 0 auto;
            padding: 18px 0 28px;
        }

        .topbar {
            display: flex;
            justify-content: space-between;
            align-items: center;
            gap: 14px;
            flex-wrap: wrap;
            margin-bottom: 18px;
        }

        .title h1 {
            margin: 0;
            font-size: 30px;
            letter-spacing: .5px;
        }

        .tabs {
            display: flex;
            gap: 10px;
            flex-wrap: wrap;
        }

        .tab-btn,
        .btn {
            border: none;
            cursor: pointer;
            border-radius: 14px;
            padding: 11px 16px;
            font-weight: 700;
            color: #fff;
            background: rgba(255, 255, 255, .05);
            border: 1px solid var(--border);
        }

        .tab-btn.active {
            background: rgba(59, 130, 246, .20);
        }

        .btn-green {
            background: rgba(34, 197, 94, .18);
        }

        .btn-blue {
            background: rgba(59, 130, 246, .18);
        }

        .btn-close {
            background: rgba(239, 68, 68, .18);
        }

        .summary {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 14px;
            margin-bottom: 18px;
        }

        .card {
            background: rgba(17, 24, 39, .92);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            box-shadow: var(--shadow);
            padding: 18px;
        }

        .summary .value {
            font-size: 28px;
            font-weight: 800;
            margin-top: 6px;
        }

        .muted {
            color: var(--muted);
        }

        .screen {
            display: none;
        }

        .screen.active {
            display: block;
        }

        .nodes-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(215px, 1fr));
            gap: 12px;
        }

        .node-tile {
            background: rgba(31, 41, 55, .92);
            border: 1px solid var(--border);
            border-radius: 16px;
            padding: 12px;
            box-shadow: var(--shadow);
            cursor: pointer;
            transition: transform .14s ease, border-color .14s ease;
            min-height: 150px;
            display: flex;
            flex-direction: column;
            justify-content: space-between;
        }

        .node-tile:hover {
            transform: translateY(-2px);
            border-color: rgba(255, 255, 255, .16);
        }

        .node-tile-top {
            display: flex;
            justify-content: space-between;
            align-items: flex-start;
            gap: 8px;
            margin-bottom: 8px;
        }

        .node-name {
            font-size: 15px;
            font-weight: 800;
            line-height: 1.2;
        }

        .badge {
            display: inline-block;
            padding: 5px 9px;
            border-radius: 999px;
            font-size: 11px;
            font-weight: 800;
            border: 1px solid transparent;
            white-space: nowrap;
        }

        .badge.online {
            background: rgba(34, 197, 94, .15);
            border-color: rgba(34, 197, 94, .25);
            color: #d1fae5;
        }

        .badge.offline {
            background: rgba(239, 68, 68, .14);
            border-color: rgba(239, 68, 68, .24);
            color: #fee2e2;
        }

        .tile-main-temp {
            font-size: 42px;
            font-weight: 900;
            line-height: 1;
            margin: 8px 0 6px;
        }

        .temp-green {
            color: var(--green);
        }

        .temp-orange {
            color: var(--orange);
        }

        .temp-red {
            color: var(--red);
        }

        .tile-bottom {
            display: flex;
            justify-content: space-between;
            align-items: flex-end;
            gap: 10px;
        }

        .battery-inline {
            font-size: 13px;
            color: var(--muted);
            font-weight: 700;
        }

        .detail-head {
            display: flex;
            justify-content: space-between;
            align-items: center;
            gap: 12px;
            flex-wrap: wrap;
            margin-bottom: 16px;
        }

        .detail-title {
            font-size: 24px;
            font-weight: 800;
            margin: 0;
        }

        .detail-sub {
            margin-top: 4px;
            color: var(--muted);
            font-size: 13px;
            word-break: break-all;
        }

        .detail-grid {
            display: grid;
            grid-template-columns: 1fr;
            gap: 16px;
        }

        .tile-kpis {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 10px;
            margin-top: 0;
            margin-bottom: 14px;
        }

        .mini {
            background: rgba(255, 255, 255, .03);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 12px;
        }

        .mini .label {
            font-size: 11px;
            color: var(--muted);
            margin-bottom: 5px;
        }

        .mini .value {
            font-size: 18px;
            font-weight: 800;
        }

        .chart-wrap {
            background: rgba(255, 255, 255, .025);
            border: 1px solid var(--border);
            border-radius: 16px;
            padding: 12px;
        }

        .chart-title {
            font-size: 13px;
            color: var(--muted);
            margin-bottom: 8px;
        }

        .chart {
            width: 100%;
            height: 320px;
            display: block;
            border-radius: 12px;
            image-rendering: auto;
        }

        .config-grid {
            display: grid;
            grid-template-columns: 1fr;
            gap: 12px;
        }

        .field label {
            display: block;
            font-size: 12px;
            color: var(--muted);
            margin-bottom: 6px;
        }

        .field input {
            width: 100%;
            padding: 11px 12px;
            border-radius: 12px;
            border: 1px solid var(--border);
            background: rgba(255, 255, 255, .04);
            color: var(--text);
            outline: none;
        }

        .field input:focus {
            border-color: rgba(59, 130, 246, .45);
        }

        .empty {
            text-align: center;
            color: var(--muted);
            padding: 32px 14px;
            border: 1px dashed rgba(255, 255, 255, .10);
            border-radius: 16px;
        }

        .hidden {
            display: none !important;
        }

        /* MODAL */
        .modal-overlay {
            position: fixed;
            inset: 0;
            background: rgba(0, 0, 0, .55);
            backdrop-filter: blur(4px);
            display: none;
            align-items: center;
            justify-content: center;
            padding: 20px;
            z-index: 999;
        }

        .modal-overlay.show {
            display: flex;
        }

        .modal {
            width: min(480px, 100%);
            background: rgba(17, 24, 39, .98);
            border: 1px solid var(--border);
            border-radius: 18px;
            box-shadow: 0 20px 50px rgba(0, 0, 0, .45);
            padding: 18px;
        }

        .modal-head {
            display: flex;
            justify-content: space-between;
            align-items: center;
            gap: 12px;
            margin-bottom: 14px;
        }

        .modal-title {
            margin: 0;
            font-size: 20px;
            font-weight: 800;
        }

        .modal-actions {
            display: flex;
            gap: 10px;
            margin-top: 14px;
            flex-wrap: wrap;
        }

        @media (max-width: 920px) {
            .tile-kpis {
                grid-template-columns: 1fr 1fr;
            }

            .chart {
                height: 260px;
            }
        }

        @media (max-width: 640px) {
            .tile-kpis {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>

<body>
    <div class="app">
        <div class="topbar">
            <div class="title">
                <h1>PACKING</h1>
            </div>

            <div class="tabs">
                <button id="btn-tab-detail" class="tab-btn" onclick="switchScreen('detail')">Detalles</button>
                <button id="btn-tab-home" class="btn btn-blue hidden" onclick="switchScreen('nodes')">Inicio</button>
                <button id="btn-tab-config" class="btn btn-blue hidden"
                    onclick="openConfigModal()">Configuración</button>

                <button id="btn-sync-time" class="btn btn-blue hidden" onclick="syncMasterTime()">
                    Sincronizar hora
                </button>
            </div>
        </div>

        <section id="screen-nodes" class="screen active">
            <div id="nodesContainer" class="nodes-grid"></div>
        </section>

        <section id="screen-master" class="screen">
            <div class="summary">
                <div class="card">
                    <div class="muted">IP maestro</div>
                    <div id="masterInfoIp" class="value">--</div>
                </div>
                <div class="card">
                    <div class="muted">Nodos total</div>
                    <div id="masterInfoNodesTotal" class="value">0</div>
                </div>
                <div class="card">
                    <div class="muted">Nodos online</div>
                    <div id="masterInfoNodesOnline" class="value">0</div>
                </div>
                <div class="card">
                    <div class="muted">Canal</div>
                    <div id="masterInfoLive" class="value" style="font-size:20px;">--</div>
                </div>
            </div>

            <div class="summary">
                <div class="card">
                    <div class="muted">Señal WiFi</div>
                    <div id="masterInfoRssi" class="value">--</div>
                </div>
                <div class="card">
                    <div class="muted">SSID WiFi</div>
                    <div id="masterInfoSsid" class="value" style="font-size:20px;">--</div>
                </div>
                <div class="card">
                    <div class="muted">Heap libre</div>
                    <div id="masterInfoHeap" class="value">--</div>
                </div>
                <div class="card">
                    <div class="muted">Uptime</div>
                    <div id="masterInfoUptime" class="value" style="font-size:18px; line-height:1.35;">--</div>
                </div>
                <div class="card">
                    <div class="muted">Hora actual</div>
                    <div id="masterInfoNow" class="value" style="font-size:20px;">--</div>
                </div>
            </div>
        </section>

        <section id="screen-detail" class="screen">
            <div id="detailEmpty" class="empty">
                Selecciona un nodo para ver su información.
            </div>

            <div id="detailContent" style="display:none;">
                <div class="card detail-head">
                    <div>
                        <div id="detailName" class="detail-title">--</div>
                        <div id="detailMac" class="detail-sub">--</div>
                    </div>
                    <div>
                        <span id="detailStatus" class="badge offline">OFFLINE</span>
                    </div>
                </div>

                <div class="detail-grid">
                    <div class="card">
                        <div class="tile-kpis">
                            <div class="mini">
                                <div class="label">Temperatura</div>
                                <div id="detailTemp" class="value">--</div>
                            </div>
                            <div class="mini">
                                <div class="label">Batería</div>
                                <div id="detailBatt" class="value">--</div>
                            </div>
                            <div class="mini">
                                <div class="label">Señal WiFi</div>
                                <div id="detailRssi" class="value">--</div>
                            </div>
                            <div class="mini">
                                <div class="label">IP nodo</div>
                                <div id="detailNodeIp" class="value" style="font-size:14px;line-height:1.25;">--</div>
                            </div>
                        </div>

                        <div class="chart-wrap">
                            <div class="chart-title">Temperatura</div>
                            <canvas id="detailTempChart" class="chart"></canvas>
                        </div>
                    </div>
                </div>
            </div>
        </section>
    </div>

    <!-- MODAL CONFIG -->
    <div id="configModalOverlay" class="modal-overlay" onclick="closeConfigModal(event)">
        <div class="modal" onclick="event.stopPropagation()">
            <div class="modal-head">
                <h3 class="modal-title">Configuración</h3>
            </div>

            <div class="config-grid">
                <div class="field">
                    <label for="cfgName">Nombre</label>
                    <input id="cfgName" type="text">
                </div>

                <div class="field">
                    <label for="cfgSleep">Sleep time (s)</label>
                    <input id="cfgSleep" type="number" min="5" max="7200" step="1">
                </div>

                <div class="field">
                    <label for="cfgOffset">Offset</label>
                    <input id="cfgOffset" type="number" step="0.01">
                </div>

                <div class="modal-actions">
                    <button class="btn btn-green" onclick="saveSelectedNodeConfig()">Guardar</button>
                    <button class="btn" onclick="closeConfigModal()">Cancelar</button>
                </div>
            </div>
        </div>
    </div>

    <script>
        let lastSnapshot = null;
        let selectedMac = null;
        let editingConfig = false;


        function fmt(v, digits = 1, suffix = '') {
            if (v === null || v === undefined || Number.isNaN(Number(v))) return '--';
            return Number(v).toFixed(digits) + suffix;
        }

        function escapeHtml(str) {
            return String(str ?? '')
                .replaceAll('&', '&amp;')
                .replaceAll('<', '&lt;')
                .replaceAll('>', '&gt;')
                .replaceAll('"', '&quot;')
                .replaceAll("'", '&#39;');
        }

        function safeArray(arr) {
            return Array.isArray(arr) ? arr : [];
        }

        function getNodeByMac(mac) {
            if (!lastSnapshot || !Array.isArray(lastSnapshot.nodes)) return null;
            return lastSnapshot.nodes.find(n => n.mac === mac) || null;
        }

        function tempClass(temp) {
            const t = Number(temp);
            if (!Number.isFinite(t)) return 'temp-green';
            if (t < 50) return 'temp-green';
            if (t < 60) return 'temp-orange';
            return 'temp-red';
        }

        function updateTopbarButtons(mode) {
            const btnDetail = document.getElementById('btn-tab-detail');
            const btnHome = document.getElementById('btn-tab-home');
            const btnConfig = document.getElementById('btn-tab-config');
            const btnSyncTime = document.getElementById('btn-sync-time');

            // Ocultar todo
            btnDetail.classList.add('hidden');
            btnHome.classList.add('hidden');
            btnConfig.classList.add('hidden');
            btnSyncTime.classList.add('hidden');

            btnDetail.classList.remove('active');

            if (mode === 'nodes') {
                // INICIO → solo "Detalles"
                btnDetail.classList.remove('hidden');

            } else if (mode === 'master') {
                // DETALLES MAESTRO → Inicio + Sincronizar hora
                btnHome.classList.remove('hidden');
                btnSyncTime.classList.remove('hidden');

            } else if (mode === 'node') {
                // DETALLE NODO → Inicio + Config
                btnHome.classList.remove('hidden');
                btnConfig.classList.remove('hidden');
            }
        }

        function switchScreen(name) {
            const screenNodes = document.getElementById('screen-nodes');
            const screenMaster = document.getElementById('screen-master');
            const screenDetail = document.getElementById('screen-detail');

            screenNodes.classList.remove('active');
            screenMaster.classList.remove('active');
            screenDetail.classList.remove('active');

            closeConfigModal();

            if (name === 'detail') {
                screenMaster.classList.add('active');
                updateTopbarButtons('master');
            } else if (name === 'node') {
                screenDetail.classList.add('active');
                updateTopbarButtons('node');
            } else {
                screenNodes.classList.add('active');
                updateTopbarButtons('nodes');
            }
        }

        function selectNode(mac) {
            selectedMac = mac;
            editingConfig = false;
            switchScreen('node');
            renderDetail();
        }

        function openConfigModal() {
            const n = getNodeByMac(selectedMac);
            if (!n) return;

            setupConfigInputsWatchers();

            if (!editingConfig) {
                document.getElementById('cfgName').value = n.name || '';
                document.getElementById('cfgSleep').value = parseInt(n.sleepSec || 60, 10);
                document.getElementById('cfgOffset').value = Number(n.tempOffset || 0).toFixed(2);
            }

            document.getElementById('configModalOverlay').classList.add('show');
        }

        function closeConfigModal(event) {
            if (event && event.target !== event.currentTarget) return;
            document.getElementById('configModalOverlay').classList.remove('show');
            editingConfig = false;
        }

        function prepareHiDPICanvas(canvas) {
            if (!canvas) return null;

            const dpr = window.devicePixelRatio || 1;
            const cssWidth = canvas.clientWidth || 600;
            const cssHeight = canvas.clientHeight || 320;

            const targetWidth = Math.round(cssWidth * dpr);
            const targetHeight = Math.round(cssHeight * dpr);

            if (canvas.width !== targetWidth || canvas.height !== targetHeight) {
                canvas.width = targetWidth;
                canvas.height = targetHeight;
            }

            const ctx = canvas.getContext('2d');
            ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            ctx.clearRect(0, 0, cssWidth, cssHeight);

            return { ctx, width: cssWidth, height: cssHeight };
        }

        function drawLineChart(canvas, values, labels, options = {}) {
            const p = prepareHiDPICanvas(canvas);
            if (!p) return;

            const { ctx, width, height } = p;

            const padL = 42;
            const padR = 12;
            const padT = 16;
            const padB = 34;

            const plotW = width - padL - padR;
            const plotH = height - padT - padB;

            const rawValues = safeArray(values).map(Number);
            const rawLabels = safeArray(labels);

            const points = [];
            for (let i = 0; i < rawValues.length; i++) {
                if (Number.isFinite(rawValues[i])) {
                    points.push({ value: rawValues[i], label: rawLabels[i] || '', idx: i });
                }
            }

            ctx.fillStyle = 'rgba(255,255,255,0.02)';
            ctx.fillRect(0, 0, width, height);

            if (points.length === 0) {
                ctx.fillStyle = '#94a3b8';
                ctx.font = '12px Arial';
                ctx.textAlign = 'center';
                ctx.fillText('Sin histórico', width / 2, height / 2);
                return;
            }

            let minY = Number.isFinite(options.minY) ? options.minY : Math.min(...points.map(p => p.value));
            let maxY = Number.isFinite(options.maxY) ? options.maxY : Math.max(...points.map(p => p.value));

            if (options.expandIfOutOfRange) {
                const realMin = Math.min(...points.map(p => p.value));
                const realMax = Math.max(...points.map(p => p.value));
                if (realMin < minY) minY = Math.floor(realMin - 1);
                if (realMax > maxY) maxY = Math.ceil(realMax + 1);
            }

            if ((maxY - minY) < 1) {
                minY -= 0.5;
                maxY += 0.5;
            }

            ctx.strokeStyle = 'rgba(255,255,255,0.08)';
            ctx.lineWidth = 1;
            for (let i = 0; i < 5; i++) {
                const y = padT + (i * plotH / 4);
                ctx.beginPath();
                ctx.moveTo(padL, y);
                ctx.lineTo(width - padR, y);
                ctx.stroke();
            }

            ctx.beginPath();
            ctx.moveTo(padL, padT + plotH);
            ctx.lineTo(width - padR, padT + plotH);
            ctx.strokeStyle = 'rgba(255,255,255,0.12)';
            ctx.stroke();

            ctx.fillStyle = '#94a3b8';
            ctx.font = '10px Arial';
            ctx.textAlign = 'left';
            ctx.fillText(String(maxY.toFixed(0)), 4, padT + 4);
            ctx.fillText(String(((maxY + minY) / 2).toFixed(0)), 4, padT + plotH / 2 + 4);
            ctx.fillText(String(minY.toFixed(0)), 4, padT + plotH + 4);

            ctx.strokeStyle = options.lineColor || '#22c55e';
            ctx.lineWidth = 2.5;
            ctx.beginPath();

            points.forEach((pt, i) => {
                const x = padL + (points.length <= 1 ? plotW / 2 : i * (plotW / (points.length - 1)));
                const y = padT + (maxY - pt.value) * (plotH / (maxY - minY));
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            });

            ctx.stroke();

            const tickCount = 9;
            ctx.fillStyle = '#94a3b8';
            ctx.font = '10px Arial';
            ctx.textAlign = 'center';

            for (let t = 0; t < tickCount; t++) {
                const ratio = tickCount === 1 ? 0 : t / (tickCount - 1);
                const x = padL + ratio * plotW;
                const sampleIndex = Math.round(ratio * (points.length - 1));
                const label = points[sampleIndex]?.label || '';

                ctx.beginPath();
                ctx.moveTo(x, padT + plotH);
                ctx.lineTo(x, padT + plotH + 4);
                ctx.strokeStyle = 'rgba(255,255,255,0.10)';
                ctx.stroke();

                ctx.fillText(label, x, height - 8);
            }
        }

function renderMasterInfo() {
    if (!lastSnapshot) return;

    const setText = (id, value) => {
        const el = document.getElementById(id);
        if (el) el.textContent = value ?? '--';
    };

    setText('masterInfoIp', lastSnapshot.ip || '--');
    setText('masterInfoNodesTotal', lastSnapshot.nodesTotal ?? 0);
    setText('masterInfoNodesOnline', lastSnapshot.nodesOnline ?? 0);
    setText('masterInfoLive', lastSnapshot.channel ?? '--');

    const rssiPct = rssiToPercent(lastSnapshot.rssi);
    setText('masterInfoRssi', rssiPct === '--' ? '--' : `${rssiPct} %`);

    setText('masterInfoSsid', lastSnapshot.ssid || '--');
    setText('masterInfoHeap', lastSnapshot.heap != null ? `${lastSnapshot.heap}` : '--');
    setText('masterInfoUptime', lastSnapshot.uptime || '--');

    const synced = lastSnapshot.timeSynced === true;
    const nowLabel = lastSnapshot.nowLabel || '--:--:-- --';
    setText('masterInfoNow', synced ? nowLabel : 'No sincronizada');
}

        function renderNodes() {
            const container = document.getElementById('nodesContainer');
            if (!container) return;

            const nodes = safeArray(lastSnapshot?.nodes);

            if (nodes.length === 0) {
                container.innerHTML = `<div class="empty">Todavía no ha llegado información desde ningún nodo.</div>`;
                return;
            }

            container.innerHTML = nodes.map(n => `
                <div class="node-tile" onclick="selectNode('${escapeHtml(n.mac || '')}')">
                    <div class="node-tile-top">
                        <div>
                            <div class="node-name">${escapeHtml(n.name || 'Nodo')}</div>
                        </div>
                        <span class="badge ${n.online ? 'online' : 'offline'}">
                            ${n.online ? 'ONLINE' : 'OFFLINE'}
                        </span>
                    </div>

                    <div class="tile-main-temp ${tempClass(n.temperature)}">
                        ${fmt(n.temperature, 1, '°')}
                    </div>

                    <div class="tile-bottom">
                        <div></div>
                        <div class="battery-inline">${fmt(n.battery, 0, '%')}</div>
                    </div>
                </div>
            `).join('');
        }

        function setupConfigInputsWatchers() {
            ['cfgName', 'cfgSleep', 'cfgOffset'].forEach(id => {
                const el = document.getElementById(id);
                if (!el || el.dataset.watchBound === '1') return;

                el.addEventListener('focus', () => { editingConfig = true; });
                el.addEventListener('input', () => { editingConfig = true; });
                el.addEventListener('blur', () => {
                    const active = document.activeElement;
                    const ids = ['cfgName', 'cfgSleep', 'cfgOffset'];
                    editingConfig = ids.includes(active?.id);
                });

                el.dataset.watchBound = '1';
            });
        }

        function renderDetail() {
            const empty = document.getElementById('detailEmpty');
            const content = document.getElementById('detailContent');

            const n = getNodeByMac(selectedMac);

            if (!n) {
                empty.style.display = '';
                content.style.display = 'none';
                return;
            }

            empty.style.display = 'none';
            content.style.display = '';

            document.getElementById('detailName').textContent = n.name || 'Nodo';
            document.getElementById('detailMac').textContent = n.mac || '--';
            document.getElementById('detailTemp').textContent = fmt(n.temperature, 1, ' °C');
            document.getElementById('detailBatt').textContent = fmt(n.battery, 0, ' %');
            const rssiPct = rssiToPercent(n.rssi);
            document.getElementById('detailRssi').textContent = rssiPct === '--' ? '--' : `${rssiPct} %`;
            document.getElementById('detailNodeIp').textContent = n.remoteIp || '--';

            const badge = document.getElementById('detailStatus');
            badge.textContent = n.online ? 'ONLINE' : 'OFFLINE';
            badge.className = `badge ${n.online ? 'online' : 'offline'}`;

            drawLineChart(
                document.getElementById('detailTempChart'),
                safeArray(n.tempHistory),
                safeArray(n.timeHistory),
                {
                    minY: 20,
                    maxY: 50,
                    expandIfOutOfRange: true,
                    lineColor: '#22c55e'
                }
            );
        }

        async function saveSelectedNodeConfig() {
            const n = getNodeByMac(selectedMac);
            if (!n) return;

            const newName = document.getElementById('cfgName').value.trim();
            const sleepSec = parseInt(document.getElementById('cfgSleep').value, 10);
            const tempOffset = parseFloat(document.getElementById('cfgOffset').value);

            const payload = {
                mac: n.mac,
                name: newName || n.name || '',
                sleepSec: Number.isFinite(sleepSec) ? sleepSec : (n.sleepSec ?? 60),
                tempOffset: Number.isFinite(tempOffset) ? tempOffset : (n.tempOffset ?? 0)
            };

            try {
                const res = await fetch('/api/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(payload)
                });

                if (!res.ok) throw new Error(`HTTP ${res.status}`);

                editingConfig = false;
                closeConfigModal();
                await fetchSnapshot();
            } catch (err) {
                console.error('Error guardando configuración:', err);
                alert('No se pudo guardar la configuración');
            }
        }

        function applySnapshot(d) {
            lastSnapshot = d;
            renderMasterInfo();
            renderNodes();
            renderDetail();
        }

        function rssiToPercent(rssi) {
            const v = Number(rssi);
            if (!Number.isFinite(v)) return '--';

            // Escala típica WiFi:
            // -100 dBm = 0%
            // -50 dBm = 100%
            if (v <= -100) return 0;
            if (v >= -50) return 100;

            return Math.round(2 * (v + 100));
        }

        async function syncMasterTime() {
            try {
                const epoch = Math.floor(Date.now() / 1000);

                const res = await fetch('/api/time', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ epoch })
                });

                if (!res.ok) throw new Error(`HTTP ${res.status}`);

                await fetchSnapshot();
                alert('Hora del maestro sincronizada');
            } catch (err) {
                console.error('Error sincronizando hora:', err);
                alert('No se pudo sincronizar la hora');
            }
        }

        window.addEventListener('resize', () => {
            renderDetail();
        });

        window.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') closeConfigModal();
        });

        window.addEventListener('DOMContentLoaded', () => {
            updateTopbarButtons('nodes');

            applySnapshot({
                ip: '--',
                ssid: '--',
                rssi: null,
                heap: null,
                uptime: '--',
                nodesTotal: 0,
                nodesOnline: 0,
                nodes: []
            });

            fetchSnapshot();
            setInterval(fetchSnapshot, 1000);

        });

        let fetchingSnapshot = false;

        async function fetchSnapshot() {
            if (fetchingSnapshot) return;
            fetchingSnapshot = true;

            try {
                const res = await fetch('/api/status', { cache: 'no-store' });
                if (!res.ok) throw new Error(`HTTP ${res.status}`);

                const data = await res.json();
                applySnapshot(data);
            } catch (err) {
                console.error('Error leyendo snapshot:', err);
            } finally {
                fetchingSnapshot = false;
            }
        }
    </script>
</body>

</html>

)rawliteral";

// =====================================================
// HELPERS
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
           (unsigned long)hh12,
           (unsigned long)mm,
           (unsigned long)ss,
           ampm);

  return String(buf);
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
  return mac;
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

String formatSampleTimeFromMillis(unsigned long ms) {
  uint32_t totalSec = ms / 1000;
  uint32_t hh = (totalSec / 3600) % 24;
  uint32_t mm = (totalSec / 60) % 60;
  uint32_t ss = totalSec % 60;

  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
           (unsigned long)hh,
           (unsigned long)mm,
           (unsigned long)ss);
  return String(buf);
}

void loadNodeConfig(const String& mac, NodeData& n, const String& fallbackName) {
  String ns = macToNs(mac);

  prefs.begin(ns.c_str(), true);

  String cfgName = prefs.getString("name", fallbackName.length() ? fallbackName : DEFAULT_SENSOR_NAME);
  float cfgOffset = prefs.getFloat("offset", DEFAULT_TEMP_OFFSET);
  uint32_t cfgSleep = prefs.getUInt("sleep", DEFAULT_SLEEP_TIME);

  prefs.end();

  if (cfgSleep < 5) cfgSleep = 5;
  if (cfgSleep > 7200) cfgSleep = 7200;

  n.name = cfgName;
  n.tempOffset = cfgOffset;
  n.sleepSec = cfgSleep;
}

void saveNodeConfig(const String& mac, const String& name, float offset, uint32_t sleepSec) {
  String ns = macToNs(mac);

  prefs.begin(ns.c_str(), false);
  prefs.putString("name", name);
  prefs.putFloat("offset", offset);
  prefs.putUInt("sleep", sleepSec);
  prefs.end();
}

uint32_t offlineThresholdMs(const NodeData& n) {
  uint32_t s = n.sleepSec;
  if (s < 5) s = DEFAULT_SLEEP_TIME;
  return s * 1000UL + 60000UL;
}

bool isNodeOnline(const NodeData& n) {
  if (n.lastSeen == 0) return false;
  return (millis() - n.lastSeen) <= offlineThresholdMs(n);
}

void pushTempHistory(NodeData& n, float tempC) {
  if (!isfinite(tempC)) return;
  if (tempC < -100.0f || tempC > 300.0f) return;

  n.tempHistory[n.tempHead] = tempC;

  String label;
  uint32_t nowEpoch = masterNowEpoch();

  if (nowEpoch != 0) {
    label = formatTimeAmPmFromEpoch(nowEpoch);
  } else {
    label = formatSampleTimeFromMillis(millis());
  }

  strlcpy(n.timeHistory[n.tempHead], label.c_str(), sizeof(n.timeHistory[n.tempHead]));

  n.tempHead = (n.tempHead + 1) % MAX_TEMP_HISTORY;

  if (n.tempCount < MAX_TEMP_HISTORY) {
    n.tempCount++;
  }
}

void handleSetTimeBody(AsyncWebServerRequest* request,
                       uint8_t* data,
                       size_t len,
                       size_t index,
                       size_t total) {
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

  for (size_t i = 0; i < len; i++) {
    (*body) += (char)data[i];
  }

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
  Serial.printf("[TIME] Hora sincronizada. epoch=%lu\n", (unsigned long)g_timeBaseEpoch);
}

String historyToJsonArray(const NodeData& n) {
  String out = "[";
  bool first = true;

  size_t start = (n.tempCount < MAX_TEMP_HISTORY) ? 0 : n.tempHead;

  for (size_t i = 0; i < n.tempCount; i++) {
    size_t idx = (start + i) % MAX_TEMP_HISTORY;
    if (!first) out += ",";
    first = false;
    out += isfinite(n.tempHistory[idx]) ? String(n.tempHistory[idx], 2) : "null";
  }

  out += "]";
  return out;
}

String timeHistoryToJsonArray(const NodeData& n) {
  String out = "[";
  bool first = true;

  size_t start = (n.tempCount < MAX_TEMP_HISTORY) ? 0 : n.tempHead;

  for (size_t i = 0; i < n.tempCount; i++) {
    size_t idx = (start + i) % MAX_TEMP_HISTORY;
    if (!first) out += ",";
    first = false;

    out += "\"";
    out += jsonEscape(String(n.timeHistory[idx]));
    out += "\"";
  }

  out += "]";
  return out;
}

String buildJsonSnapshot() {
  String out;
  out.reserve(16000);

  int onlineCount = 0;
  for (const auto& kv : nodes) {
    if (isNodeOnline(kv.second)) onlineCount++;
  }

  out += "{";
  out += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  out += "\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
  out += "\"channel\":" + String(WiFi.channel()) + ",";
  out += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  out += "\"uptime\":\"" + formatUptime() + "\",";
  out += "\"nowEpoch\":" + String(masterNowEpoch()) + ",";
  out += "\"nowLabel\":\"" + jsonEscape(formatTimeAmPmFromEpoch(masterNowEpoch())) + "\",";
  out += "\"timeSynced\":";
  out += (masterTimeIsValid() ? "true" : "false");
  out += ",";
  out += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  out += "\"nodesTotal\":" + String((int)nodes.size()) + ",";
  out += "\"nodesOnline\":" + String(onlineCount) + ",";
  out += "\"nodes\":[";

  bool firstNode = true;
  for (const auto& kv : nodes) {
    const NodeData& n = kv.second;

    if (!firstNode) out += ",";
    firstNode = false;

    out += "{";
    out += "\"mac\":\"" + jsonEscape(n.mac) + "\",";
    out += "\"name\":\"" + jsonEscape(n.name) + "\",";
    out += "\"temperature\":";
    out += isfinite(n.temperature) ? String(n.temperature, 2) : "null";
    out += ",";
    out += "\"battery\":";
    out += isfinite(n.battery) ? String(n.battery, 1) : "null";
    out += ",";
    out += "\"uptimeMs\":" + String(n.uptimeMs) + ",";
    out += "\"rssi\":" + String(n.rssi) + ",";
    out += "\"tempOffset\":" + String(n.tempOffset, 3) + ",";
    out += "\"sleepSec\":" + String(n.sleepSec) + ",";
    out += "\"remoteIp\":\"" + jsonEscape(n.remoteIp) + "\",";
    out += "\"online\":";
    out += (isNodeOnline(n) ? "true" : "false");
    out += ",";
    out += "\"tempHistory\":" + historyToJsonArray(n) + ",";
    out += "\"timeHistory\":" + timeHistoryToJsonArray(n);
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

bool connectWiFi() {
  WiFi.mode(WIFI_STA);

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("[WIFI] Error configurando IP fija");
    return false;
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("[WIFI] Conectando");
  uint32_t t0 = millis();

  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] No se pudo conectar");
    return false;
  }

  Serial.println("[WIFI] Conectado");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("[WIFI] Canal: ");
  Serial.println(WiFi.channel());
  return true;
}

void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWiFiRetryMs < 10000) return;
  lastWiFiRetryMs = millis();

  Serial.println("[WIFI] Reconectando...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

// =====================================================
// ENDPOINT: CONFIG PARA EL NODO
// =====================================================
void handleGetConfig(AsyncWebServerRequest* request) {
  if (!request->hasParam("mac")) {
    request->send(400, "application/json", "{\"apply\":false,\"error\":\"mac_requerida\"}");
    return;
  }

  String mac = normalizeMac(request->getParam("mac")->value());
  String reportedName = request->hasParam("name") ? request->getParam("name")->value() : "";

  NodeData& n = nodes[mac];
  n.mac = mac;

  if (n.name.isEmpty()) {
    loadNodeConfig(mac, n, reportedName);
  }

  String out = "{";
  out += "\"apply\":true,";
  out += "\"newName\":\"" + jsonEscape(n.name) + "\",";
  out += "\"tempOffset\":" + String(n.tempOffset, 3) + ",";
  out += "\"sleepSec\":" + String(n.sleepSec);
  out += "}";

  request->send(200, "application/json; charset=utf-8", out);
}

// =====================================================
// ENDPOINT: GUARDAR CONFIG DESDE LA WEB
// =====================================================
void handleSaveConfigBody(AsyncWebServerRequest* request,
                          uint8_t* data,
                          size_t len,
                          size_t index,
                          size_t total) {
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

  for (size_t i = 0; i < len; i++) {
    (*body) += (char)data[i];
  }

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
  float tempOffset = doc["tempOffset"] | DEFAULT_TEMP_OFFSET;
  uint32_t sleepSec = doc["sleepSec"] | DEFAULT_SLEEP_TIME;

  if (mac.isEmpty()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"mac_requerida\"}");
    return;
  }

  if (newName.isEmpty()) newName = DEFAULT_SENSOR_NAME;
  if (sleepSec < 5) sleepSec = 5;
  if (sleepSec > 7200) sleepSec = 7200;

  saveNodeConfig(mac, newName, tempOffset, sleepSec);

  NodeData& n = nodes[mac];
  n.mac = mac;
  n.name = newName;
  n.tempOffset = tempOffset;
  n.sleepSec = sleepSec;

  request->send(200, "application/json", "{\"ok\":true}");
  pushSnapshot();
}

// =====================================================
// ENDPOINT: RECEPCIÓN DE DATOS DEL NODO
// =====================================================
void handlePushBody(AsyncWebServerRequest* request,
                    uint8_t* data,
                    size_t len,
                    size_t index,
                    size_t total) {
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

  for (size_t i = 0; i < len; i++) {
    (*body) += (char)data[i];
  }

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
  String reportedName = doc["name"] | "";

  if (mac.isEmpty()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"mac_requerida\"}");
    return;
  }

  NodeData& n = nodes[mac];
  n.mac = mac;
  n.remoteIp = request->client()->remoteIP().toString();
  n.lastSeen = millis();

  if (n.name.isEmpty()) {
    loadNodeConfig(mac, n, reportedName);
  }

  if (doc["temperature"].is<float>() || doc["temperature"].is<double>() || doc["temperature"].is<int>()) {
    n.temperature = doc["temperature"].as<float>();
    pushTempHistory(n, n.temperature);
  }

  if (doc["battery"].is<float>() || doc["battery"].is<double>() || doc["battery"].is<int>()) {
    n.battery = doc["battery"].as<float>();
  }

  if (doc["uptime_ms"].is<uint32_t>() || doc["uptime_ms"].is<int>() || doc["uptime_ms"].is<unsigned long>()) {
    n.uptimeMs = doc["uptime_ms"].as<uint32_t>();
  }

  if (doc["rssi"].is<int>() || doc["rssi"].is<long>()) {
    n.rssi = doc["rssi"].as<int>();
  }

  Serial.printf("[RX] mac=%s name=%s temp=%.2f batt=%.1f rssi=%d sleep=%lu offset=%.3f onlineThr=%lu ms\n",
                n.mac.c_str(),
                n.name.c_str(),
                n.temperature,
                n.battery,
                n.rssi,
                (unsigned long)n.sleepSec,
                (double)n.tempOffset,
                (unsigned long)offlineThresholdMs(n));

  request->send(200, "application/json", "{\"ok\":true}");
  pushSnapshot();
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(3000);

  connectWiFi();

  events.onConnect([](AsyncEventSourceClient* client) {
    Serial.println("[SSE] Cliente web conectado");
    String payload = buildJsonSnapshot();
    client->send(payload.c_str(), "snapshot", millis());
  });
  server.addHandler(&events);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "application/json; charset=utf-8", buildJsonSnapshot());
  });

  server.on("/api/config", HTTP_GET, handleGetConfig);

  server.on(
    "/api/push", HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    handlePushBody);

  server.on(
    "/api/config", HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    handleSaveConfigBody);

  server.on(
    "/api/time", HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    handleSetTimeBody);

  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "404");
  });

  server.begin();

  Serial.println("[WEB] Maestro iniciado");
  Serial.println("[WEB] Abrir:");
  Serial.println("http://192.168.0.33/");
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  ensureWiFiConnected();

  if (millis() - lastBroadcastMs >= 1000) {
    lastBroadcastMs = millis();
    pushSnapshot();
  }

  if (millis() - lastStatusPrintMs >= 10000) {
    lastStatusPrintMs = millis();

    int onlineCount = 0;
    for (const auto& kv : nodes) {
      if (isNodeOnline(kv.second)) onlineCount++;
    }

    Serial.printf("[STATUS] nodes=%u online=%d heap=%u rssi=%d wifi=%s\n",
                  (unsigned)nodes.size(),
                  onlineCount,
                  (unsigned)ESP.getFreeHeap(),
                  WiFi.RSSI(),
                  WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
  }
}