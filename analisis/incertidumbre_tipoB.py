"""Presupuesto de incertidumbre tipo B (GUM, JCGM 100) de la cadena PT100 + MAX31865,
a partir de especificaciones, para el punto de 70 C de la comparacion de campo.

No sustituye una calibracion: cuantifica lo que las fichas permiten acotar y deja
fuera lo que no (contacto termico, montaje, gradiente superficie-sonda).

Entradas:
  PT100 IEC 60751: clase B +/-(0.30 + 0.005|t|) C; clase A +/-(0.15 + 0.002|t|) C.
  MAX31865: exactitud total 0.5 C max (hoja de datos); 15 bits -> 0.03125 C por cuenta.
  R_ref 421.1 ohm, tolerancia supuesta 0.1 %; sensibilidad PT100 0.385 ohm/C.
  Desequilibrio residual de los tres hilos: supuesto +/-0.05 ohm.
  Polarizacion RTD ~6 mA solo durante la conversion: autocalentamiento despreciable.
Uso: python incertidumbre_tipoB.py
"""
import math

T = 70.0                      # C, punto de la comparacion de campo
R_RTD = 100.0 * (1 + 3.9083e-3 * T - 5.775e-7 * T**2)   # ohm, IEC 60751
ALPHA = 0.385                 # ohm/C

def rect(a):                  # distribucion rectangular de semiancho a
    return a / math.sqrt(3)

def presupuesto(clase):
    tol = (0.30 + 0.005 * T) if clase == "B" else (0.15 + 0.002 * T)
    filas = [
        ("Tolerancia de la PT100 (clase %s)" % clase, tol, rect(tol)),
        ("Exactitud total del MAX31865", 0.5, rect(0.5)),
        ("Tolerancia de R_ref (0.1 %)", 0.001 * R_RTD / ALPHA, rect(0.001 * R_RTD / ALPHA)),
        ("Desequilibrio de los tres hilos (0.05 ohm)", 0.05 / ALPHA, rect(0.05 / ALPHA)),
        ("Resolucion (0.03125 C)", 0.03125 / 2, 0.03125 / math.sqrt(12)),
        ("Autocalentamiento (6 mA en 0.3 s cada 120 s)", 0.0, 0.0),
    ]
    uc = math.sqrt(sum(u**2 for _, _, u in filas))
    return filas, uc, 2 * uc

for clase in ("B", "A"):
    filas, uc, U = presupuesto(clase)
    print(f"\n=== Clase {clase}: R_RTD({T:.0f} C) = {R_RTD:.1f} ohm ===")
    for nombre, a, u in filas:
        print(f"  {nombre:46s} semiancho {a:5.3f} C  u = {u:5.3f} C")
    print(f"  u_c = {uc:.3f} C   U (k=2) = {U:.2f} C")

filas, uc, U = presupuesto("B")
assert 1.0 <= U <= 1.1, U            # el presupuesto con clase B debe dar ~1.0 C
assert abs(R_RTD - 127.1) < 0.2
print("\n--- filas LaTeX (clase B) ---")
for nombre, a, u in filas:
    print(f"{nombre} & $\\pm${a:.2f} & rectangular & {u:.3f} \\\\")
print(f"Combinada $u_c$ & & & {uc:.2f} \\\\")
print(f"Expandida $U$ ($k=2$) & & & {U:.2f} \\\\")
