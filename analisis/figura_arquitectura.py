"""Diagrama de flujo de la arquitectura con el desglose por nodo (punto 12 de la
revisión): tres nodos con la cadena PT100 -> MAX31865 -> ESP32-C6 -> LoRa,
convergiendo al gateway y de ahí a la visualización local y al dashboard web.

Salida: figuras/arquitectura-detalle.{pdf,png}
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

BASE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(BASE, "..", "figuras"))
plt.rcParams.update({"font.size": 8.2, "savefig.dpi": 200, "savefig.bbox": "tight"})

fig, ax = plt.subplots(figsize=(7.4, 3.1))
ax.set_xlim(0, 100); ax.set_ylim(0, 100); ax.axis("off")

VERDE, AZUL, NARANJA = "#e8f5e9", "#e3f2fd", "#fff3e0"
BORDE_V, BORDE_A, BORDE_N = "#43a047", "#1e88e5", "#fb8c00"


def caja(x, y, w, h, texto, fondo, borde, negrita_1a=True, fs=8.2):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.6,rounding_size=2",
                                linewidth=1.1, edgecolor=borde, facecolor=fondo))
    lineas = texto.split("\n")
    cy = y + h / 2 + (len(lineas) - 1) * 0.5 * (fs * 0.14 * 100 / 100)
    # centra verticalmente el bloque de texto
    total = len(lineas)
    for i, ln in enumerate(lineas):
        yy = y + h - (h / (total + 1)) * (i + 1)
        weight = "bold" if (i == 0 and negrita_1a) else "normal"
        ax.text(x + w / 2, yy, ln, ha="center", va="center", fontsize=fs, fontweight=weight)


def flecha(x0, y0, x1, y1, texto=None, color="#333333"):
    ax.add_patch(FancyArrowPatch((x0, y0), (x1, y1), arrowstyle="-|>", mutation_scale=11,
                                 linewidth=1.2, color=color, shrinkA=0, shrinkB=0))
    if texto:
        ax.text((x0 + x1) / 2, (y0 + y1) / 2 + 3, texto, ha="center", va="bottom",
                fontsize=7, color=color)


# --- Tres nodos (izquierda) ---
ys = [66, 38, 10]
for i, y in enumerate(ys, start=1):
    caja(1, y, 41, 22,
         f"Nodo {i}\nPT100 → MAX31865 → ESP32-C6\nBatería LiPo · LoRa E220",
         VERDE, BORDE_V, fs=7.6)

# --- Gateway (centro) ---
caja(50, 34, 22, 30,
     "Gateway /\nvisualizador\nESP32-C6 + E220\nPantalla táctil\nmicroSD / LittleFS",
     NARANJA, BORDE_N, fs=7.8)

# Flechas nodos -> gateway (LoRa)
for k, y in enumerate(ys):
    flecha(42, y + 11, 50, 49, "LoRa 433 MHz" if k == 0 else None, color="#6d4c41")

# --- Salidas (derecha) ---
caja(78, 56, 21, 20, "Pantalla local\n(OLED/LCD táctil)\nlectura en sitio", AZUL, BORDE_A, fs=7.4)
caja(78, 20, 21, 26, "Dashboard web\n+ histórico\nWi-Fi local\n24 h · alarmas · offset",
     AZUL, BORDE_A, fs=7.4)

flecha(72, 52, 78, 64, color="#1e88e5")
flecha(72, 46, 78, 33, "Datos", color="#1e88e5")

ax.text(50, 96, "Arquitectura implementada — cadena de señal por nodo y flujo de datos",
        ha="center", va="center", fontsize=9, fontweight="bold")

for ext in ("pdf", "png"):
    fig.savefig(os.path.join(OUT, f"arquitectura-detalle.{ext}"))
print("OK arquitectura-detalle")
