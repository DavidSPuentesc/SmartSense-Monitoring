"""Diagrama de montaje para medir la corriente activa del nodo con shunt de
lado bajo (low-side) y el FNIRSI 2C23T en modo osciloscopio.

Salida: figuras/montaje-corriente.{pdf,png}
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

BASE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(BASE, "..", "figuras"))
plt.rcParams.update({"font.size": 9, "savefig.dpi": 200, "savefig.bbox": "tight"})

fig, ax = plt.subplots(figsize=(6.6, 4.2))
ax.set_xlim(0, 100); ax.set_ylim(0, 100); ax.axis("off")


def caja(x, y, w, h, texto, fondo, borde, fs=9):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.6,rounding_size=2",
                                linewidth=1.3, edgecolor=borde, facecolor=fondo))
    ax.text(x + w / 2, y + h / 2, texto, ha="center", va="center", fontsize=fs)


def cable(x0, y0, x1, y1, color="#222", lw=1.8):
    ax.plot([x0, x1], [y0, y1], color=color, lw=lw, solid_capstyle="round")


def resistor(x0, x1, y, label):
    """Zigzag simple centrado entre x0 y x1."""
    n = 6
    xs = [x0]
    ys = [y]
    import numpy as np
    seg = (x1 - x0) / n
    for i in range(1, n):
        xs.append(x0 + seg * i)
        ys.append(y + (3 if i % 2 else -3))
    xs.append(x1); ys.append(y)
    ax.plot(xs, ys, color="#222", lw=1.8)
    ax.text((x0 + x1) / 2, y - 8, label, ha="center", va="top", fontsize=8.5, color="#b91c1c")


# Bloques
caja(6, 45, 22, 22, "Batería\nLiPo 3.7 V", "#e8f5e9", "#43a047")
caja(64, 45, 26, 22, "Nodo\nESP32-C6", "#e3f2fd", "#1e88e5")
caja(28, 8, 44, 16, "FNIRSI 2C23T\n(modo osciloscopio)", "#fff3e0", "#fb8c00")

# Terminales
bat_pos = (28, 61); bat_neg = (28, 51)
nodo_pos = (64, 61); nodo_neg = (64, 51)

# Cable superior: batería(+) -> nodo(+)
cable(*bat_pos, nodo_pos[0], bat_pos[1])
ax.text(46, 64, "+", fontsize=12, ha="center", color="#b91c1c")
ax.add_patch(FancyArrowPatch((44, 61), (50, 61), arrowstyle="-|>", mutation_scale=13,
                             color="#b91c1c", lw=1.5))
ax.text(47, 57, "corriente I", fontsize=8, ha="center", color="#b91c1c")

# Cable inferior: nodo(-) -> shunt -> batería(-)   (shunt de lado bajo)
sh_x0, sh_x1, sh_y = 42, 54, 51
cable(nodo_neg[0], nodo_neg[1], sh_x1, sh_y)     # nodo(-) -> shunt der.
resistor(sh_x0, sh_x1, sh_y, "R shunt 1 Ω")
cable(sh_x0, sh_y, bat_neg[0], bat_neg[1])       # shunt izq. -> batería(-)
ax.text(34, 54, "-", fontsize=12, ha="center", color="#222")

# Puntas del osciloscopio
# GND (negra) al lado de batería(-) del shunt; punta (roja) al lado de nodo(-)
cable(sh_x0, sh_y, sh_x0, 24, color="#222", lw=1.6)          # GND -> extremo izq.
ax.text(sh_x0 - 1.5, 32, "GND", fontsize=8, ha="right", color="#222")
cable(sh_x1, sh_y, sh_x1, 24, color="#b91c1c", lw=1.6)        # punta -> extremo der.
ax.text(sh_x1 + 1.5, 32, "punta", fontsize=8, ha="left", color="#b91c1c")

# Anotaciones
ax.text(50, 3, "$V_{shunt} = I \\times R$   $\\Rightarrow$   $I = V_{shunt} / R$",
        ha="center", va="bottom", fontsize=9.5)
ax.text(50, 95, "Medición de corriente con shunt de lado bajo",
        ha="center", va="center", fontsize=10, fontweight="bold")
ax.text(50, 90, "El osciloscopio lee la caída en el shunt; el promedio del pulso da la corriente activa",
        ha="center", va="center", fontsize=7.5, color="#555")

for ext in ("pdf", "png"):
    fig.savefig(os.path.join(OUT, f"montaje-corriente.{ext}"))
print("OK montaje-corriente")
