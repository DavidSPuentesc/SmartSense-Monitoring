"""Comparacion de campo SmartSense (nodo A) frente al Fluke 62 MAX con parejas verificables.

Cada pareja es una foto del Fluke cuya fecha y hora se tomaron de la galeria del telefono
(las copias de figuras/adjuntos_mediciones/ perdieron esos metadatos al pasar por WhatsApp).
La lectura del sensor en el instante de la foto se interpola linealmente entre las dos muestras
del CSV exportado que la rodean (cadencia ~121 s).

Salidas: analisis/parejas_fluke.csv y un resumen con media, media absoluta, RMSE y maximo,
separando las parejas en regimen estable de las tomadas durante un transitorio de arranque.
Uso: python parejas_fluke.py [ruta_csv]
"""
import csv
import math
import os
import sys
from datetime import datetime

sys.stdout.reconfigure(encoding="utf-8")
BASE = os.path.dirname(os.path.abspath(__file__))
CSV = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.expanduser("~"), "Downloads", "historial_sensores_2026-08-05.csv")
NODO_A = "58:E6:C5:13:A3:C0"
VENTANA_S = 600      # s a cada lado para medir que tan rapido cambiaba la superficie

# (foto, fecha y hora de la galeria, lectura del Fluke en la foto)
FOTOS = [
    ("1000089986.jpg", "2026-05-10 22:54:00", 37.4),
    ("1000089991.jpg", "2026-05-12 14:17:15", 51.7),
    ("1000089954.jpg / 1000089981.jpg", "2026-05-25 17:09:02", 67.7),
    ("1000089954.jpg / 1000089981.jpg", "2026-05-25 17:15:18", 67.7),
    ("1000089984.jpg", "2026-05-25 19:20:49", 65.3),
]

serie = []
with open(CSV, encoding="utf-8-sig", newline="") as fh:
    for r in csv.DictReader(fh):
        if r["mac_sensor"] == NODO_A:
            serie.append((datetime.strptime(r["fecha"] + " " + r["hora"], "%Y-%m-%d %H:%M:%S"), float(r["temperatura_c"])))
serie.sort()


def sensor_en(t):
    """Interpolacion lineal entre las muestras que rodean t, y tasa de cambio en +-10 min (C/min)."""
    for (t0, v0), (t1, v1) in zip(serie, serie[1:]):
        if t0 <= t <= t1:
            assert (t1 - t0).total_seconds() < 400, f"hueco en el registro alrededor de {t}"
            f = (t - t0).total_seconds() / (t1 - t0).total_seconds()
            vent = [(u, w) for u, w in serie if abs((u - t).total_seconds()) <= VENTANA_S]
            tasa = (vent[-1][1] - vent[0][1]) / ((vent[-1][0] - vent[0][0]).total_seconds() / 60)
            return v0 + f * (v1 - v0), tasa
    raise ValueError(f"{t} fuera del registro")


filas = []
for foto, hora, fluke in FOTOS:
    t = datetime.fromisoformat(hora)
    ss, tasa = sensor_en(t)
    filas.append({"foto": foto, "fecha_hora": hora, "fluke_c": fluke, "smartsense_c": round(ss, 2),
                  "diferencia_c": round(ss - fluke, 2), "tasa_c_por_min": round(tasa, 3)})

with open(os.path.join(BASE, "parejas_fluke.csv"), "w", encoding="utf-8", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=list(filas[0]))
    w.writeheader()
    w.writerows(filas)

for f in filas:
    print(f"{f['fecha_hora']}  Fluke {f['fluke_c']:5.1f}  SmartSense {f['smartsense_c']:6.2f}  dif {f['diferencia_c']:+6.2f}  tasa {f['tasa_c_por_min']:+.3f} C/min")


def resumen(nombre, difs):
    n = len(difs)
    media = sum(difs) / n
    mae = sum(abs(d) for d in difs) / n
    rmse = math.sqrt(sum(d * d for d in difs) / n)
    mx = max(abs(d) for d in difs)
    print(f"{nombre} (n={n}): media {media:+.2f} C | media absoluta {mae:.2f} C | RMSE {rmse:.2f} C | maximo {mx:.2f} C")
    return mae, mx


print()
todas = [f["diferencia_c"] for f in filas]
resumen("Las cinco parejas", todas)
# 10 de mayo: dia de parada; a las 22:54 el motor arrancaba tras horas detenido (sonda a 21-42 C ese dia)
sin_arranque = [f["diferencia_c"] for f in filas if not f["fecha_hora"].startswith("2026-05-10")]
mae, mx = resumen("Sin la pareja del arranque del 10 de mayo", sin_arranque)

# comprobaciones
assert len(filas) == 5 and len(sin_arranque) == 4
assert mx < 0.5 and mae < 0.2
