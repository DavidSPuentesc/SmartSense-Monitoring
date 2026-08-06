"""Genera los esquemáticos KiCad de SmartSense (nodo sensor y gateway).

Versión 2: cableado REAL entre componentes (los pines enfrentados quedan
alineados en la misma fila y se unen con cables; los saltos largos usan
etiquetas de red, como en cualquier esquemático profesional).

Uso:  python gen_esquematicos.py   ->  nodo_sensor.kicad_sch, gateway.kicad_sch
Verificación:  kicad-cli sch erc <archivo>
"""
import os
import uuid

GRID = 2.54
NC = None          # pin sin conectar
W = "@wire"        # pin que se cablea manualmente más abajo


def u():
    return str(uuid.uuid4())


def f(v):
    return f"{v:.2f}"


class Symbol:
    """Símbolo rectangular con pines a izquierda y derecha (todos pasivos).

    Las listas admiten None para dejar una fila vacía (hueco).
    """

    def __init__(self, name, left, right, width=25.4):
        self.name, self.left, self.right = name, left, right
        self.half_w = width / 2
        n = max(len(left), len(right))
        self.half_h = 1.27 * (n + 1)
        self.pins = {}  # nombre -> (x, y, lado, numero)
        num = 1
        for i, pname in enumerate(left):
            if pname is None:
                continue
            self.pins[pname] = (-(self.half_w + GRID), 1.27 * n - GRID * i, "L", str(num))
            num += 1
        for i, pname in enumerate(right):
            if pname is None:
                continue
            self.pins[pname] = (self.half_w + GRID, 1.27 * n - GRID * i, "R", str(num))
            num += 1

    def row_offset(self, i):
        """Desplazamiento vertical (coordenadas de hoja) de la fila i respecto al origen."""
        n = max(len(self.left), len(self.right))
        return -(1.27 * n - GRID * i)

    def lib_def(self, prefix="smartsense:", indent="    "):
        s = [f'{indent}(symbol "{prefix}{self.name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)']
        s.append(f'{indent}  (property "Reference" "U" (at 0 {f(self.half_h + 2.54)} 0) (effects (font (size 1.27 1.27))))')
        s.append(f'{indent}  (property "Value" "{self.name}" (at 0 {f(-self.half_h - 2.54)} 0) (effects (font (size 1.27 1.27))))')
        s.append(f'{indent}  (symbol "{self.name}_0_1"')
        s.append(f'{indent}    (rectangle (start {f(-self.half_w)} {f(self.half_h)}) (end {f(self.half_w)} {f(-self.half_h)})'
                 f' (stroke (width 0.254) (type default)) (fill (type background))))')
        s.append(f'{indent}  (symbol "{self.name}_1_1"')
        for pname, (px, py, side, num) in self.pins.items():
            ang = 0 if side == "L" else 180
            s.append(f'{indent}    (pin passive line (at {f(px)} {f(py)} {ang}) (length {f(GRID)})'
                     f' (name "{pname}" (effects (font (size 1.27 1.27))))'
                     f' (number "{num}" (effects (font (size 1.27 1.27)))))')
        s.append(f"{indent}  ))")
        return "\n".join(s)


class Inst:
    def __init__(self, sym, x, y):
        self.sym, self.x, self.y = sym, x, y

    def pin(self, name):
        """Punto de conexión global del pin (el eje Y del símbolo se invierte)."""
        px, py, _side, _num = self.sym.pins[name]
        return (self.x + px, self.y - py)


class Sheet:
    def __init__(self, title):
        self.title = title
        self.uuid = u()
        self.symbols = {}
        self.body = []

    def text(self, texto, x, y, size=2.54):
        self.body.append(f'  (text "{texto}" (exclude_from_sim no) (at {f(x)} {f(y)} 0)'
                         f' (effects (font (size {f(size)} {f(size)})) (justify left bottom)) (uuid "{u()}"))')

    def wire(self, *pts):
        for a, b in zip(pts, pts[1:]):
            self.body.append(f'  (wire (pts (xy {f(a[0])} {f(a[1])}) (xy {f(b[0])} {f(b[1])}))'
                             f' (stroke (width 0) (type default)) (uuid "{u()}"))')

    def junction(self, x, y):
        self.body.append(f'  (junction (at {f(x)} {f(y)}) (diameter 0) (color 0 0 0 0) (uuid "{u()}"))')

    def label(self, nombre, x, y, justify="left"):
        self.body.append(f'  (label "{nombre}" (at {f(x)} {f(y)} 0)'
                         f' (effects (font (size 1.27 1.27)) (justify {justify} bottom)) (uuid "{u()}"))')

    def glabel(self, nombre, x, y, ang=0):
        """Etiqueta global: para redes que saltan con un solo pin por punta."""
        self.body.append(f'  (global_label "{nombre}" (shape passive) (at {f(x)} {f(y)} {ang})'
                         f' (effects (font (size 1.27 1.27)) (justify left)) (uuid "{u()}"))')

    def stub(self, punto, side, nombre):
        gx, gy = punto
        ex = gx - GRID if side == "L" else gx + GRID
        self.wire((gx, gy), (ex, gy))
        self.label(nombre, ex, gy, "right" if side == "L" else "left")

    def place(self, sym, ref, value, x, y, nets, tags="auto"):
        """nets: pin -> etiqueta (str), NC (no conectado) o W (cableado manual).

        tags: "auto" (ref arriba, valor abajo), "top" o "bottom" (ambos juntos).
        """
        if tags == "top":
            y_ref, y_val = y - sym.half_h - 5.08, y - sym.half_h - 2.54
        elif tags == "bottom":
            y_ref, y_val = y + sym.half_h + 2.54, y + sym.half_h + 5.08
        else:
            y_ref, y_val = y - sym.half_h - 2.54, y + sym.half_h + 2.54
        self.symbols[sym.name] = sym
        lines = [f'  (symbol (lib_id "smartsense:{sym.name}") (at {f(x)} {f(y)} 0) (unit 1)'
                 f' (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no) (uuid "{u()}")']
        lines.append(f'    (property "Reference" "{ref}" (at {f(x)} {f(y_ref)} 0) (effects (font (size 1.27 1.27))))')
        lines.append(f'    (property "Value" "{value}" (at {f(x)} {f(y_val)} 0) (effects (font (size 1.27 1.27))))')
        lines.append(f'    (property "Footprint" "" (at {f(x)} {f(y)} 0) (effects (font (size 1.27 1.27)) hide))')
        lines.append(f'    (property "Datasheet" "" (at {f(x)} {f(y)} 0) (effects (font (size 1.27 1.27)) hide))')
        for _pname, (_px, _py, _side, num) in sym.pins.items():
            lines.append(f'    (pin "{num}" (uuid "{u()}"))')
        lines.append(f'    (instances (project "smartsense" (path "/{self.uuid}" (reference "{ref}") (unit 1)))))')
        self.body.append("\n".join(lines))

        inst = Inst(sym, x, y)
        for pname, (_px, _py, side, _num) in sym.pins.items():
            if pname not in nets:
                raise ValueError(f"{sym.name}: falta red para pin {pname}")
            accion = nets[pname]
            if accion is NC:
                gx, gy = inst.pin(pname)
                self.body.append(f'  (no_connect (at {f(gx)} {f(gy)}) (uuid "{u()}"))')
            elif accion is not W and accion != W:
                self.stub(inst.pin(pname), side, accion)
        return inst

    def render(self):
        libs = "\n".join(s.lib_def() for s in self.symbols.values())
        return (f'(kicad_sch (version 20231120) (generator "gen_esquematicos") (generator_version "2.0")\n'
                f'  (uuid "{self.uuid}")\n'
                f'  (paper "A4")\n'
                f'  (title_block (title "{self.title}") (comment 1 "SmartSense Monitoring - design files"))\n'
                f'  (lib_symbols\n{libs}\n  )\n'
                + "\n".join(self.body) + "\n"
                f'  (sheet_instances (path "/" (page "1")))\n'
                f')\n')


def conecta(sheet, inst_a, pin_a, inst_b, pin_b):
    """Cable recto horizontal entre dos pines enfrentados (misma fila)."""
    a, b = inst_a.pin(pin_a), inst_b.pin(pin_b)
    if abs(a[1] - b[1]) > 0.01:
        raise ValueError(f"{pin_a} y {pin_b} no están alineados: {a[1]} vs {b[1]}")
    sheet.wire(a, b)
    return a, b


# ---------- Símbolos (pines ordenados para que el cableado salga recto) ----------
XIAO = Symbol("XIAO_ESP32C6",
              left=["D10", "D9", "D8", "D7", "3V3", "GND"],
              right=["D6", "D5", "D4", "D3", "D2", "D0_A0", "BAT_P", "BAT_N"])
E220 = Symbol("E220_433",
              left=["AUX"],
              right=["M0", "M1", "RXD", "TXD", "VCC", "GND"])
MAX = Symbol("MAX31865_MOD",
             left=["CLK", "SDO", "SDI", "CS", "VIN", "GND", "3V3_OUT", "RDY"],
             right=["F_P", "RTD_P", "RTD_N", "F_N"])
PT100 = Symbol("PT100_3H", left=["H1", "H2", "H3"], right=[], width=20.32)
TP4056 = Symbol("TP4056", left=["IN_P", "IN_N", None, None, "B_P", "B_N"], right=["OUT_P", "OUT_N"])
BAT = Symbol("BATERIA_LIPO", left=[], right=["POS", "NEG"], width=20.32)
USB = Symbol("CONN_USB_CARGA", left=[], right=["VBUS", "GND"], width=20.32)
RES = Symbol("RESISTENCIA", left=["P1"], right=["P2"], width=15.24)
GW = Symbol("ESP32C6_LCD147",
            left=["GPIO9", None, "GPIO7", "GPIO8", "3V3", "GND"],
            right=["GPIO16"])
SALIDA = Symbol("SALIDA_ALARMA", left=["S_P", "S_N"], right=[], width=20.32)

# =====================================================
# Hoja 1: nodo sensor
# =====================================================
nodo = Sheet("SmartSense - Nodo sensor de temperatura")
nodo.text("Nodo sensor: XIAO ESP32-C6 + PT100/MAX31865 + LoRa E220-433", 20.32, 25.4)
nodo.text("Verificado contra Nodo_Lora.ino; detalle en hardware/CONEXIONES.md", 20.32, 30.48, 1.27)

# Posiciones alineadas por filas (ver row_offset): E220 | XIAO | MAX31865 | PT100
X_X, Y_X = 134.62, 96.52                 # XIAO (centro)
xiao = nodo.place(XIAO, "U1", "XIAO ESP32-C6", X_X, Y_X, {
    "D10": W, "D9": W, "D8": W, "D7": W, "3V3": W, "GND": W,
    "D6": W, "D5": W, "D4": W, "D3": W, "D2": W,
    "D0_A0": W, "BAT_P": "VBAT", "BAT_N": "GND",
})
# A0 baja con cable real hasta el divisor de batería (ver bloque del divisor)
a0 = xiao.pin("D0_A0")
nodo.wire(a0, (175.26, a0[1]), (175.26, 147.32), (149.86, 147.32))
nodo.label("VBAT_SENSE", 154.94, a0[1])
e220 = nodo.place(E220, "U3", "Ebyte E220-433", 63.5, Y_X - 2.54, {
    "AUX": NC, "M0": W, "M1": W, "RXD": W, "TXD": W, "VCC": W, "GND": W,
})
maxm = nodo.place(MAX, "U4", "MAX31865 (RREF 421.1)", 210.82, Y_X, {
    "CLK": W, "SDO": W, "SDI": W, "CS": W, "VIN": W, "GND": W,
    "3V3_OUT": NC, "RDY": NC, "F_P": W, "RTD_P": W, "RTD_N": W, "F_N": W,
})
rt1 = nodo.place(PT100, "RT1", "PT100 3 hilos", 259.08, Y_X - 6.35, {
    "H1": W, "H2": W, "H3": W,
})

# E220 <-> XIAO: seis cables rectos (M0-D10, M1-D9, RXD-D8, TXD-D7, VCC-3V3, GND-GND)
for pe, px in (("M0", "D10"), ("M1", "D9"), ("RXD", "D8"), ("TXD", "D7"), ("VCC", "3V3")):
    conecta(nodo, e220, pe, xiao, px)
g1, g2 = conecta(nodo, e220, "GND", xiao, "GND")

# XIAO <-> MAX31865: SPI + alimentación conmutada, cinco cables rectos
for px, pm in (("D6", "CLK"), ("D5", "SDO"), ("D4", "SDI"), ("D3", "CS"), ("D2", "VIN")):
    conecta(nodo, xiao, px, maxm, pm)

# MAX31865 <-> PT100: tres hilos; F- se puentea con RTD- (modo 3 hilos)
conecta(nodo, maxm, "F_P", rt1, "H1")
conecta(nodo, maxm, "RTD_P", rt1, "H2")
w_rtdn_a, w_rtdn_b = conecta(nodo, maxm, "RTD_N", rt1, "H3")
fn = maxm.pin("F_N")
puente_x = fn[0] + GRID
nodo.wire(fn, (puente_x, fn[1]), (puente_x, w_rtdn_a[1]))
nodo.junction(puente_x, w_rtdn_a[1])

# Carril de GND inferior
Y_RAIL = 172.72
drop1_x = 106.68                          # baja desde el cable GND E220-XIAO
nodo.wire((drop1_x, g1[1]), (drop1_x, Y_RAIL))
nodo.junction(drop1_x, g1[1])
mg = maxm.pin("GND")
drop2_x = 180.34                          # baja desde GND del MAX31865
nodo.wire(mg, (drop2_x, mg[1]), (drop2_x, Y_RAIL))
nodo.wire((drop1_x, Y_RAIL), (drop2_x, Y_RAIL))
nodo.label("GND", drop1_x - 2.54, Y_RAIL, "right")
nodo.wire((drop1_x - 2.54, Y_RAIL), (drop1_x, Y_RAIL))
nodo.junction(drop1_x, Y_RAIL)

# Bloque de alimentación: USB -> TP4056 <- batería; salida hacia VBAT
Y_T = 149.86
tp = nodo.place(TP4056, "U5", "Carga/proteccion LiPo", 78.74, Y_T, {
    "IN_P": W, "IN_N": W, "B_P": W, "B_N": W, "OUT_P": "VBAT", "OUT_N": "GND",
})
j3 = nodo.place(USB, "J3", "USB carga", 33.02, Y_T - 5.08, {"VBUS": W, "GND": W}, tags="top")
bt = nodo.place(BAT, "BT1", "LiPo 3.7V 2000mAh", 33.02, Y_T + 5.08, {"POS": W, "NEG": W}, tags="bottom")
conecta(nodo, j3, "VBUS", tp, "IN_P")
conecta(nodo, j3, "GND", tp, "IN_N")
conecta(nodo, bt, "POS", tp, "B_P")
conecta(nodo, bt, "NEG", tp, "B_N")

# Divisor de batería: VBAT - R1 - (VBAT_SENSE) - R2 - GND
Y_D = 152.4
r1 = nodo.place(RES, "R1", "344.8k", 137.16, Y_D, {"P1": "VBAT", "P2": W})
r2 = nodo.place(RES, "R2", "994.3k", 162.56, Y_D, {"P1": W, "P2": W})
a, b = r1.pin("P2"), r2.pin("P1")
nodo.wire(a, b)
tap_x = (a[0] + b[0]) / 2
nodo.wire((tap_x, a[1]), (tap_x, 147.32))      # sube hasta el cable que viene de A0
nodo.junction(tap_x, a[1])
p2 = r2.pin("P2")
nodo.wire(p2, (p2[0] + GRID, p2[1]), (p2[0] + GRID, Y_RAIL), (drop2_x, Y_RAIL))
nodo.junction(drop2_x, Y_RAIL)

nodo.text("Divisor: VBAT - R1 - VBAT_SENSE (A0) - R2 - GND. Factor (R1+R2)/R2 = 1.347", 20.32, 190.5, 1.27)
nodo.text("PT100 3 hilos: par del mismo extremo a F+ y RTD+; hilo restante a RTD-, puenteado con F- (jumper 2/3-wire)", 20.32, 195.58, 1.27)

# =====================================================
# Hoja 2: gateway
# =====================================================
gw = Sheet("SmartSense - Gateway / visualizador")
gw.text("Gateway: placa ESP32-C6 Touch LCD 1.47 (pantalla, tactil y microSD integrados) + LoRa E220-433", 20.32, 25.4)
gw.text("Verificado contra maestro_Lora_pantalla.ino; detalle en hardware/CONEXIONES.md", 20.32, 30.48, 1.27)

Y_G = 96.52
gw1 = gw.place(GW, "GW1", "ESP32-C6 Touch LCD 1.47", 149.86, Y_G, {
    "GPIO9": W, "GPIO7": W, "GPIO8": W, "3V3": W, "GND": W, "GPIO16": W,
})
e2g = gw.place(E220, "U3", "Ebyte E220-433", 78.74, Y_G, {
    "AUX": NC, "M0": W, "M1": W, "RXD": W, "TXD": W, "VCC": W, "GND": W,
})
# M0 y M1 unidos al mismo GPIO9 (asi esta en firmware)
m0a, _m0b = conecta(gw, e2g, "M0", gw1, "GPIO9")
m1 = e2g.pin("M1")
puente_x = m1[0] + GRID
gw.wire(m1, (puente_x, m1[1]), (puente_x, m0a[1]))
gw.junction(puente_x, m0a[1])
conecta(gw, e2g, "RXD", gw1, "GPIO7")
conecta(gw, e2g, "TXD", gw1, "GPIO8")
conecta(gw, e2g, "VCC", gw1, "3V3")
gg1, _gg2 = conecta(gw, e2g, "GND", gw1, "GND")
gw.label("GND", gg1[0] + 12.7, gg1[1], "left")   # etiqueta sobre el cable GND

# Salida de alarma crítica
j2 = gw.place(SALIDA, "J2", "Salida alarma critica", 218.44, Y_G - 5.08, {"S_P": W, "S_N": "GND"})
conecta(gw, gw1, "GPIO16", j2, "S_P")

gw.text("M0 y M1 del E220 van unidos al mismo GPIO9 (asi esta cableado y en firmware)", 20.32, 152.4, 1.27)

# ---------- Escribir ----------
base = os.path.dirname(os.path.abspath(__file__))
for nombre, hoja in (("nodo_sensor", nodo), ("gateway", gw)):
    ruta = os.path.join(base, nombre + ".kicad_sch")
    with open(ruta, "w", encoding="utf-8") as fh:
        fh.write(hoja.render())
    print("OK", ruta)

# Biblioteca de símbolos + tabla de bibliotecas del proyecto
todos = {}
for hoja in (nodo, gw):
    todos.update(hoja.symbols)
with open(os.path.join(base, "smartsense.kicad_sym"), "w", encoding="utf-8") as fh:
    fh.write('(kicad_symbol_lib (version 20231120) (generator "gen_esquematicos") (generator_version "2.0")\n')
    for s in todos.values():
        fh.write(s.lib_def(prefix="", indent="  ") + "\n")
    fh.write(")\n")
with open(os.path.join(base, "sym-lib-table"), "w", encoding="utf-8") as fh:
    fh.write('(sym_lib_table (version 7)\n'
             '  (lib (name "smartsense")(type "KiCad")(uri "${KIPRJMOD}/smartsense.kicad_sym")(options "")(descr "Simbolos SmartSense"))\n'
             ')\n')
print("OK biblioteca smartsense.kicad_sym + sym-lib-table")
