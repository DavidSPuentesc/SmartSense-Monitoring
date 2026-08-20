"""Analiza el log del monitor serie de una prueba (lineas CSV que imprime el
gateway) y saca, por nodo: PDR/perdida, RSSI, temperatura y bateria.

Formato de cada linea CSV que emite el gateway (una por paquete):
  CSV,epoch,mac,id,seq,temp_c,vbat,bat_pct,rssi_dbm,rssi_raw,up_ms
  - rssi_raw = -1  -> RSSI no disponible en esa lectura

Uso:  python analizar_prueba.py <log_del_monitor_serie.txt>
Sirve para esta prueba y para todas las futuras (mismo formato).
"""
import sys
import statistics as st
from collections import defaultdict

if len(sys.argv) < 2:
    print("Uso: python analizar_prueba.py <log.txt>")
    sys.exit(1)

filas = defaultdict(list)   # mac -> list of dicts
with open(sys.argv[1], encoding="utf-8", errors="replace") as fh:
    for ln in fh:
        ln = ln.strip()
        if not ln.startswith("CSV,"):
            continue
        p = ln.split(",")
        if len(p) < 11:
            continue
        try:
            filas[p[2]].append({
                "epoch": int(p[1]), "id": p[3], "seq": int(p[4]),
                "temp": float(p[5]), "vbat": float(p[6]), "bat": float(p[7]),
                "rssi_dbm": int(p[8]), "rssi_raw": int(p[9]), "up": int(p[10]),
            })
        except ValueError:
            continue

if not filas:
    print("No se encontraron lineas CSV en el log. ¿Grabaste el monitor serie del gateway?")
    sys.exit(1)

print(f"Nodos: {len(filas)}\n")
for mac in sorted(filas):
    r = sorted(filas[mac], key=lambda x: x["seq"])
    seqs = [x["seq"] for x in r]
    recibidos = len(set(seqs))
    esperados = max(seqs) - min(seqs) + 1 if seqs else 0
    perdidos = max(0, esperados - recibidos)
    pdr = 100.0 * recibidos / esperados if esperados else 0.0

    temps = [x["temp"] for x in r]
    vbats = [x["vbat"] for x in r if x["vbat"] == x["vbat"]]  # descarta NaN
    rssis = [x["rssi_dbm"] for x in r if x["rssi_raw"] >= 0]

    idn = r[0]["id"]
    print(f"=== MAC {mac} (ID {idn}) ===")
    print(f"  Paquetes: recibidos={recibidos} esperados={esperados} perdidos={perdidos} "
          f"-> PDR={pdr:.1f}% / perdida={100-pdr:.1f}%")
    print(f"  Temp (C): min={min(temps):.1f} max={max(temps):.1f} prom={st.mean(temps):.1f}")
    if vbats:
        print(f"  Bateria (V): inicio={vbats[0]:.3f} fin={vbats[-1]:.3f} caida={vbats[0]-vbats[-1]:.3f}")
    if rssis:
        print(f"  RSSI (dBm): min={min(rssis)} max={max(rssis)} prom={st.mean(rssis):.0f} (n={len(rssis)})")
    else:
        print("  RSSI: sin datos (no llego el byte de RSSI; revisar TRY_RSSI/AT+RSSI)")
    ups = [x["up"] for x in r if x["up"] > 0]
    if ups:
        print(f"  Tiempo encendido (ms): prom={st.mean(ups):.0f}")
    print()

# Resumen global
tot_rec = sum(len(set(x['seq'] for x in v)) for v in filas.values())
print(f"Total paquetes recibidos (unicos): {tot_rec}")
