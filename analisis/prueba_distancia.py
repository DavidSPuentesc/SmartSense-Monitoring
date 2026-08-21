"""Prueba de alcance al aire libre (2026-08-20): RSSI y bateria vs tiempo.

El nodo se alejo del gateway (>600 m) y regreso; hacia las 20:44-20:54 se
mantuvo en el punto mas lejano. Salida: figuras/prueba-distancia.{pdf,png}
"""
import csv
import os
from datetime import datetime

import matplotlib
matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

BASE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(BASE, "..", "figuras"))
CSV = os.path.join(os.path.expanduser("~"), "Downloads", "attachments",
                   "historial_sensores_2026-08-20(2).csv")
plt.rcParams.update({"font.size": 9, "figure.dpi": 150, "savefig.bbox": "tight"})

t, rssi, bat = [], [], []
with open(CSV, encoding="utf-8-sig", newline="") as fh:
    for r in csv.DictReader(fh):
        try:
            t.append(datetime.strptime(r["fecha"] + " " + r["hora"], "%Y-%m-%d %H:%M:%S"))
            rssi.append(int(float(r["rssi_dbm"])))
            bat.append(float(r["bateria_porcentaje"]))
        except (ValueError, KeyError):
            pass

print(f"n={len(t)} | RSSI {min(rssi)}..{max(rssi)} dBm | bat {min(bat):.1f}-{max(bat):.1f}%")

fig, ax = plt.subplots(figsize=(7.0, 3.2))
ax.plot(t, rssi, "-o", ms=2.5, lw=0.8, color="#1e88e5", label="RSSI")
ax.set_ylabel("RSSI (dBm)", color="#1e88e5")
ax.tick_params(axis="y", labelcolor="#1e88e5")
ax.set_xlabel("Time (HH:MM)")
ax.set_title("Outdoor range test: RSSI vs time (node moving away and back)")
ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
ax.grid(True, lw=0.3, alpha=0.5)

# marca la ventana lejana (~600 m)
far0 = datetime(2026, 8, 20, 20, 44)
far1 = datetime(2026, 8, 20, 20, 55)
ax.axvspan(far0, far1, color="#b91c1c", alpha=0.12)
ax.annotate("farthest point (>600 m)\nRSSI $\\approx$ -98 dBm",
            xy=(datetime(2026, 8, 20, 20, 49), -98), xytext=(datetime(2026, 8, 20, 21, 5), -70),
            fontsize=8, color="#b91c1c",
            arrowprops=dict(arrowstyle="->", color="#b91c1c", lw=0.8))

# bateria en eje secundario
ax2 = ax.twinx()
ax2.plot(t, bat, lw=0.8, color="#16a34a", alpha=0.6, label="Battery")
ax2.set_ylabel("Battery (%)", color="#16a34a")
ax2.tick_params(axis="y", labelcolor="#16a34a")
ax2.set_ylim(0, 100)

for ext in ("pdf", "png"):
    fig.savefig(os.path.join(OUT, f"prueba-distancia.{ext}"))
print("OK prueba-distancia")
