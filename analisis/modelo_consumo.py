"""Modelo de consumo por estados del nodo SmartSense a partir de fichas tecnicas,
con varianza (Monte Carlo) y contraste con la descarga parcial observada.

El prototipo se entrego a la empresa aliada, asi que la corriente del nodo no se
pudo medir. Este modelo NO sustituye esa medicion: acota la autonomia con los
valores de ficha de cada componente y los tiempos que fija el firmware, y la
compara con lo que implica la descarga observada (4.112 -> 4.089 V en 120 h).

Fuentes de las entradas (ver tabla PARAMS):
  ESP32-C6 Series Datasheet v1.5, tablas 5-10 y 5-11 (Espressif).
  XIAO ESP32C6 wiki (Seeed): 15 uA de placa en sueno profundo.
  E220-400T22D User Manual v1.0, tabla 2-2 (Ebyte).
  Nodo_Lora.ino: delays, muestras y ventana de configuracion.
  autonomia_bateria.py: puntos de la descarga y tabla OCV-SoC.

Salidas en figuras/: modelo-consumo.{pdf,png}. Imprime la tabla de resultados.
Uso: python modelo_consumo.py
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

BASE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(BASE, "..", "figuras"))
plt.rcParams.update({"font.size": 9, "figure.dpi": 150, "savefig.bbox": "tight"})
rng = np.random.default_rng(20260917)
N = 50_000

# ---------- Entradas: (nominal, minimo, maximo, unidad, fuente) ----------
# Cada parametro se muestrea con distribucion triangular entre min y max, moda = nominal.
PARAMS = {
    # ciclo y tiempos (firmware)
    "T_ciclo":        (120.0, 119.0, 122.0, "s",  "periodo nominal; mediana observada 121 s"),
    "t_pre":          (0.35, 0.25, 0.45, "s",  "arranque del ESP32 + delay(200) antes de bootMs"),
    "t_activo":       (0.75, 0.65, 0.90, "s",  "2.279 s instrumentados menos la espera CFG de 1.5 s (solo 30 ciclos tras energizar)"),
    "t_aire":         (0.35, 0.25, 0.50, "s",  "trama de ~85 bytes a 2.4 kbps mas preambulo"),
    "t_sensor":       (0.45, 0.35, 0.55, "s",  "init 80 ms + 4 conversiones + delays (readTemperature)"),
    # corrientes activas (fichas)
    "I_esp_act":      (30.0, 27.0, 38.0, "mA", "ESP32-C6 modem-sleep, CPU 160 MHz activa (tabla 5-10)"),
    "I_e220_rx":      (16.8, 15.0, 18.5, "mA", "E220-400T22D recepcion (tabla 2-2)"),
    "I_e220_tx":      (110.0, 100.0, 120.0, "mA", "E220-400T22D transmision (tabla 2-2)"),
    "I_max_chip":     (2.0, 1.0, 3.0, "mA", "MAX31865 + regulador del modulo (supuesto; ficha por confirmar)"),
    "I_max_bias":     (6.0, 5.5, 6.5, "mA", "polarizacion RTD: 3.3 V / (421.1 + ~127) ohm"),
    # corrientes de reposo (fichas)
    "I_xiao_sleep":   (0.015, 0.012, 0.020, "mA", "XIAO ESP32C6 en sueno profundo (wiki Seeed)"),
    "I_e220_sleep":   (0.005, 0.004, 0.008, "mA", "E220 modo 3: M0/M1 quedan en pull-up al dormir el ESP32"),
    "I_divisor":      (0.0031, 0.0031, 0.0031, "mA", "4.2 V / 1.339 Mohm"),
    "I_proteccion":   (0.004, 0.002, 0.008, "mA", "circuito de proteccion del modulo de carga (DW01 o similar)"),
    # bateria
    "autodescarga":   (2.0, 1.0, 3.0, "%/mes", "autodescarga tipica de una celda LiPo"),
    "C_util":         (0.95, 0.85, 1.00, "-", "fraccion utilizable de 1000 mAh hasta 3.30 V"),
}
CAP_MAH = 1000.0


def muestra(nombre, n):
    nom, lo, hi, *_ = PARAMS[nombre]
    if lo == hi:
        return np.full(n, nom)
    return rng.triangular(lo, nom, hi, n)


def modelo(p):
    """Corriente media (mA) y carga por ciclo (mAs) a partir de un diccionario de parametros."""
    t_on = p["t_pre"] + p["t_activo"]                       # ESP32 y radio en modo 0 encendidos
    q_esp = p["I_esp_act"] * t_on
    q_rx = p["I_e220_rx"] * t_on                            # el radio queda en recepcion todo el tiempo activo
    q_tx = (p["I_e220_tx"] - p["I_e220_rx"]) * p["t_aire"]  # exceso durante la transmision
    q_max = (p["I_max_chip"] + p["I_max_bias"]) * p["t_sensor"]
    q_ciclo = q_esp + q_rx + q_tx + q_max                   # mAs
    i_sleep = p["I_xiao_sleep"] + p["I_e220_sleep"] + p["I_divisor"] + p["I_proteccion"]
    i_auto = CAP_MAH * p["autodescarga"] / 100.0 / (30 * 24)   # mA equivalentes
    i_media = q_ciclo / p["T_ciclo"] + i_sleep + i_auto
    return i_media, {"ESP32-C6": q_esp, "E220 RX": q_rx, "E220 TX (exceso)": q_tx, "MAX31865 + RTD": q_max}


# ---------- Caso nominal ----------
nominal = {k: v[0] for k, v in PARAMS.items()}
i_nom, q_nom = modelo(nominal)
aut_nom_d = CAP_MAH * nominal["C_util"] / i_nom / 24
print(f"Nominal: carga por ciclo {sum(q_nom.values()):.1f} mAs | corriente media {i_nom:.3f} mA | autonomia {aut_nom_d:.0f} dias")
for k, v in q_nom.items():
    print(f"   {k:18s} {v:6.1f} mAs ({100*v/sum(q_nom.values()):4.1f} %)")

# ---------- Monte Carlo ----------
mc = {k: muestra(k, N) for k in PARAMS}
i_mc, _ = modelo(mc)
aut_mc = CAP_MAH * mc["C_util"] / i_mc / 24
p5, p50, p95 = np.percentile(aut_mc, [5, 50, 95])
i5, i50, i95 = np.percentile(i_mc, [5, 50, 95])
print(f"\nMonte Carlo (N={N}): corriente media {i50:.3f} mA [{i5:.3f}, {i95:.3f}] (p5, p95)")
print(f"Autonomia por fichas: {p50:.0f} dias [{p5:.0f}, {p95:.0f}] (p5, p95)")

# ---------- Contraste con la descarga observada ----------
# Lo que implica la caida de 23 mV en 120 h segun autonomia_bateria.py
I_LINEAL, AUT_LINEAL = 0.236, 177      # extrapolacion lineal a 3.30 V
I_OCV, AUT_OCV = (0.128, (291, 307))    # curva OCV-SoC, cortes 3.30 / 3.00 V
print(f"\nDescarga observada: lineal {I_LINEAL} mA -> {AUT_LINEAL} d | curva LiPo {I_OCV} mA -> {AUT_OCV[0]}-{AUT_OCV[1]} d")
print(f"Razon corriente ficha / observada: {i50/I_LINEAL:.1f}x (lineal), {i50/I_OCV:.1f}x (LiPo)")

# caida de tension que el modelo de fichas habria producido en 120 h (tabla OCV-SoC del script de autonomia)
soc_tab = np.array([100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 0]) / 100.0
ocv_tab = np.array([4.20, 4.05, 3.96, 3.89, 3.84, 3.81, 3.78, 3.75, 3.70, 3.60, 3.00])
soc0 = float(np.interp(4.112, ocv_tab[::-1], soc_tab[::-1]))
soc_120 = soc0 - i50 * 120 / CAP_MAH
v_120 = float(np.interp(soc_120, soc_tab[::-1], ocv_tab[::-1]))
print(f"Con {i50:.3f} mA, la tabla OCV-SoC predice {v_120:.3f} V a las 120 h (observado 4.089 V)")

# ---------- Comprobaciones ----------
assert abs(sum(q_nom.values()) / nominal["T_ciclo"] + 0.0271 + 0.0278 - i_nom) < 0.01 * i_nom
assert p5 < p50 < p95 and i5 < i50 < i95
# si el radio quedara en recepcion durante el sueno (16.8 mA), la celda duraria menos de 3 dias:
i_sin_dormir, _ = modelo({**nominal, "I_e220_sleep": 16.8})
assert CAP_MAH / i_sin_dormir / 24 < 3, "el radio en modo 0 durante el sueno agotaria la celda en dias"
print(f"Comprobacion: con el radio despierto en el sueno la autonomia seria {CAP_MAH/i_sin_dormir/24:.1f} dias; "
      f"las 120 h observadas descartan ese caso.")

# ---------- Figura ----------
fig, (a1, a2) = plt.subplots(1, 2, figsize=(7.4, 3.2), gridspec_kw={"width_ratios": [1, 1.5]})
nombres = list(q_nom)
vals = [q_nom[k] for k in nombres]
a1.barh(nombres, vals, color=["#1e88e5", "#64b5f6", "#0d47a1", "#f59e0b"])
for y, v in enumerate(vals):
    a1.text(v + 1, y, f"{v:.0f}", va="center", fontsize=7.5)
a1.set_xlabel("Carga por ciclo (mA·s), caso nominal")
a1.set_title("Reparto del consumo activo", fontsize=9)
a1.invert_yaxis()
a1.grid(True, axis="x", lw=0.3, alpha=0.5)

a2.hist(aut_mc, bins=80, color="#94a3b8", edgecolor="none")
a2.axvline(p50, color="#1e293b", lw=1.2)
a2.axvspan(p5, p95, color="#1e293b", alpha=0.08)
a2.axvline(AUT_LINEAL, color="#b91c1c", lw=1.2, ls="--")
a2.axvspan(AUT_OCV[0], AUT_OCV[1], color="#16a34a", alpha=0.25)
a2.text(p50, a2.get_ylim()[1] * 0.95, f" fichas: {p50:.0f} d\n [{p5:.0f}, {p95:.0f}]", fontsize=7.5, va="top")
a2.text(AUT_LINEAL, a2.get_ylim()[1] * 0.60, f" descarga observada,\n lineal: {AUT_LINEAL} d", fontsize=7.5, va="top", color="#b91c1c")
a2.text(AUT_OCV[0] - 3, a2.get_ylim()[1] * 0.35, f"descarga observada,\ncurva LiPo: {AUT_OCV[0]}–{AUT_OCV[1]} d ", fontsize=7.5, va="top", ha="right", color="#15803d")
a2.set_xlabel("Autonomía estimada hasta 3.30 V (días)")
a2.set_ylabel("Frecuencia (Monte Carlo)")
a2.set_title("Modelo por fichas frente a la descarga observada", fontsize=9)
a2.grid(True, lw=0.3, alpha=0.5)
fig.tight_layout()
for ext in ("pdf", "png"):
    fig.savefig(os.path.join(OUT, f"modelo-consumo.{ext}"))
plt.close(fig)
print("OK modelo-consumo")

# ---------- Filas LaTeX de la tabla de entradas ----------
print("\n--- filas LaTeX (parametro, nominal, rango, fuente) ---")
for k, (nom, lo, hi, u, src) in PARAMS.items():
    rango = f"{lo:g}--{hi:g}" if lo != hi else "fijo"
    print(f"{k.replace('_', r'\\_')} & {nom:g} {u} & {rango} & {src} \\\\")
