"""Estima la perdida de paquetes a partir de los timestamps del CSV historico.

Metodo (sin contador de secuencia en los datos historicos): dentro de tramos
activos, si entre dos muestras consecutivas el intervalo es aproximadamente k
veces el ciclo de muestreo, se infiere que faltaron (k-1) paquetes. Los huecos
muy grandes (gateway apagado) se excluyen porque no son fallas del enlace.

Entrega: perdida agregada por nodo (solo tramos activos) y una ventana de 1 h
representativa por nodo, como evidencia puntual.
"""
import csv
import sys
from collections import defaultdict
from datetime import datetime, timedelta

RUTA = sys.argv[1] if len(sys.argv) > 1 else \
    r"C:\Users\santy\Downloads\historial_sensores_2026-08-05.csv"

CICLO = 120.0          # ciclo de muestreo nominal (s); la mediana medida fue 121 s
GAP_ACTIVO = 1800.0    # intervalos mayores a 30 min = gateway/nodo apagado -> excluir


def cargar(ruta):
    series, nombres = defaultdict(list), {}
    with open(ruta, encoding="utf-8-sig", newline="") as fh:
        for r in csv.DictReader(fh):
            try:
                dt = datetime.strptime(r["fecha"] + " " + r["hora"], "%Y-%m-%d %H:%M:%S")
                series[r["mac_sensor"]].append(dt)
                nombres[r["mac_sensor"]] = r.get("nombre_sensor", "")
            except (ValueError, KeyError):
                pass
    for m in series:
        series[m].sort()
    return series, nombres


def perdida_en(ts):
    """Sobre una lista ordenada de marcas de tiempo, cuenta recibidos y perdidos
    en tramos activos. Devuelve (recibidos, perdidos, huecos_grandes)."""
    recibidos = len(ts)
    perdidos = 0
    huecos_grandes = 0
    for a, b in zip(ts, ts[1:]):
        s = (b - a).total_seconds()
        if s > GAP_ACTIVO:
            huecos_grandes += 1
            continue
        k = round(s / CICLO)
        if k >= 2:
            perdidos += (k - 1)
    return recibidos, perdidos, huecos_grandes


def mejor_ventana_1h(ts):
    """Busca la ventana continua de ~1 h con mas paquetes (sin hueco grande),
    como ejemplo representativo. Devuelve (inicio, lista_ts_ventana)."""
    mejor = None
    i = 0
    n = len(ts)
    while i < n:
        j = i
        while j + 1 < n and (ts[j + 1] - ts[j]).total_seconds() <= GAP_ACTIVO \
                and (ts[j + 1] - ts[i]) <= timedelta(hours=1):
            j += 1
        ventana = ts[i:j + 1]
        if mejor is None or len(ventana) > len(mejor):
            mejor = ventana
        # avanzar al siguiente inicio no cubierto
        i = j + 1 if j > i else i + 1
    return mejor


def main():
    series, nombres = cargar(RUTA)
    macs = sorted(series)
    print(f"Ciclo nominal = {CICLO:.0f} s | umbral gateway-apagado = {GAP_ACTIVO:.0f} s\n")

    print("=== Perdida agregada por nodo (solo tramos activos) ===")
    for i, mac in enumerate(macs):
        rec, perd, hg = perdida_en(series[mac])
        esperados = rec + perd
        loss = 100.0 * perd / esperados if esperados else 0.0
        pdr = 100.0 - loss
        print(f"Nodo {chr(65+i)} ({nombres[mac] or '-'}): recibidos={rec} perdidos_inferidos={perd} "
              f"esperados={esperados} -> perdida={loss:.2f}% (PDR~{pdr:.2f}%), {hg} huecos de gateway")

    print("\n=== Ventana de 1 hora representativa por nodo (evidencia puntual) ===")
    for i, mac in enumerate(macs):
        v = mejor_ventana_1h(series[mac])
        if not v or len(v) < 2:
            print(f"Nodo {chr(65+i)}: sin ventana continua de 1 h")
            continue
        rec, perd, _ = perdida_en(v)
        dur_min = (v[-1] - v[0]).total_seconds() / 60.0
        esperados = rec + perd
        loss = 100.0 * perd / esperados if esperados else 0.0
        print(f"Nodo {chr(65+i)}: {v[0]} a {v[-1]} ({dur_min:.0f} min) | "
              f"recibidos={rec} perdidos={perd} esperados={esperados} -> perdida={loss:.2f}%")


if __name__ == "__main__":
    main()
