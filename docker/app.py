"""Réplica local del dashboard SmartSense para análisis de datos.

Recibe exportes CSV de la planta (carga masiva), los guarda en SQLite y
sirve un dashboard neutro para explorar el histórico y sacar figuras.
"""
import csv
import io
import os
import re
import sqlite3
import sys
from datetime import datetime

from flask import Flask, jsonify, render_template, request

BASE = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.path.join(BASE, "data", "lecturas.db")
APP_NAME = os.environ.get("APP_NAME", "Monitoreo de Temperatura Industrial")

# Rango físico de una PT100; fuera de esto la lectura es basura segura
TEMP_MIN, TEMP_MAX = -200.0, 850.0
MAX_ERRORES_DETALLE = 10

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 64 * 1024 * 1024  # 64 MB


# SQLite local, sin ORM; si algún día hay multiusuario concurrente, migrar a Postgres
def db():
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    con = sqlite3.connect(DB_PATH)
    con.execute(
        """CREATE TABLE IF NOT EXISTS lecturas (
               sensor TEXT NOT NULL,
               ts     TEXT NOT NULL,
               temp_c REAL NOT NULL,
               bateria REAL,
               UNIQUE(sensor, ts))"""
    )
    return con


# Alias aceptados en el encabezado del CSV (case-insensitive)
COLS = {
    "sensor": {"sensor", "mac", "nodo", "nombre", "id"},
    "fecha": {"fecha", "date", "dia"},
    "hora": {"hora", "time"},
    "ts": {"timestamp", "ts", "fecha_hora", "fechahora", "datetime"},
    "temp": {"temp", "temperatura", "temp_c", "temperatura_c", "temperature"},
    "bat": {"bateria", "battery", "bat", "bateria_pct"},
}

TS_FORMATS = (
    "%Y-%m-%d %H:%M:%S",
    "%Y-%m-%d %H:%M",
    "%d/%m/%Y %H:%M:%S",
    "%d/%m/%Y %H:%M",
    "%Y/%m/%d %H:%M:%S",
    "%d-%m-%Y %H:%M:%S",
    "%d-%m-%Y %H:%M",
)


class CsvInvalido(ValueError):
    """CSV rechazado por completo (encabezado o archivo inservible)."""


def normaliza_ts(texto):
    texto = texto.strip()
    for fmt in TS_FORMATS:
        try:
            return datetime.strptime(texto, fmt).strftime("%Y-%m-%d %H:%M:%S")
        except ValueError:
            pass
    return None


def parse_csv(data):
    """Devuelve (filas_ok, errores). errores = [{linea, motivo}, ...].

    Tolera ',' o ';' y coma decimal (Excel es-CO). Lanza CsvInvalido si el
    archivo o el encabezado no sirven, con un mensaje que explica qué falta.
    """
    texto = data.decode("utf-8-sig", errors="replace")
    lineas = texto.splitlines()
    if not lineas:
        raise CsvInvalido("El archivo está vacío.")
    delim = ";" if lineas[0].count(";") > lineas[0].count(",") else ","
    filas = list(csv.reader(io.StringIO(texto), delimiter=delim))
    con_datos = [(n, f) for n, f in enumerate(filas, start=1) if any(c.strip() for c in f)]
    if len(con_datos) < 2:
        raise CsvInvalido("El archivo no tiene filas de datos (solo encabezado o nada).")

    header = [h.strip().lower() for h in con_datos[0][1]]
    idx = {}
    for clave, alias in COLS.items():
        for i, h in enumerate(header):
            if h in alias:
                idx[clave] = i
                break
    faltan = []
    if "sensor" not in idx:
        faltan.append("sensor (acepta: sensor, mac, nodo, nombre, id)")
    if "temp" not in idx:
        faltan.append("temperatura (acepta: temperatura, temp, temp_c, temperature)")
    if "ts" not in idx and "fecha" not in idx:
        faltan.append("fecha/hora (acepta: timestamp, ts, fecha_hora, datetime — o fecha y hora en columnas)")
    if faltan:
        raise CsvInvalido("Encabezado no reconocido. Falta columna de: " + "; ".join(faltan))

    ok, errores = [], []

    def error(nlinea, motivo):
        if len(errores) < MAX_ERRORES_DETALLE:
            errores.append({"linea": nlinea, "motivo": motivo})
        else:
            errores.append(None)  # solo cuenta

    for nlinea, fila in con_datos[1:]:
        try:
            sensor = fila[idx["sensor"]].strip()
        except IndexError:
            error(nlinea, "fila con menos columnas que el encabezado")
            continue
        if not sensor:
            error(nlinea, "sensor vacío")
            continue

        try:
            if "ts" in idx:
                crudo = fila[idx["ts"]]
            else:
                crudo = fila[idx["fecha"]].strip() + " " + (fila[idx["hora"]].strip() if "hora" in idx else "00:00:00")
            ts = normaliza_ts(crudo)
        except IndexError:
            ts = None
        if ts is None:
            error(nlinea, "fecha/hora inválida")
            continue

        try:
            temp = float(fila[idx["temp"]].strip().replace(",", "."))
        except (ValueError, IndexError):
            error(nlinea, "temperatura no numérica")
            continue
        if not (TEMP_MIN <= temp <= TEMP_MAX):
            error(nlinea, f"temperatura fuera de rango físico ({TEMP_MIN:g} a {TEMP_MAX:g} °C)")
            continue

        bat = None
        if "bat" in idx:
            try:
                crudo_bat = fila[idx["bat"]].strip()
                if crudo_bat:
                    bat = float(crudo_bat.replace(",", "."))
                    if not (0.0 <= bat <= 100.0):
                        bat = None  # batería inválida no invalida la lectura de temperatura
            except (ValueError, IndexError):
                bat = None

        ok.append((sensor, ts, temp, bat))
    return ok, errores


@app.errorhandler(413)
def muy_grande(_e):
    return jsonify({"error": "El archivo supera el límite de 64 MB."}), 413


@app.errorhandler(500)
def error_interno(_e):
    return jsonify({"error": "Error interno del servidor. Revisa los logs del contenedor."}), 500


@app.route("/")
def index():
    return render_template("index.html", app_name=APP_NAME)


@app.route("/api/importar", methods=["POST"])
def importar():
    archivo = request.files.get("archivo")
    if not archivo or not archivo.filename:
        return jsonify({"error": "No llegó ningún archivo."}), 400
    if not archivo.filename.lower().endswith((".csv", ".txt")):
        return jsonify({"error": "El archivo debe ser .csv (o .txt separado por comas)."}), 400

    try:
        filas, errores = parse_csv(archivo.read())
    except CsvInvalido as e:
        return jsonify({"error": str(e)}), 400

    con = db()
    cur = con.executemany("INSERT OR IGNORE INTO lecturas VALUES (?,?,?,?)", filas)
    con.commit()
    insertadas = cur.rowcount if cur.rowcount != -1 else 0
    con.close()
    return jsonify({
        "insertadas": insertadas,
        "duplicadas": len(filas) - insertadas,
        "errores": len(errores),
        "detalle_errores": [e for e in errores if e],
    })


@app.route("/api/sensores")
def sensores():
    con = db()
    filas = con.execute(
        "SELECT sensor, COUNT(*), MIN(ts), MAX(ts) FROM lecturas GROUP BY sensor ORDER BY sensor"
    ).fetchall()
    con.close()
    return jsonify([
        {"sensor": s, "lecturas": n, "desde": d, "hasta": h} for s, n, d, h in filas
    ])


FECHA_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


@app.route("/api/historico")
def historico():
    sensor = request.args.get("sensor", "").strip()
    if not sensor:
        return jsonify({"error": "Falta el parámetro sensor."}), 400
    desde = request.args.get("desde", "").strip()
    hasta = request.args.get("hasta", "").strip()
    for nombre, valor in (("desde", desde), ("hasta", hasta)):
        if valor and not FECHA_RE.match(valor):
            return jsonify({"error": f"Fecha '{nombre}' inválida, formato esperado AAAA-MM-DD."}), 400
    if desde and hasta and desde > hasta:
        desde, hasta = hasta, desde
    lim_desde = desde or "0000"
    lim_hasta = (hasta or "9999") + " 23:59:59"

    con = db()
    filas = con.execute(
        "SELECT ts, temp_c FROM lecturas WHERE sensor=? AND ts>=? AND ts<=? ORDER BY ts",
        (sensor, lim_desde, lim_hasta),
    ).fetchall()
    stats = con.execute(
        "SELECT MIN(temp_c), MAX(temp_c), AVG(temp_c), COUNT(*) FROM lecturas WHERE sensor=? AND ts>=? AND ts<=?",
        (sensor, lim_desde, lim_hasta),
    ).fetchone()
    con.close()
    paso = max(1, len(filas) // 2000)  # decimación simple para no ahogar el navegador
    return jsonify({
        "puntos": [{"ts": t, "temp": v} for t, v in filas[::paso]],
        "stats": {"min": stats[0], "max": stats[1], "prom": stats[2], "total": stats[3]},
    })


def selftest():
    filas, err = parse_csv(b"sensor,timestamp,temperatura_c,bateria_pct\nA1,2026-01-15 10:00:00,25.4,87\nA1,2026-01-15 10:01:00,25.6,87\n")
    assert len(filas) == 2 and not err and filas[0][2] == 25.4, (filas, err)

    filas, err = parse_csv("mac;fecha;hora;temperatura\nAA:BB;15/01/2026;10:00;25,4\nAA:BB;fila;mala;x\n".encode())
    assert len(filas) == 1 and len(err) == 1 and filas[0][1] == "2026-01-15 10:00:00", (filas, err)
    assert err[0]["motivo"] == "fecha/hora inválida", err

    filas, err = parse_csv(b"sensor,timestamp,temperatura\nA1,2026-01-15 10:00:00,999\nA1,2026-01-15 10:01:00,-300\n")
    assert not filas and len(err) == 2, (filas, err)

    filas, err = parse_csv(b"sensor,timestamp,temperatura,bateria\nA1,2026-01-15 10:00:00,25.0,150\n")
    assert len(filas) == 1 and filas[0][3] is None, filas

    for datos, esperado in (
        (b"", "vacío"),
        (b"columna_rara,otra\n1,2\n", "Encabezado"),
    ):
        try:
            parse_csv(datos)
            raise AssertionError("debió rechazar: " + repr(datos))
        except CsvInvalido as e:
            assert esperado in str(e), e

    print("selftest OK")


if __name__ == "__main__":
    if "selftest" in sys.argv:
        selftest()
    else:
        app.run(host="0.0.0.0", port=int(os.environ.get("PORT", "8080")))
