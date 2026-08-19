"""Ventana representativa de 60 minutos del despliegue real, como la 'prueba de
60 minutos' definida en el anteproyecto.

Busca una hora continua con los tres nodos activos, grafica temperatura vs
tiempo y calcula por nodo: esperados (60 min / 120 s), recibidos, perdidos, PDR.

Salida: figuras/prueba-60min.{pdf,png}
"""
import csv
import os
from collections import defaultdict
from datetime import datetime, timedelta

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
CICLO = 120.0

serie = defaultdict(list)
with open(CSV, encoding="utf-8-sig", newline="") as fh:
    for r in csv.DictReader(fh):
        try:
            dt = datetime.strptime(r["fecha"] + " " + r["hora"], "%Y-%m-%d %H:%M:%S")
            serie[ORDEN[r["mac_sensor"]]].append((dt, float(r["temperatura_c"])))
        except (KeyError, ValueError):
            pass
for k in serie:
    serie[k].sort()

# Buscar una ventana de 60 min con los tres nodos bien cubiertos
nodos = ["Node A", "Node B", "Node C"]
ini_comun = max(serie[k][0][0] for k in nodos)
fin_comun = min(serie[k][-1][0] for k in nodos)
mejor, mejor_cnt = None, -1
t = ini_comun
while t < fin_comun - timedelta(hours=1):
    fin = t + timedelta(hours=1)
    conteo = {k: sum(1 for d, _ in serie[k] if t <= d < fin) for k in nodos}
    total = sum(conteo.values())
    if all(c >= 25 for c in conteo.values()) and total > mejor_cnt:
        mejor, mejor_cnt = (t, fin, conteo), total
        break  # la primera ventana buena basta como ejemplo
    t += timedelta(minutes=30)

t0, t1, conteo = mejor
print(f"Ventana: {t0} a {t1}")
esperados = round((t1 - t0).total_seconds() / CICLO)
print(f"Esperados por nodo: {esperados}")
for k in nodos:
    rec = conteo[k]
    perd = max(0, esperados - rec)
    pdr = 100.0 * rec / esperados
    print(f"  {k}: recibidos={rec} perdidos={perd} PDR={pdr:.1f}%")

# Grafica temperatura vs tiempo en la ventana
fig, ax = plt.subplots(figsize=(6.6, 3.2))
for k in nodos:
    pts = [(d, v) for d, v in serie[k] if t0 <= d < t1]
    ax.plot([d for d, _ in pts], [v for _, v in pts], "-o", ms=3, lw=1.0,
            color=COLOR[k], label=k)
ax.set_xlabel("Time (HH:MM)")
ax.set_ylabel("Temperature (°C)")
ax.set_title(f"Representative 60-minute test ({t0:%Y-%m-%d %H:%M})")
ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
ax.grid(True, lw=0.3, alpha=0.5)
ax.legend(loc="best", fontsize=8, ncol=3)
for ext in ("pdf", "png"):
    fig.savefig(os.path.join(OUT, f"prueba-60min.{ext}"))
plt.close(fig)
print("OK prueba-60min")
