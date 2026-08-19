"""Graficas adicionales de los tres sensores a partir del CSV real.

Salida en figuras/:
  comparacion-sensores.{pdf,png}   series de los 3 nodos en la ventana comun
  distribucion-temperatura.{pdf,png} distribucion (boxplot) por nodo
"""
import csv
import os
from collections import defaultdict
from datetime import datetime

import matplotlib
matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

BASE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(BASE, "..", "figuras"))
CSV = os.path.join(os.path.expanduser("~"), "Downloads", "historial_sensores_2026-08-05.csv")
plt.rcParams.update({"font.size": 9, "figure.dpi": 150, "savefig.bbox": "tight"})

ORDEN = {"58:E6:C5:13:A3:C0": "Node A", "58:E6:C5:13:A9:20": "Node B", "58:E6:C5:19:28:20": "Node C"}
COLOR = {"Node A": "#b91c1c", "Node B": "#1e88e5", "Node C": "#16a34a"}

serie = defaultdict(list)   # etiqueta -> [(dt, temp)]
with open(CSV, encoding="utf-8-sig", newline="") as fh:
    for r in csv.DictReader(fh):
        try:
            dt = datetime.strptime(r["fecha"] + " " + r["hora"], "%Y-%m-%d %H:%M:%S")
            serie[ORDEN[r["mac_sensor"]]].append((dt, float(r["temperatura_c"])))
        except (KeyError, ValueError):
            pass
for k in serie:
    serie[k].sort()

# Ventana comun a los tres (para comparar de forma justa)
ini = max(min(d for d, _ in serie[k]) for k in serie)
fin = min(max(d for d, _ in serie[k]) for k in serie)
print(f"Ventana comun: {ini} a {fin}")

# ---------- Figura 1: series superpuestas ----------
fig, ax = plt.subplots(figsize=(7.0, 3.0))
for k in ["Node A", "Node B", "Node C"]:
    pts = [(d, t) for d, t in serie[k] if ini <= d <= fin]
    paso = max(1, len(pts) // 1500)
    ax.plot([d for d, _ in pts[::paso]], [t for _, t in pts[::paso]],
            lw=0.5, color=COLOR[k], label=k, alpha=0.8)
ax.set_ylabel("Temperature (°C)")
ax.set_xlabel("Date")
ax.set_title("Surface temperature of the three nodes (common window)")
ax.xaxis.set_major_formatter(mdates.DateFormatter("%b %d"))
ax.grid(True, lw=0.3, alpha=0.5)
ax.legend(loc="upper right", fontsize=8, ncol=3)
for ext in ("pdf", "png"):
    fig.savefig(os.path.join(OUT, f"comparacion-sensores.{ext}"))
plt.close(fig)
print("OK comparacion-sensores")

# ---------- Figura 2: distribucion por nodo (boxplot) ----------
fig, ax = plt.subplots(figsize=(4.6, 3.0))
datos = [[t for _, t in serie[k]] for k in ["Node A", "Node B", "Node C"]]
bp = ax.boxplot(datos, tick_labels=["Node A", "Node B", "Node C"], showfliers=False,
                patch_artist=True, medianprops=dict(color="black"))
for parche, k in zip(bp["boxes"], ["Node A", "Node B", "Node C"]):
    parche.set_facecolor(COLOR[k]); parche.set_alpha(0.45)
ax.set_ylabel("Temperature (°C)")
ax.set_title("Temperature distribution per node")
ax.grid(True, axis="y", lw=0.3, alpha=0.5)
for ext in ("pdf", "png"):
    fig.savefig(os.path.join(OUT, f"distribucion-temperatura.{ext}"))
plt.close(fig)
print("OK distribucion-temperatura")
