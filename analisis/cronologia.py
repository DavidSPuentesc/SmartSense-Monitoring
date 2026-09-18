"""Cronologia del piloto a partir del CSV exportado del gateway.

Detecta, para el nodo del succionador (Twin):
  - dias con la maquina detenida (media diaria < 30 C, sonda cerca del ambiente),
  - intervalos sin registro simultaneos en los tres nodos (gateway sin registrar),
  - el maximo historico,
y los combina con los ensayos posteriores (descarga parcial y alcance).

Salidas: analisis/cronologia.csv (eventos) y figuras/cronologia.{pdf,png}.
Uso: python cronologia.py [ruta_csv]
"""
import csv
import os
import sys
sys.stdout.reconfigure(encoding="utf-8")
from collections import defaultdict
from datetime import datetime, date, timedelta

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

BASE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(BASE, "..", "figuras"))
CSV = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.expanduser("~"), "Downloads", "historial_sensores_2026-08-05.csv")
plt.rcParams.update({"font.size": 9, "figure.dpi": 150, "savefig.bbox": "tight"})
T_PARADA = 30.0      # C: media diaria por debajo -> maquina detenida
GAP_H = 6.0          # h: hueco largo
ANON = {"58:E6:C5:13:A3:C0": "Nodo A", "58:E6:C5:13:A9:20": "Nodo B", "58:E6:C5:19:28:20": "Nodo C"}
# paradas en las que, segun el equipo de planta, se intervino el rodamiento del succionador
MANTENIMIENTO = {(date(2026, 5, 30), date(2026, 5, 31)), (date(2026, 6, 7), date(2026, 6, 8))}


def cargar(ruta):
    series = defaultdict(list)
    with open(ruta, encoding="utf-8-sig", newline="") as fh:
        for r in csv.DictReader(fh):
            try:
                dt = datetime.strptime(r["fecha"] + " " + r["hora"], "%Y-%m-%d %H:%M:%S")
                series[r["mac_sensor"]].append((dt, float(r["temperatura_c"])))
            except (ValueError, KeyError):
                pass
    for mac in series:
        series[mac].sort()
    return series


def huecos(s, horas):
    return [(a, b) for (a, _), (b, _) in zip(s, s[1:]) if (b - a).total_seconds() / 3600 > horas]


def rachas(dias):
    """Agrupa fechas consecutivas en (inicio, fin)."""
    out, ini, prev = [], None, None
    for d in sorted(dias):
        if ini is None:
            ini = prev = d
        elif d == prev + timedelta(days=1):
            prev = d
        else:
            out.append((ini, prev)); ini = prev = d
    if ini is not None:
        out.append((ini, prev))
    return out


series = cargar(CSV)
A = "58:E6:C5:13:A3:C0"
sA = series[A]
fin_piloto = max(t for t, _ in sA)
por_dia = defaultdict(list)
for t, v in sA:
    por_dia[t.date()].append(v)
media = {d: sum(v) / len(v) for d, v in por_dia.items()}
maximo = {d: max(v) for d, v in por_dia.items()}
paradas = rachas([d for d, m in media.items() if m < T_PARADA])
pico = max(sA, key=lambda x: x[1])

# huecos simultaneos: intervalos largos del nodo A que solapan con huecos largos de B y C
hA = huecos(sA, GAP_H)
otros = [huecos(series[m], GAP_H) for m in series if m != A]
simultaneos = []
for a, b in hA:
    if b > fin_piloto:
        continue
    if all(any(x < b and y > a for x, y in h) for h in otros):
        simultaneos.append((a, b))

eventos = [(min(t for t, _ in sA).date(), "Instalación de los nodos A y C; inicio del registro", "CSV"),
           (min(t for t, _ in series["58:E6:C5:13:A9:20"]).date(), "Inicio del registro del nodo B", "CSV")]
for a, b in simultaneos:
    eventos.append((a.date(), f"Gateway sin registrar del {a:%d/%m %H:%M} al {b:%d/%m %H:%M} ({(b-a).total_seconds()/3600:.0f} h), simultáneo en los tres nodos", "CSV"))
for ini, fin in paradas:
    rango = f"{ini:%d/%m}" if ini == fin else f"{ini:%d/%m} al {fin:%d/%m}"
    extra = "; intervención del rodamiento (equipo de planta)" if (ini, fin) in MANTENIMIENTO else ""
    eventos.append((ini, f"Succionador detenido ({rango}); sonda entre {min(min(por_dia[d]) for d in por_dia if ini <= d <= fin):.0f} y {max(max(por_dia[d]) for d in por_dia if ini <= d <= fin):.0f} °C{extra}", "CSV"))
eventos.append((pico[0].date(), f"Máximo histórico del succionador: {pico[1]:.1f} °C a las {pico[0]:%H:%M}", "CSV"))
eventos.append((fin_piloto.date(), "Fin del registro de los nodos A y C", "CSV"))
eventos += [(date(2026, 8, 13), "Inicio de la descarga parcial (celda de 1000 mAh, 4.112 V)", "Ensayo"),
            (date(2026, 8, 18), "Fin de la descarga parcial (4.089 V tras 120 h)", "Ensayo"),
            (date(2026, 8, 20), "Prueba de alcance urbana: 688 m con RSSI de −98 dBm", "Ensayo")]
eventos.sort()

with open(os.path.join(BASE, "cronologia.csv"), "w", encoding="utf-8", newline="") as fh:
    w = csv.writer(fh); w.writerow(["fecha", "evento", "fuente"]); w.writerows(eventos)
for d, e, f in eventos:
    print(f"{d}  [{f}]  {e}")

# comprobaciones
assert pico[1] == 70.0 and pico[0].date() == date(2026, 5, 23)
assert any(ini <= date(2026, 5, 19) <= fin for ini, fin in paradas), "la parada de mayo debe detectarse"
assert len(simultaneos) >= 3

# ---------- figura ----------
fig, (a1, a2) = plt.subplots(2, 1, figsize=(7.4, 4.6), gridspec_kw={"height_ratios": [3, 1.1]}, sharex=False)
dias = sorted(por_dia)
a1.bar(dias, [maximo[d] for d in dias], width=0.8, color="#cbd5e1", label="Máximo diario")
a1.plot(dias, [media[d] for d in dias], color="#1e40af", lw=1.4, marker="o", ms=2.5, label="Media diaria")
for ini, fin in paradas:
    mant = (ini, fin) in MANTENIMIENTO
    a1.axvspan(ini - timedelta(hours=12), fin + timedelta(hours=12), color="#fca5a5" if mant else "#fde68a", alpha=0.55 if mant else 0.5, lw=0)
for a, b in simultaneos:
    a1.axvspan(a, b, color="#94a3b8", alpha=0.35, hatch="//", lw=0)
a1.plot([pico[0].date()], [pico[1]], "v", color="#b91c1c", ms=8, zorder=5)
a1.annotate(f"máximo {pico[1]:.1f} °C\n{pico[0]:%d/%m %H:%M}", xy=(pico[0].date(), pico[1]), xytext=(pico[0].date() - timedelta(days=9), 84),
            fontsize=7.5, color="#b91c1c", arrowprops=dict(arrowstyle="->", lw=0.7, color="#b91c1c"))
a1.axhline(T_PARADA, color="#f59e0b", lw=0.8, ls=":")
a1.text(dias[0], T_PARADA + 1, "umbral de parada (media diaria < 30 °C)", fontsize=7, color="#b45309")
a1.set_ylabel("Temperatura del succionador (°C)")
a1.set_ylim(0, 96)
a1.set_title("Nodo A (succionador de la Twin): días de operación, paradas y periodos sin registro", fontsize=9)
a1.xaxis.set_major_formatter(mdates.DateFormatter("%d/%m"))
a1.legend(loc="upper right", fontsize=7.5, ncol=2, framealpha=0.95)
a1.grid(True, lw=0.3, alpha=0.5)
a1.text(0.01, 0.97, "amarillo: máquina detenida · rojo: parada con intervención del rodamiento · rayado gris: gateway sin registrar", transform=a1.transAxes, fontsize=7, va="top", color="#334155")

# linea de tiempo completa (mayo a agosto)
hitos = [(date(2026, 5, 8), "instalación"), (pico[0].date(), "máximo 70 °C"), (date(2026, 6, 9), "fin A y C"),
         (date(2026, 7, 20), "fin B"), (date(2026, 8, 13), "descarga\n13–18/08"), (date(2026, 8, 20), "alcance\n20/08")]
a2.hlines(0, date(2026, 5, 5), date(2026, 8, 23), color="#94a3b8", lw=1.5)
for i, (d, txt) in enumerate(hitos):
    a2.plot([d], [0], "o", color="#1e293b", ms=5)
    a2.text(d, 0.25 if i % 2 == 0 else -0.25, txt, ha="center", va="bottom" if i % 2 == 0 else "top", fontsize=7.5)
a2.set_ylim(-1, 1)
a2.set_yticks([])
a2.xaxis.set_major_formatter(mdates.DateFormatter("%d/%m"))
a2.set_xlim(date(2026, 5, 5), date(2026, 8, 23))
a2.set_title("Hitos del proyecto, mayo a agosto de 2026", fontsize=9)
for sp in ("left", "right", "top"):
    a2.spines[sp].set_visible(False)
fig.tight_layout()
for ext in ("pdf", "png"):
    fig.savefig(os.path.join(OUT, f"cronologia.{ext}"))
print("OK cronologia")
