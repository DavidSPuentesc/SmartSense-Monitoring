#!/usr/bin/env python3
"""
Monitor serial triple para comparar Panel / Central / Maestro.

Modo app:
    python monitor_serial_triple.py

Modo consola:
    python monitor_serial_triple.py --panel COM7 --central COM8 --maestro COM9
"""

from __future__ import annotations

import argparse
import queue
import signal
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from tkinter.scrolledtext import ScrolledText
except ImportError:
    tk = None
    ttk = None
    filedialog = None
    messagebox = None
    ScrolledText = None

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:  # pragma: no cover
    print("Falta pyserial. Instala con: pip install pyserial", file=sys.stderr)
    raise SystemExit(1) from exc


LABEL_WIDTH = 7
DEFAULT_BAUD = 115200
APP_TITLE = "Monitor Serial Triple - Panel / Central / Maestro"
SERIAL_RETRY_CONNECT_MS = 1500
SERIAL_WAIT_LOG_MS = 5000


@dataclass
class SerialEvent:
    label: str
    wall_ts: float
    elapsed_s: float
    text: str


def debug_text(raw: bytes) -> str:
    chunks: list[str] = []
    for byte in raw:
        if byte in (10, 13):
            continue
        if 32 <= byte <= 126:
            chunks.append(chr(byte))
        else:
            chunks.append(f"\\x{byte:02X}")
    return "".join(chunks).strip()


def now_label(ts: float) -> str:
    local = time.localtime(ts)
    millis = int((ts - int(ts)) * 1000)
    return time.strftime("%H:%M:%S", local) + f".{millis:03d}"


def list_available_ports_text() -> str:
    ports = sorted(list_ports.comports(), key=lambda p: p.device)
    if not ports:
        return "No se detectaron puertos seriales."

    lines = ["Puertos detectados:"]
    for port in ports:
        desc = f" - {port.device}"
        if port.description:
            desc += f" | {port.description}"
        lines.append(desc)
    return "\n".join(lines)


def list_available_port_devices() -> list[str]:
    return [port.device for port in sorted(list_ports.comports(), key=lambda p: p.device)]


class SerialReader(threading.Thread):
    def __init__(
        self,
        *,
        label: str,
        port: str,
        baudrate: int,
        start_monotonic: float,
        out_queue: "queue.Queue[SerialEvent]",
        stop_event: threading.Event,
    ) -> None:
        super().__init__(daemon=True)
        self.label = label
        self.port = port
        self.baudrate = baudrate
        self.start_monotonic = start_monotonic
        self.out_queue = out_queue
        self.stop_event = stop_event
        self.serial_handle: Optional[serial.Serial] = None
        self.was_connected = False
        self.last_wait_notice_monotonic = 0.0

    def emit(self, text: str) -> None:
        self.out_queue.put(
            SerialEvent(
                label=self.label,
                wall_ts=time.time(),
                elapsed_s=time.monotonic() - self.start_monotonic,
                text=text,
            )
        )

    def try_open(self) -> bool:
        try:
            self.serial_handle = serial.Serial(self.port, self.baudrate, timeout=0.2)
            self.was_connected = True
            self.last_wait_notice_monotonic = 0.0
            self.emit(f"[MONITOR] Conectado a {self.port} @ {self.baudrate}")
            return True
        except Exception as exc:
            now_mono = time.monotonic()
            if (now_mono - self.last_wait_notice_monotonic) >= (SERIAL_WAIT_LOG_MS / 1000.0):
                self.last_wait_notice_monotonic = now_mono
                self.emit(f"[MONITOR][WAIT] {self.port} no disponible aun, reintentando... ({exc})")
            self.serial_handle = None
            return False

    def run(self) -> None:
        while not self.stop_event.is_set():
            if not self.serial_handle or not self.serial_handle.is_open:
                if not self.try_open():
                    if self.stop_event.wait(SERIAL_RETRY_CONNECT_MS / 1000.0):
                        break
                    continue

            try:
                raw = self.serial_handle.readline()
            except Exception as exc:
                if self.was_connected:
                    self.emit(f"[MONITOR][WAIT] Conexion perdida en {self.port}, esperando reconexion... ({exc})")
                self.close()
                if self.stop_event.wait(SERIAL_RETRY_CONNECT_MS / 1000.0):
                    break
                continue

            if not raw:
                continue

            text = debug_text(raw)
            if not text:
                continue

            self.emit(text)

        self.close()

    def close(self) -> None:
        if self.serial_handle and self.serial_handle.is_open:
            try:
                self.serial_handle.close()
            except Exception:
                pass
        self.serial_handle = None


class SerialMonitorSession:
    def __init__(self, port_map: dict[str, tuple[str, int]], log_path: Optional[str] = None) -> None:
        self.port_map = port_map
        self.log_path = log_path
        self.event_queue: "queue.Queue[SerialEvent]" = queue.Queue()
        self.stop_event = threading.Event()
        self.start_monotonic = 0.0
        self.readers: list[SerialReader] = []
        self.log_file = None

    @property
    def running(self) -> bool:
        return any(reader.is_alive() for reader in self.readers) and not self.stop_event.is_set()

    def start(self) -> None:
        self.start_monotonic = time.monotonic()
        self.stop_event.clear()
        self.log_file = open_log(self.log_path)
        self.readers = [
            SerialReader(
                label=label,
                port=port,
                baudrate=baud,
                start_monotonic=self.start_monotonic,
                out_queue=self.event_queue,
                stop_event=self.stop_event,
            )
            for label, (port, baud) in self.port_map.items()
        ]
        for reader in self.readers:
            reader.start()

    def stop(self) -> None:
        self.stop_event.set()
        for reader in self.readers:
            reader.close()
        for reader in self.readers:
            reader.join(timeout=1.0)
        self.readers = []
        if self.log_file:
            self.log_file.close()
            self.log_file = None

    def drain_events(self) -> list[SerialEvent]:
        events: list[SerialEvent] = []
        while True:
            try:
                event = self.event_queue.get_nowait()
            except queue.Empty:
                break
            events.append(event)
            if self.log_file:
                self.log_file.write(format_event(event) + "\n")
                self.log_file.flush()
        return events


def open_log(path_str: Optional[str]):
    if not path_str:
        return None
    path = Path(path_str).expanduser()
    path.parent.mkdir(parents=True, exist_ok=True)
    return path.open("a", encoding="utf-8")


def format_event(event: SerialEvent) -> str:
    return f"{now_label(event.wall_ts)} | +{event.elapsed_s:8.3f}s | {event.label:<{LABEL_WIDTH}} | {event.text}"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Monitor serial triple para Panel / Central / Maestro.")
    parser.add_argument("--gui", action="store_true", help="Fuerza el modo app")
    parser.add_argument("--panel", help="Puerto serial del panel. Ej: COM7")
    parser.add_argument("--central", help="Puerto serial de la central. Ej: COM8")
    parser.add_argument("--maestro", help="Puerto serial del maestro. Ej: COM9")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Baudios por defecto para los tres puertos")
    parser.add_argument("--panel-baud", type=int, help="Baudios del panel")
    parser.add_argument("--central-baud", type=int, help="Baudios de la central")
    parser.add_argument("--maestro-baud", type=int, help="Baudios del maestro")
    parser.add_argument("--log", help="Archivo opcional para guardar todo el monitoreo")
    parser.add_argument("--show-ports", action="store_true", help="Muestra puertos detectados antes de iniciar")
    return parser


class SerialMonitorApp:
    def __init__(self, root: tk.Tk, args: argparse.Namespace) -> None:
        self.root = root
        self.args = args
        self.root.title(APP_TITLE)
        self.root.geometry("1500x920")
        self.root.minsize(1200, 720)

        self.session: Optional[SerialMonitorSession] = None

        self.panel_port_var = tk.StringVar(value=args.panel or "")
        self.central_port_var = tk.StringVar(value=args.central or "")
        self.maestro_port_var = tk.StringVar(value=args.maestro or "")
        self.panel_baud_var = tk.StringVar(value=str(args.panel_baud or args.baud or DEFAULT_BAUD))
        self.central_baud_var = tk.StringVar(value=str(args.central_baud or args.baud or DEFAULT_BAUD))
        self.maestro_baud_var = tk.StringVar(value=str(args.maestro_baud or args.baud or DEFAULT_BAUD))
        self.log_path_var = tk.StringVar(value=args.log or "")
        self.status_var = tk.StringVar(value="Listo para conectar.")

        self.port_values = list_available_port_devices()
        self.widgets_by_label: dict[str, ScrolledText] = {}

        self._build_ui()
        self.refresh_ports()
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.root.after(120, self.poll_session)

    def _build_ui(self) -> None:
        self.root.configure(bg="#101218")

        style = ttk.Style()
        try:
            style.theme_use("clam")
        except Exception:
            pass

        top = ttk.Frame(self.root, padding=12)
        top.pack(fill="x")
        top.columnconfigure(1, weight=1)
        top.columnconfigure(4, weight=1)
        top.columnconfigure(7, weight=1)

        row = 0
        ttk.Label(top, text="Panel").grid(row=row, column=0, sticky="w", padx=(0, 6))
        self.panel_combo = ttk.Combobox(top, textvariable=self.panel_port_var, values=self.port_values, width=18)
        self.panel_combo.grid(row=row, column=1, sticky="ew", padx=(0, 6))
        ttk.Entry(top, textvariable=self.panel_baud_var, width=10).grid(row=row, column=2, sticky="w", padx=(0, 14))

        ttk.Label(top, text="Central").grid(row=row, column=3, sticky="w", padx=(0, 6))
        self.central_combo = ttk.Combobox(top, textvariable=self.central_port_var, values=self.port_values, width=18)
        self.central_combo.grid(row=row, column=4, sticky="ew", padx=(0, 6))
        ttk.Entry(top, textvariable=self.central_baud_var, width=10).grid(row=row, column=5, sticky="w", padx=(0, 14))

        ttk.Label(top, text="Maestro").grid(row=row, column=6, sticky="w", padx=(0, 6))
        self.maestro_combo = ttk.Combobox(top, textvariable=self.maestro_port_var, values=self.port_values, width=18)
        self.maestro_combo.grid(row=row, column=7, sticky="ew", padx=(0, 6))
        ttk.Entry(top, textvariable=self.maestro_baud_var, width=10).grid(row=row, column=8, sticky="w")

        row += 1
        ttk.Label(top, text="Log").grid(row=row, column=0, sticky="w", pady=(10, 0), padx=(0, 6))
        ttk.Entry(top, textvariable=self.log_path_var).grid(row=row, column=1, columnspan=6, sticky="ew", pady=(10, 0))
        ttk.Button(top, text="Elegir...", command=self.choose_log_file).grid(row=row, column=7, sticky="ew", pady=(10, 0), padx=(6, 6))
        ttk.Button(top, text="Refrescar puertos", command=self.refresh_ports).grid(row=row, column=8, sticky="ew", pady=(10, 0))

        row += 1
        buttons = ttk.Frame(top)
        buttons.grid(row=row, column=0, columnspan=9, sticky="ew", pady=(12, 0))
        buttons.columnconfigure(4, weight=1)

        self.connect_btn = ttk.Button(buttons, text="Conectar", command=self.connect_ports)
        self.connect_btn.grid(row=0, column=0, padx=(0, 6))
        self.disconnect_btn = ttk.Button(buttons, text="Desconectar", command=self.disconnect_ports, state="disabled")
        self.disconnect_btn.grid(row=0, column=1, padx=(0, 6))
        ttk.Button(buttons, text="Limpiar consola", command=self.clear_texts).grid(row=0, column=2, padx=(0, 6))
        ttk.Button(buttons, text="Puertos detectados", command=self.show_ports_dialog).grid(row=0, column=3, padx=(0, 6))
        ttk.Label(buttons, textvariable=self.status_var).grid(row=0, column=4, sticky="e")

        body = ttk.Panedwindow(self.root, orient="vertical")
        body.pack(fill="both", expand=True, padx=12, pady=(0, 12))

        combined_frame = ttk.Labelframe(body, text="Consola combinada", padding=8)
        body.add(combined_frame, weight=3)
        self.combined_text = ScrolledText(combined_frame, wrap="none", font=("Consolas", 10))
        self.combined_text.pack(fill="both", expand=True)
        self.combined_text.tag_configure("PANEL", foreground="#60a5fa")
        self.combined_text.tag_configure("CENTRAL", foreground="#f59e0b")
        self.combined_text.tag_configure("MAESTRO", foreground="#34d399")
        self.combined_text.tag_configure("ERROR", foreground="#ef4444")

        lower = ttk.Frame(body)
        lower.columnconfigure(0, weight=1)
        lower.columnconfigure(1, weight=1)
        lower.columnconfigure(2, weight=1)
        body.add(lower, weight=2)

        self.widgets_by_label = {
            "PANEL": self._make_port_text(lower, 0, "Panel"),
            "CENTRAL": self._make_port_text(lower, 1, "Central"),
            "MAESTRO": self._make_port_text(lower, 2, "Maestro"),
        }

    def _make_port_text(self, parent: ttk.Frame, column: int, title: str):
        frame = ttk.Labelframe(parent, text=title, padding=8)
        frame.grid(row=0, column=column, sticky="nsew", padx=(0 if column == 0 else 6, 0), pady=0)
        text = ScrolledText(frame, wrap="none", font=("Consolas", 10), height=16)
        text.pack(fill="both", expand=True)
        return text

    def choose_log_file(self) -> None:
        if not filedialog:
            return
        path = filedialog.asksaveasfilename(
            title="Guardar log serial",
            defaultextension=".txt",
            filetypes=[("Texto", "*.txt"), ("Log", "*.log"), ("Todos", "*.*")],
        )
        if path:
            self.log_path_var.set(path)

    def show_ports_dialog(self) -> None:
        if messagebox:
            messagebox.showinfo("Puertos detectados", list_available_ports_text())

    def refresh_ports(self) -> None:
        self.port_values = list_available_port_devices()
        for combo in (self.panel_combo, self.central_combo, self.maestro_combo):
            combo["values"] = self.port_values
        if not self.port_values:
            self.status_var.set("No se detectaron puertos seriales.")
        else:
            self.status_var.set(f"Puertos detectados: {', '.join(self.port_values)}")

    def parse_baud(self, value: str, label: str) -> int:
        try:
            baud = int(str(value).strip())
        except Exception as exc:
            raise ValueError(f"Baudios invalidos para {label}") from exc
        if baud <= 0:
            raise ValueError(f"Baudios invalidos para {label}")
        return baud

    def build_port_map(self) -> dict[str, tuple[str, int]]:
        port_map = {
            "PANEL": (self.panel_port_var.get().strip(), self.parse_baud(self.panel_baud_var.get(), "Panel")),
            "CENTRAL": (self.central_port_var.get().strip(), self.parse_baud(self.central_baud_var.get(), "Central")),
            "MAESTRO": (self.maestro_port_var.get().strip(), self.parse_baud(self.maestro_baud_var.get(), "Maestro")),
        }

        missing = [label for label, (port, _baud) in port_map.items() if not port]
        if missing:
            raise ValueError(f"Faltan puertos: {', '.join(missing)}")

        ports = [port for port, _baud in port_map.values()]
        duplicated = sorted({port for port in ports if ports.count(port) > 1})
        if duplicated:
            raise ValueError(f"Hay puertos repetidos: {', '.join(duplicated)}")

        return port_map

    def connect_ports(self) -> None:
        try:
            port_map = self.build_port_map()
        except ValueError as exc:
            if messagebox:
                messagebox.showerror("Configuracion invalida", str(exc))
            else:
                self.status_var.set(str(exc))
            return

        self.disconnect_ports()
        self.clear_texts()

        self.session = SerialMonitorSession(port_map=port_map, log_path=self.log_path_var.get().strip() or None)
        self.session.start()
        self.connect_btn.config(state="disabled")
        self.disconnect_btn.config(state="normal")
        self.status_var.set("Conectando...")

    def disconnect_ports(self) -> None:
        if self.session:
            self.session.stop()
            self.session = None
        self.connect_btn.config(state="normal")
        self.disconnect_btn.config(state="disabled")
        self.status_var.set("Desconectado.")

    def append_line(self, widget: ScrolledText, line: str, tag: Optional[str] = None) -> None:
        widget.insert("end", line + "\n", tag or ())
        widget.see("end")

    def poll_session(self) -> None:
        if self.session:
            for event in self.session.drain_events():
                line = format_event(event)
                tag = "ERROR" if "[ERROR]" in event.text else event.label
                self.append_line(self.combined_text, line, tag)
                short_line = f"{now_label(event.wall_ts)} | +{event.elapsed_s:8.3f}s | {event.text}"
                port_text = self.widgets_by_label.get(event.label)
                if port_text:
                    self.append_line(port_text, short_line)

            if self.session and not self.session.running and self.session.stop_event.is_set():
                self.status_var.set("Sesion detenida.")
                self.connect_btn.config(state="normal")
                self.disconnect_btn.config(state="disabled")

        self.root.after(120, self.poll_session)

    def clear_texts(self) -> None:
        for widget in [self.combined_text, *self.widgets_by_label.values()]:
            widget.delete("1.0", "end")

    def on_close(self) -> None:
        self.disconnect_ports()
        self.root.destroy()


def run_gui(args: argparse.Namespace) -> int:
    if tk is None or ttk is None or ScrolledText is None:
        print("Tkinter no esta disponible en este Python.", file=sys.stderr)
        return 3

    root = tk.Tk()
    app = SerialMonitorApp(root, args)
    root.mainloop()
    return 0


def run_cli(args: argparse.Namespace) -> int:
    if args.show_ports:
        print(list_available_ports_text())
        print("")

    required = [args.panel, args.central, args.maestro]
    if not all(required):
        print("En modo consola debes indicar --panel, --central y --maestro.", file=sys.stderr)
        return 2

    port_map = {
        "PANEL": (args.panel, args.panel_baud or args.baud or DEFAULT_BAUD),
        "CENTRAL": (args.central, args.central_baud or args.baud or DEFAULT_BAUD),
        "MAESTRO": (args.maestro, args.maestro_baud or args.baud or DEFAULT_BAUD),
    }

    ports = [port for port, _baud in port_map.values()]
    duplicated = sorted({port for port in ports if ports.count(port) > 1})
    if duplicated:
        print(f"Hay puertos repetidos: {', '.join(duplicated)}", file=sys.stderr)
        return 2

    print(list_available_ports_text())
    print("")
    print("Monitoreando Panel / Central / Maestro. Ctrl+C para salir.")
    print("-" * 110)

    session = SerialMonitorSession(port_map=port_map, log_path=args.log)
    session.start()

    def stop_all(*_args) -> None:
        if session:
            session.stop_event.set()

    signal.signal(signal.SIGINT, stop_all)
    signal.signal(signal.SIGTERM, stop_all)

    try:
        while not session.stop_event.is_set():
            events = session.drain_events()
            for event in events:
                print(format_event(event), flush=True)
            time.sleep(0.08)
    finally:
        session.stop()

    return 0


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    wants_cli = any([args.panel, args.central, args.maestro]) and not args.gui
    if wants_cli:
        return run_cli(args)
    return run_gui(args)


if __name__ == "__main__":
    raise SystemExit(main())
