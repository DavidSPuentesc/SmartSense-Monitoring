"""Estimacion de autonomia a partir de la prueba de descarga y el modelo de
consumo pulsado (sleep + pico de transmision).

Datos medidos (bateria 1000 mAh):
  13-08-2026 -> 4.112 V   (t = 0 h)
  18-08-2026 -> 4.089 V   (t = 120 h, 5 dias)

Genera dos figuras en figuras/:
  autonomia-descarga.{pdf,png}  curva de descarga y extrapolacion al corte
  autonomia-ciclo.{pdf,png}     perfil de corriente en un ciclo (sleep + pico)
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

BASE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(BASE, "..", "figuras"))
plt.rcParams.update({"font.size": 9, "figure.dpi": 150, "savefig.bbox": "tight"})

# ---------- Datos y parametros ----------
CAP_MAH   = 1000.0          # capacidad de la bateria de prueba
T_ACTIVE  = 2.279           # s activos por ciclo (medido)
T_CYCLE   = 120.0           # s por ciclo (del CSV)
I_SLEEP   = 0.015           # mA en sueño profundo (datasheet ESP32-C6)
V0, V5    = 4.112, 4.089    # V a t=0 y t=120 h
H         = 120.0           # h entre medidas
CORTE     = {"3.30 V": 3.30, "3.00 V": 3.00}

# ---------- Calculos de descarga ----------
tasa = (V0 - V5) / H                       # V/h
print(f"Tasa de caida: {tasa*1000:.3f} mV/h")
autonomia = {}
for k, vc in CORTE.items():
    h = (V0 - vc) / tasa
    autonomia[k] = h
    print(f"Autonomia hasta {k}: {h:.0f} h = {h/24:.0f} dias = {h/24/30:.1f} meses")

# Corriente promedio implicada (usando el corte de 3.30 V, conservador)
h_ref = autonomia["3.30 V"]
i_prom = CAP_MAH / h_ref                    # mA
duty = T_ACTIVE / T_CYCLE
i_active = (i_prom - I_SLEEP * (1 - duty)) / duty
print(f"\nCorriente promedio (a 3.30 V): {i_prom:.3f} mA")
print(f"Ciclo de trabajo: {duty*100:.2f} %")
print(f"Corriente activa implicada (promedio del pico): {i_active:.1f} mA")

# ---------- Figura 1: curva de descarga ----------
fig, ax = plt.subplots(figsize=(6.4, 3.4))
dias_medidos = [0, H/24]
ax.plot(dias_medidos, [V0, V5], "o", color="#1e88e5", ms=7, zorder=5, label="Medido")
# extrapolacion lineal hasta 3.00 V
t_fin = (V0 - 3.00) / tasa / 24
tt = np.linspace(0, t_fin, 200)
ax.plot(tt, V0 - tasa * (tt * 24), color="#1e88e5", lw=1.3, ls="--",
        label="Extrapolación lineal")
for k, vc in CORTE.items():
    ax.axhline(vc, color="#b91c1c" if vc == 3.30 else "#f59e0b", lw=0.9, ls=":")
    ax.text(t_fin*0.99, vc+0.01, f"corte {k}  ({autonomia[k]/24:.0f} d)",
            ha="right", va="bottom", fontsize=7.5,
            color="#b91c1c" if vc == 3.30 else "#f59e0b")
ax.set_xlabel("Tiempo (días)")
ax.set_ylabel("Voltaje de batería (V)")
ax.set_title("Descarga de la batería de 1000 mAh (nodo en operación normal)")
ax.set_ylim(2.9, 4.25)
ax.grid(True, lw=0.3, alpha=0.5)
ax.legend(loc="upper right", fontsize=8)
for ext in ("pdf", "png"):
    fig.savefig(os.path.join(OUT, f"autonomia-descarga.{ext}"))
plt.close(fig)
print("OK autonomia-descarga")

# ---------- Figura 2: perfil de corriente en un ciclo ----------
# Modelo: sueño la mayor parte del ciclo y un pico activo de T_ACTIVE s.
fig, ax = plt.subplots(figsize=(6.4, 3.0))
t = np.linspace(0, T_CYCLE, 2000)
i = np.full_like(t, I_SLEEP)
i[(t >= 5) & (t < 5 + T_ACTIVE)] = i_active     # pico activo
ax.plot(t, i, color="#16a34a", lw=1.4)
ax.fill_between(t, I_SLEEP, i, color="#16a34a", alpha=0.15)
ax.set_yscale("log")
ax.set_xlabel("Tiempo dentro de un ciclo (s)")
ax.set_ylabel("Corriente (mA, escala log)")
ax.set_title("Perfil de consumo por ciclo: sueño + pico de transmisión (modelo)")
ax.annotate(f"pico activo ≈ {i_active:.0f} mA\n(arranque + lectura + TX LoRa, {T_ACTIVE:.2f} s)",
            xy=(5 + T_ACTIVE/2, i_active), xytext=(28, i_active*0.5),
            fontsize=7.5, arrowprops=dict(arrowstyle="->", lw=0.7))
ax.annotate(f"sueño profundo ≈ {I_SLEEP*1000:.0f} µA ({(1-duty)*100:.1f}% del tiempo)",
            xy=(80, I_SLEEP), xytext=(40, I_SLEEP*3), fontsize=7.5,
            arrowprops=dict(arrowstyle="->", lw=0.7))
ax.text(0.99, 0.06, f"Corriente promedio del ciclo ≈ {i_prom:.2f} mA  ·  ciclo de trabajo {duty*100:.1f}%",
        transform=ax.transAxes, ha="right", fontsize=7.5, color="#333")
ax.grid(True, which="both", lw=0.3, alpha=0.4)
for ext in ("pdf", "png"):
    fig.savefig(os.path.join(OUT, f"autonomia-ciclo.{ext}"))
plt.close(fig)
print("OK autonomia-ciclo")
