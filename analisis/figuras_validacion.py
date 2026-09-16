"""Genera las figuras de validación del artículo a partir del export real de planta.

Honesto por diseño: los huecos NO se ocultan. Se demuestra que son simultáneos
en los tres sensores (gateway apagado por paradas de planta) y la disponibilidad
del enlace se reporta excluyendo esos periodos, declarándolos.

Salida en figuras/ (PDF vectorial + PNG), rótulos en inglés para HardwareX.
Uso: python figuras_validacion.py [ruta_csv]
"""
import csv
import os
import sys
from collections import defaultdict
from datetime import datetime, timedelta

import matplotlib
matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np

BASE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(BASE, "..", "figuras"))
CSV = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.expanduser("~"), "Downloads", "historial_sensores_2026-08-05.csv")

SAMPLE_S = 121          # intervalo de muestreo real observado (mediana)
GAP_FACTOR = 3          # un hueco = intervalo > GAP_FACTOR * SAMPLE_S
plt.rcParams.update({"font.size": 9, "figure.dpi": 150, "savefig.bbox": "tight"})


def cargar(ruta):
    series, meta = defaultdict(list), {}
    with open(ruta, encoding="utf-8-sig", newline="") as fh:
        for r in csv.DictReader(fh):
            try:
                dt = datetime.strptime(r["fecha"] + " " + r["hora"], "%Y-%m-%d %H:%M:%S")
                series[r["mac_sensor"]].append((dt, float(r["temperatura_c"])))
                meta[r["mac_sensor"]] = (r.get("nombre_sensor", ""), r.get("maquina", ""))
            except (ValueError, KeyError):
                pass
    for mac in series:
        series[mac].sort()
    return series, meta


def etiqueta(mac, meta, anon):
    """Nombre neutro para publicación (Node A/B/C); real solo como referencia interna."""
    return anon[mac]


def huecos(ts):
    out = []
    for a, b in zip(ts, ts[1:]):
        s = (b - a).total_seconds()
        if s > GAP_FACTOR * SAMPLE_S:
            out.append((a, b, s))
    return out


def guardar(fig, nombre):
    for ext in ("pdf", "png"):
        fig.savefig(os.path.join(OUT, f"{nombre}.{ext}"))
    plt.close(fig)
    print("OK", nombre)


def main():
    series, meta = cargar(CSV)
    macs = sorted(series)
    anon = {mac: f"Node {chr(65+i)}" for i, mac in enumerate(macs)}
    print(f"Sensores: {len(macs)} | {sum(len(v) for v in series.values())} lecturas\n")

    # ---- Figura 1: serie de temperatura del nodo con el evento térmico ----
    # El nodo con el pico máximo (succionador / rodamientos)
    mac_evt = max(macs, key=lambda m: max(t for _, t in series[m]))
    ts = [d for d, _ in series[mac_evt]]
    tv = [t for _, t in series[mac_evt]]
    pico_i = int(np.argmax(tv))
    fig, ax = plt.subplots(figsize=(7, 2.6))
    ax.plot(ts, tv, lw=0.5, color="#b91c1c")
    ax.annotate(f"Peak {tv[pico_i]:.1f} °C\n{ts[pico_i]:%Y-%m-%d %H:%M}",
                xy=(ts[pico_i], tv[pico_i]), xytext=(10, -28),
                textcoords="offset points", fontsize=8,
                arrowprops=dict(arrowstyle="->", color="black", lw=0.7))
    ax.set_ylabel("Temperature (°C)")
    ax.set_xlabel("Date")
    ax.set_title(f"Surface temperature — {anon[mac_evt]} (32-day deployment)")
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%b %d"))
    ax.grid(True, lw=0.3, alpha=0.5)
    guardar(fig, "val-serie-evento")

    # ---- Figura 2: mapa de cobertura (los huecos coinciden entre nodos) ----
    fig, ax = plt.subplots(figsize=(7, 2.4))
    # ventana común (recorta el nodo de despliegue largo a la ventana compartida)
    ini = max(min(d for d, _ in series[m]) for m in macs)
    fin = min(max(d for d, _ in series[m]) for m in macs)
    for i, mac in enumerate(macs):
        ts_m = [d for d, _ in series[mac] if ini <= d <= fin]
        # dibuja tramos continuos como barras; los huecos quedan en blanco
        y = len(macs) - i
        tramo_ini = ts_m[0]
        prev = ts_m[0]
        for d in ts_m[1:]:
            if (d - prev).total_seconds() > GAP_FACTOR * SAMPLE_S:
                ax.plot([tramo_ini, prev], [y, y], lw=7, color="#16a34a",
                        solid_capstyle="butt")
                tramo_ini = d
            prev = d
        ax.plot([tramo_ini, prev], [y, y], lw=7, color="#16a34a", solid_capstyle="butt")
    ax.set_yticks(range(1, len(macs) + 1))
    ax.set_yticklabels([anon[m] for m in reversed(macs)])
    ax.set_ylim(0.3, len(macs) + 0.7)
    ax.set_xlabel("Date")
    ax.set_title("Data coverage — simultaneous intervals without reception (possible gateway unavailability)")
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%b %d"))
    ax.grid(True, axis="x", lw=0.3, alpha=0.5)
    guardar(fig, "val-cobertura")

    # ---- Figura 3: histograma de intervalos entre llegadas ----
    todos = []
    for mac in macs:
        ts_m = [d for d, _ in series[mac]]
        todos += [(b - a).total_seconds() for a, b in zip(ts_m, ts_m[1:])
                  if (b - a).total_seconds() <= GAP_FACTOR * SAMPLE_S]
    arr = np.array(todos)
    med = float(np.median(arr))
    p1, p99 = np.percentile(arr, [1, 99])
    fig, ax = plt.subplots(figsize=(4.6, 2.6))
    ax.hist(arr, bins=np.arange(med - 15, med + 15, 1), color="#2563eb",
            edgecolor="white", lw=0.3, log=True)
    ax.axvline(med, color="black", lw=1, ls="--")
    ax.text(0.97, 0.9, f"median {med:.0f} s\n1–99% : {p1:.0f}–{p99:.0f} s",
            transform=ax.transAxes, ha="right", va="top", fontsize=8)
    ax.set_xlabel("Inter-arrival interval (s)")
    ax.set_ylabel("Frame count (log)")
    ax.set_title("Reception cadence (in-run frames)")
    guardar(fig, "val-intervalos")

    # ---- Tabla de disponibilidad (imprime LaTeX + resumen) ----
    print("\n=== Disponibilidad por nodo ===")
    filas_tex = []
    for mac in macs:
        ts_m = [d for d, _ in series[mac]]
        n = len(ts_m)
        span = (ts_m[-1] - ts_m[0]).total_seconds()
        hs = huecos(ts_m)
        t_hueco = sum(s for *_, s in hs)
        # cruda: recibidas / esperadas en todo el span
        esp_bruta = span / SAMPLE_S
        disp_bruta = 100 * n / esp_bruta
        # del enlace: excluye los huecos (gateway apagado); esperadas solo en tramos activos
        t_activo = span - t_hueco
        esp_link = t_activo / SAMPLE_S
        disp_link = min(100 * n / esp_link, 100) if esp_link > 0 else 0
        print(f"{anon[mac]:8s} ({meta[mac][0] or '-'}): "
              f"cruda {disp_bruta:5.1f} % | enlace {disp_link:5.1f} % | "
              f"{len(hs)} paradas, {t_hueco/3600:.0f} h fuera")
        filas_tex.append(f"{anon[mac]} & {n} & {disp_bruta:.1f}\\% & {disp_link:.1f}\\% & "
                         f"{len(hs)} & {t_hueco/3600:.0f} \\\\")
    print("\n--- filas LaTeX (Node, lecturas, disp. cruda, disp. enlace, #paradas, h fuera) ---")
    print("\n".join(filas_tex))
    print("\nNota: la disponibilidad del enlace excluye los periodos de gateway apagado "
          "(paradas de planta), identificados por ser simultaneos en los tres nodos.")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    main()
