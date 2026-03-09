#!/usr/bin/env python3
"""
ESP Bridge Debug Monitor GUI

A modern monitoring application for ESP-NOW debug data.
Reads serial output from the ESP32-C3 Debug Monitor and displays
data in tables and live graphs, categorized by robot.

Expected serial input format: [MAC_ADDRESS] key: value
""" 

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import serial
import serial.tools.list_ports
import threading
import time
import csv
import re
from collections import deque


# ---------------------------------------------------------------------------
# Data Manager
# ---------------------------------------------------------------------------
class DataManager:
    """Stores and manages debug data with min/max tracking and type detection."""

    def __init__(self, max_history=2000):
        self.max_history = max_history
        self.lock = threading.Lock()
        # Structure: {robot_id: {key: {values, timestamps, min, max, current, type}}}
        self.data = {"Robot 1": {}, "Robot 2": {}}
        self.mac_to_robot: dict[str, str] = {}
        self.update_callbacks: list = []

    # -- callbacks ----------------------------------------------------------
    def on_update(self, callback):
        self.update_callbacks.append(callback)

    def _notify(self):
        for cb in self.update_callbacks:
            try:
                cb()
            except Exception:
                pass

    # -- robot assignment ---------------------------------------------------
    def assign_robot(self, mac: str) -> str:
        if mac in self.mac_to_robot:
            return self.mac_to_robot[mac]
        robot1_macs = [m for m, r in self.mac_to_robot.items() if r == "Robot 1"]
        if len(robot1_macs) == 0:
            self.mac_to_robot[mac] = "Robot 1"
        else:
            self.mac_to_robot[mac] = "Robot 2"
        return self.mac_to_robot[mac]

    def set_mac_robot(self, mac: str, robot_id: str):
        """Manually assign a MAC to a robot group."""
        with self.lock:
            old = self.mac_to_robot.get(mac)
            if old == robot_id:
                return
            self.mac_to_robot[mac] = robot_id

    # -- type detection -----------------------------------------------------
    @staticmethod
    def detect_type(value_str: str):
        s = value_str.strip()
        if s.lower() in ("true", "false"):
            return "bool", s.lower() == "true"
        try:
            return "integer", int(s)
        except ValueError:
            pass
        try:
            return "double", float(s)
        except ValueError:
            pass
        return "string", s

    # -- data ingestion -----------------------------------------------------
    def add_data(self, mac: str, key: str, value_str: str):
        with self.lock:
            robot_id = self.assign_robot(mac)
            dtype, value = self.detect_type(value_str)

            if key not in self.data[robot_id]:
                self.data[robot_id][key] = {
                    "values": deque(maxlen=self.max_history),
                    "timestamps": deque(maxlen=self.max_history),
                    "min": None,
                    "max": None,
                    "current": None,
                    "type": dtype,
                }

            entry = self.data[robot_id][key]
            entry["current"] = value
            entry["type"] = dtype
            now = time.time()
            entry["timestamps"].append(now)

            if dtype in ("integer", "double"):
                entry["values"].append(value)
                if entry["min"] is None or value < entry["min"]:
                    entry["min"] = value
                if entry["max"] is None or value > entry["max"]:
                    entry["max"] = value
            elif dtype == "bool":
                entry["values"].append(1 if value else 0)
                entry["min"] = False
                entry["max"] = True
            else:
                entry["values"].append(value)
                entry["min"] = "-"
                entry["max"] = "-"
        self._notify()

    # -- queries ------------------------------------------------------------
    def get_table_data(self, robot_id: str) -> list[dict]:
        with self.lock:
            rows = []
            for key, e in self.data.get(robot_id, {}).items():
                rows.append({
                    "name": key,
                    "value": e["current"],
                    "min": e["min"],
                    "max": e["max"],
                    "type": e["type"],
                })
            return rows

    def get_graph_data(self, robot_id: str) -> dict:
        with self.lock:
            result = {}
            for key, e in self.data.get(robot_id, {}).items():
                if e["type"] in ("integer", "double", "bool"):
                    result[key] = {
                        "values": list(e["values"]),
                        "timestamps": list(e["timestamps"]),
                    }
            return result

    def get_numeric_keys(self, robot_id: str) -> list[str]:
        with self.lock:
            return [
                k for k, e in self.data.get(robot_id, {}).items()
                if e["type"] in ("integer", "double", "bool")
            ]

    def reset(self):
        with self.lock:
            self.data = {"Robot 1": {}, "Robot 2": {}}
            self.mac_to_robot.clear()
        self._notify()

    def get_known_macs(self) -> dict[str, str]:
        with self.lock:
            return dict(self.mac_to_robot)


# ---------------------------------------------------------------------------
# Serial Reader Thread
# ---------------------------------------------------------------------------
class SerialReader(threading.Thread):
    """Background thread reading the debug monitor serial port."""

    # Pattern: [MAC_ADDR] payload
    LINE_RE = re.compile(r"\[([0-9A-Fa-f:]{17})\]\s+(.*)")

    def __init__(self, data_manager: DataManager):
        super().__init__(daemon=True)
        self.dm = data_manager
        self.ser: serial.Serial | None = None
        self.running = False
        self.raw_callbacks: list = []

    def on_raw_line(self, callback):
        self.raw_callbacks.append(callback)

    def connect(self, port: str, baud: int) -> str | None:
        """Returns None on success or an error message."""
        try:
            self.ser = serial.Serial(port, baud, timeout=0.5)
            self.running = True
            return None
        except Exception as exc:
            return str(exc)

    def disconnect(self):
        self.running = False
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    @property
    def is_connected(self) -> bool:
        return self.running and self.ser is not None and self.ser.is_open

    def _parse(self, line: str):
        m = self.LINE_RE.match(line)
        if not m:
            return
        mac = m.group(1)
        payload = m.group(2).strip()

        # Try  key: value
        if ":" in payload:
            key, _, val = payload.partition(":")
            key, val = key.strip(), val.strip()
            if key and val:
                self.dm.add_data(mac, key, val)
                return

        # Try  key=value
        if "=" in payload:
            key, _, val = payload.partition("=")
            key, val = key.strip(), val.strip()
            if key and val:
                self.dm.add_data(mac, key, val)
                return

        # Fallback: last token is value
        parts = payload.rsplit(None, 1)
        if len(parts) == 2:
            self.dm.add_data(mac, parts[0], parts[1])
        else:
            self.dm.add_data(mac, "message", payload)

    def run(self):
        while self.running:
            try:
                if self.ser and self.ser.is_open:
                    raw = self.ser.readline()
                    if raw:
                        line = raw.decode("utf-8", errors="replace").strip()
                        if line:
                            for cb in self.raw_callbacks:
                                try:
                                    cb(line)
                                except Exception:
                                    pass
                            self._parse(line)
                else:
                    time.sleep(0.1)
            except serial.SerialException:
                self.running = False
                break
            except Exception:
                time.sleep(0.05)


# ---------------------------------------------------------------------------
# GUI Application
# ---------------------------------------------------------------------------

# Colour palette
BG_DARK = "#1e1e2e"
BG_MID = "#2a2a3d"
BG_LIGHT = "#363650"
FG = "#cdd6f4"
ACCENT = "#89b4fa"
ACCENT2 = "#a6e3a1"
RED = "#f38ba8"
YELLOW = "#f9e2af"


class MonitorApp(tk.Tk):
    """Main application window."""

    def __init__(self):
        super().__init__()
        self.title("ESP Bridge Debug Monitor")
        self.geometry("1100x720")
        self.minsize(900, 600)
        self.configure(bg=BG_DARK)

        self.dm = DataManager()
        self.reader = SerialReader(self.dm)
        self.current_robot = tk.StringVar(value="Robot 1")

        # Track visibility of graph lines {key: BooleanVar}
        self.line_vars: dict[str, tk.BooleanVar] = {}
        # Graph data window (seconds)
        self.graph_window = tk.DoubleVar(value=30.0)

        self._apply_style()
        self._build_ui()

        # Periodic UI refresh
        self._schedule_refresh()

        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # -- styling ------------------------------------------------------------
    def _apply_style(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure(".", background=BG_DARK, foreground=FG,
                         fieldbackground=BG_MID, borderwidth=0)
        style.configure("TNotebook", background=BG_DARK)
        style.configure("TNotebook.Tab", background=BG_MID, foreground=FG,
                         padding=[14, 6])
        style.map("TNotebook.Tab",
                  background=[("selected", ACCENT)],
                  foreground=[("selected", BG_DARK)])
        style.configure("TFrame", background=BG_DARK)
        style.configure("TLabel", background=BG_DARK, foreground=FG)
        style.configure("TButton", background=ACCENT, foreground=BG_DARK,
                         padding=[10, 4])
        style.map("TButton",
                  background=[("active", ACCENT2)])
        style.configure("Accent.TButton", background=ACCENT2, foreground=BG_DARK)
        style.configure("Danger.TButton", background=RED, foreground=BG_DARK)
        style.configure("Robot.TButton", background=BG_LIGHT, foreground=FG,
                         padding=[12, 6], font=("Segoe UI", 11, "bold"))
        style.map("Robot.TButton",
                  background=[("active", ACCENT)])
        style.configure("Treeview", background=BG_MID, foreground=FG,
                         fieldbackground=BG_MID, rowheight=28)
        style.configure("Treeview.Heading", background=BG_LIGHT, foreground=FG,
                         font=("Segoe UI", 10, "bold"))
        style.map("Treeview", background=[("selected", ACCENT)],
                  foreground=[("selected", BG_DARK)])
        style.configure("TLabelframe", background=BG_DARK, foreground=ACCENT)
        style.configure("TLabelframe.Label", background=BG_DARK, foreground=ACCENT,
                         font=("Segoe UI", 10, "bold"))
        style.configure("TCombobox", fieldbackground=BG_MID, foreground=FG)
        style.configure("TCheckbutton", background=BG_DARK, foreground=FG)
        style.map("TCheckbutton",
                  background=[("active", BG_DARK)])
        style.configure("Horizontal.TScale", background=BG_DARK,
                         troughcolor=BG_MID)

    # -- UI construction ----------------------------------------------------
    def _build_ui(self):
        # Robot selector bar
        top = ttk.Frame(self)
        top.pack(fill="x", padx=10, pady=(10, 0))
        ttk.Label(top, text="ESP Bridge Debug Monitor",
                  font=("Segoe UI", 16, "bold"), foreground=ACCENT).pack(side="left")

        self.robot_btn = ttk.Button(top, text="⮂  Robot 1",
                                    style="Robot.TButton",
                                    command=self._toggle_robot)
        self.robot_btn.pack(side="right")

        # Notebook
        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True, padx=10, pady=10)

        self._build_settings_tab()
        self._build_table_tab()
        self._build_monitor_tab()

    # -- Settings tab -------------------------------------------------------
    def _build_settings_tab(self):
        frm = ttk.Frame(self.nb)
        self.nb.add(frm, text="  ⚙  Einstellungen  ")

        # Connection group
        conn = ttk.LabelFrame(frm, text="Verbindung", padding=16)
        conn.pack(fill="x", padx=20, pady=(20, 10))

        row1 = ttk.Frame(conn)
        row1.pack(fill="x", pady=4)
        ttk.Label(row1, text="Port:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_cb = ttk.Combobox(row1, textvariable=self.port_var, width=25,
                                     state="readonly")
        self.port_cb.pack(side="left", padx=(8, 4))
        ttk.Button(row1, text="↻", width=3,
                   command=self._refresh_ports).pack(side="left")

        row2 = ttk.Frame(conn)
        row2.pack(fill="x", pady=4)
        ttk.Label(row2, text="Baud:").pack(side="left")
        self.baud_var = tk.StringVar(value="115200")
        ttk.Combobox(row2, textvariable=self.baud_var, width=10,
                     values=["9600", "19200", "38400", "57600", "115200",
                             "230400", "460800", "921600"],
                     state="readonly").pack(side="left", padx=(8, 0))

        row3 = ttk.Frame(conn)
        row3.pack(fill="x", pady=(10, 0))
        self.connect_btn = ttk.Button(row3, text="Verbinden",
                                       command=self._toggle_connection)
        self.connect_btn.pack(side="left")
        self.status_lbl = ttk.Label(row3, text="● Getrennt", foreground=RED,
                                     font=("Segoe UI", 10, "bold"))
        self.status_lbl.pack(side="left", padx=16)

        # MAC assignment group
        mac_frm = ttk.LabelFrame(frm, text="MAC → Roboter Zuordnung", padding=16)
        mac_frm.pack(fill="x", padx=20, pady=10)
        self.mac_list_frame = ttk.Frame(mac_frm)
        self.mac_list_frame.pack(fill="x")
        self.mac_info_lbl = ttk.Label(mac_frm,
                                       text="MAC-Adressen werden automatisch zugeordnet.\n"
                                            "Die erste erkannte MAC → Robot 1, die zweite → Robot 2.")
        self.mac_info_lbl.pack(anchor="w", pady=(8, 0))

        # Data group
        data_frm = ttk.LabelFrame(frm, text="Daten", padding=16)
        data_frm.pack(fill="x", padx=20, pady=10)
        ttk.Button(data_frm, text="Alle Daten zurücksetzen",
                   style="Danger.TButton",
                   command=self._reset_data).pack(anchor="w")

        self._refresh_ports()

    # -- Table tab ----------------------------------------------------------
    def _build_table_tab(self):
        frm = ttk.Frame(self.nb)
        self.nb.add(frm, text="  📋  Tabelle  ")

        toolbar = ttk.Frame(frm)
        toolbar.pack(fill="x", padx=10, pady=(10, 4))
        ttk.Button(toolbar, text="CSV Export",
                   style="Accent.TButton",
                   command=self._export_csv).pack(side="right")
        self.table_robot_lbl = ttk.Label(toolbar,
                                          text="Robot 1",
                                          font=("Segoe UI", 12, "bold"),
                                          foreground=ACCENT)
        self.table_robot_lbl.pack(side="left")

        cols = ("name", "type", "value", "min", "max")
        self.tree = ttk.Treeview(frm, columns=cols, show="headings",
                                  selectmode="browse")
        self.tree.heading("name", text="Name")
        self.tree.heading("type", text="Typ")
        self.tree.heading("value", text="Wert")
        self.tree.heading("min", text="Min")
        self.tree.heading("max", text="Max")
        self.tree.column("name", width=200)
        self.tree.column("type", width=80, anchor="center")
        self.tree.column("value", width=160, anchor="center")
        self.tree.column("min", width=120, anchor="center")
        self.tree.column("max", width=120, anchor="center")

        sb = ttk.Scrollbar(frm, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.pack(fill="both", expand=True, padx=(10, 0), pady=(0, 10))
        sb.pack(side="right", fill="y", padx=(0, 10), pady=(0, 10))

    # -- Monitor tab --------------------------------------------------------
    def _build_monitor_tab(self):
        # Import here so missing matplotlib doesn't break the rest
        from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
        from matplotlib.figure import Figure

        frm = ttk.Frame(self.nb)
        self.nb.add(frm, text="  📈  Monitor  ")

        # Controls
        ctrl = ttk.Frame(frm)
        ctrl.pack(fill="x", padx=10, pady=(10, 2))

        self.monitor_robot_lbl = ttk.Label(ctrl,
                                            text="Robot 1",
                                            font=("Segoe UI", 12, "bold"),
                                            foreground=ACCENT)
        self.monitor_robot_lbl.pack(side="left")

        # Slider for data window
        slider_frm = ttk.Frame(ctrl)
        slider_frm.pack(side="right")
        ttk.Label(slider_frm, text="Zeitfenster:").pack(side="left")
        self.window_slider = ttk.Scale(slider_frm, from_=5, to=120,
                                        orient="horizontal", length=180,
                                        variable=self.graph_window)
        self.window_slider.pack(side="left", padx=4)
        self.window_lbl = ttk.Label(slider_frm, text="30 s", width=6)
        self.window_lbl.pack(side="left")

        # Checkboxes for line visibility
        self.check_frame = ttk.LabelFrame(frm, text="Datenströme", padding=6)
        self.check_frame.pack(fill="x", padx=10, pady=(2, 4))
        self.check_inner = ttk.Frame(self.check_frame)
        self.check_inner.pack(fill="x")

        # Matplotlib figure
        self.fig = Figure(figsize=(10, 4), dpi=100, facecolor=BG_DARK)
        self.ax = self.fig.add_subplot(111)
        self._style_axis()

        self.canvas = FigureCanvasTkAgg(self.fig, master=frm)
        self.canvas.get_tk_widget().pack(fill="both", expand=True, padx=10,
                                          pady=(0, 10))

    def _style_axis(self):
        self.ax.set_facecolor(BG_MID)
        self.ax.tick_params(colors=FG, labelsize=8)
        self.ax.spines["bottom"].set_color(BG_LIGHT)
        self.ax.spines["left"].set_color(BG_LIGHT)
        self.ax.spines["top"].set_visible(False)
        self.ax.spines["right"].set_visible(False)
        self.ax.set_xlabel("Zeit (s)", color=FG, fontsize=9)
        self.ax.set_ylabel("Wert", color=FG, fontsize=9)
        self.ax.grid(True, color=BG_LIGHT, alpha=0.5, linewidth=0.5)

    # -- actions ------------------------------------------------------------
    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_cb["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def _toggle_connection(self):
        if self.reader.is_connected:
            self.reader.disconnect()
            self.connect_btn.configure(text="Verbinden")
            self.status_lbl.configure(text="● Getrennt", foreground=RED)
        else:
            port = self.port_var.get()
            baud = int(self.baud_var.get())
            if not port:
                messagebox.showwarning("Kein Port",
                                       "Bitte wählen Sie einen seriellen Port.")
                return
            # Create a fresh reader thread (threads cannot be restarted)
            self.reader = SerialReader(self.dm)
            err = self.reader.connect(port, baud)
            if err:
                messagebox.showerror("Verbindungsfehler", err)
                return
            self.reader.start()
            self.connect_btn.configure(text="Trennen")
            self.status_lbl.configure(text="● Verbunden", foreground=ACCENT2)

    def _toggle_robot(self):
        cur = self.current_robot.get()
        new = "Robot 2" if cur == "Robot 1" else "Robot 1"
        self.current_robot.set(new)
        self.robot_btn.configure(text=f"⮂  {new}")
        self.table_robot_lbl.configure(text=new)
        self.monitor_robot_lbl.configure(text=new)
        # Reset line visibility checkboxes
        self.line_vars.clear()
        for w in self.check_inner.winfo_children():
            w.destroy()

    def _reset_data(self):
        if messagebox.askyesno("Daten zurücksetzen",
                               "Alle gesammelten Daten löschen?"):
            self.dm.reset()
            self.line_vars.clear()
            for w in self.check_inner.winfo_children():
                w.destroy()

    def _export_csv(self):
        robot = self.current_robot.get()
        data = self.dm.get_table_data(robot)
        if not data:
            messagebox.showinfo("Keine Daten",
                                f"Keine Daten für {robot} vorhanden.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV Dateien", "*.csv"), ("Alle Dateien", "*.*")],
            title="Tabelle exportieren",
            initialfile=f"{robot.replace(' ', '_')}_export.csv",
        )
        if not path:
            return
        with open(path, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f, delimiter=";")
            writer.writerow(["Name", "Typ", "Wert", "Min", "Max"])
            for row in data:
                writer.writerow([
                    row["name"], row["type"], row["value"],
                    row["min"], row["max"],
                ])
        messagebox.showinfo("Export", f"Daten nach {path} exportiert.")

    # -- periodic refresh ---------------------------------------------------
    def _schedule_refresh(self):
        self._refresh_table()
        self._refresh_graph()
        self._refresh_slider_label()
        self._refresh_mac_list()
        self.after(500, self._schedule_refresh)

    def _refresh_table(self):
        robot = self.current_robot.get()
        data = self.dm.get_table_data(robot)
        # Keep existing items to reduce flicker
        existing = {self.tree.item(iid, "values")[0]: iid
                    for iid in self.tree.get_children()}
        seen = set()
        for row in data:
            name = row["name"]
            vals = (name, row["type"],
                    self._fmt(row["value"]),
                    self._fmt(row["min"]),
                    self._fmt(row["max"]))
            if name in existing:
                self.tree.item(existing[name], values=vals)
            else:
                self.tree.insert("", "end", values=vals)
            seen.add(name)
        # Remove rows no longer present
        for name, iid in existing.items():
            if name not in seen:
                self.tree.delete(iid)

    @staticmethod
    def _fmt(v):
        if v is None:
            return "-"
        if isinstance(v, float):
            return f"{v:.4f}"
        if isinstance(v, bool):
            return "True" if v else "False"
        return str(v)

    def _refresh_graph(self):
        robot = self.current_robot.get()
        gdata = self.dm.get_graph_data(robot)
        window = self.graph_window.get()

        # Update checkboxes for new keys
        for key in gdata:
            if key not in self.line_vars:
                var = tk.BooleanVar(value=True)
                self.line_vars[key] = var
                cb = ttk.Checkbutton(self.check_inner, text=key, variable=var)
                cb.pack(side="left", padx=6)

        self.ax.clear()
        self._style_axis()

        now = time.time()
        colors = ["#89b4fa", "#a6e3a1", "#f9e2af", "#f38ba8",
                  "#cba6f7", "#fab387", "#94e2d5", "#f5c2e7",
                  "#74c7ec", "#b4befe"]

        color_idx = 0
        for key, series in gdata.items():
            if key in self.line_vars and not self.line_vars[key].get():
                color_idx += 1
                continue
            ts = series["timestamps"]
            vs = series["values"]
            if not ts:
                color_idx += 1
                continue
            # Filter to window
            rel = [t - now for t in ts]
            pairs = [(r, v) for r, v in zip(rel, vs) if r >= -window]
            if not pairs:
                color_idx += 1
                continue
            rx, ry = zip(*pairs)
            c = colors[color_idx % len(colors)]
            self.ax.plot(rx, ry, label=key, color=c, linewidth=1.5)
            color_idx += 1

        if color_idx > 0:
            self.ax.legend(loc="upper left", fontsize=7,
                           facecolor=BG_LIGHT, edgecolor=BG_LIGHT,
                           labelcolor=FG)
        self.ax.set_xlim(-window, 0)
        self.fig.tight_layout()
        self.canvas.draw_idle()

    def _refresh_slider_label(self):
        val = int(self.graph_window.get())
        self.window_lbl.configure(text=f"{val} s")

    def _refresh_mac_list(self):
        macs = self.dm.get_known_macs()
        for w in self.mac_list_frame.winfo_children():
            w.destroy()
        if not macs:
            ttk.Label(self.mac_list_frame,
                      text="Noch keine MAC-Adressen erkannt.",
                      foreground=YELLOW).pack(anchor="w")
            return
        for mac, robot in macs.items():
            row = ttk.Frame(self.mac_list_frame)
            row.pack(fill="x", pady=2)
            ttk.Label(row, text=mac, font=("Consolas", 10)).pack(side="left")
            ttk.Label(row, text="→").pack(side="left", padx=8)
            ttk.Label(row, text=robot, foreground=ACCENT,
                      font=("Segoe UI", 10, "bold")).pack(side="left")

    def _on_close(self):
        self.reader.disconnect()
        self.destroy()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main():
    app = MonitorApp()
    app.mainloop()


if __name__ == "__main__":
    main()
