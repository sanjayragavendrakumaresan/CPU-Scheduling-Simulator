#!/usr/bin/env python3
"""Tkinter desktop front-end for the C++17 CPU Scheduling Simulator.

The scheduling algorithms remain implemented in C++: this application exports
its process table to a temporary CSV and invokes the compiled C++ backend.
"""
from __future__ import annotations

import csv
import os
import random
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, simpledialog, ttk
except ImportError as exc:  # Linux distributions sometimes package Tk separately.
    print("Tkinter is required. On Debian/Ubuntu install it with: sudo apt install python3-tk", file=sys.stderr)
    raise SystemExit(2) from exc


ROOT_DIR = Path(__file__).resolve().parent
ALGORITHMS = [
    ("First Come First Serve (FCFS)", "fcfs"),
    ("Shortest Job First (Non-preemptive)", "sjf"),
    ("Shortest Remaining Time First (SRTF)", "srtf"),
    ("Priority (Non-preemptive)", "priority-np"),
    ("Priority (Preemptive)", "priority-p"),
    ("Round Robin (RR)", "rr"),
]
ALGORITHM_BY_LABEL = dict(ALGORITHMS)
ALGORITHM_LABEL = dict((key, label) for label, key in ALGORITHMS)


class ProcessDialog(tk.Toplevel):
    def __init__(self, parent: tk.Misc, title: str, initial: dict | None = None):
        super().__init__(parent)
        self.title(title)
        self.resizable(False, False)
        self.transient(parent)
        self.grab_set()
        initial = initial or {"id": "", "arrival": 0, "burst": 1, "priority": 1}
        self.result: dict | None = None
        self.vars = {
            "id": tk.StringVar(value=str(initial["id"])),
            "arrival": tk.StringVar(value=str(initial["arrival"])),
            "burst": tk.StringVar(value=str(initial["burst"])),
            "priority": tk.StringVar(value=str(initial["priority"])),
        }
        labels = [("Process ID", "id"), ("Arrival time", "arrival"), ("Burst time", "burst"), ("Priority (smaller = higher)", "priority")]
        for row, (label, key) in enumerate(labels):
            ttk.Label(self, text=label).grid(row=row, column=0, sticky="w", padx=12, pady=6)
            entry = ttk.Entry(self, textvariable=self.vars[key], width=24)
            entry.grid(row=row, column=1, sticky="ew", padx=12, pady=6)
            if row == 0:
                entry.focus_set()
        buttons = ttk.Frame(self)
        buttons.grid(row=4, column=0, columnspan=2, sticky="e", padx=12, pady=(8, 12))
        ttk.Button(buttons, text="Cancel", command=self.destroy).pack(side="right", padx=(6, 0))
        ttk.Button(buttons, text="Save", command=self._save).pack(side="right")
        self.bind("<Return>", lambda _event: self._save())
        self.bind("<Escape>", lambda _event: self.destroy())
        self.wait_visibility()

    def _save(self) -> None:
        pid = self.vars["id"].get().strip()
        if not pid:
            messagebox.showerror("Invalid process", "Process ID cannot be empty.", parent=self)
            return
        try:
            arrival = int(self.vars["arrival"].get())
            burst = int(self.vars["burst"].get())
            priority = int(self.vars["priority"].get())
        except ValueError:
            messagebox.showerror("Invalid process", "Arrival, burst, and priority must be whole numbers.", parent=self)
            return
        if arrival < 0 or burst <= 0:
            messagebox.showerror("Invalid process", "Arrival must be >= 0 and burst must be > 0.", parent=self)
            return
        self.result = {"id": pid, "arrival": arrival, "burst": burst, "priority": priority}
        self.destroy()


class SchedulerGUI:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("CPU Scheduling Simulator — Desktop GUI")
        self.root.geometry("1260x850")
        self.root.minsize(980, 680)
        self.root.configure(bg="#f3f6fb")
        self.processes: list[dict] = []
        self.last_result: dict | None = None
        self.last_comparison: list[dict] | None = None
        self.busy = False
        self.step_index = -1
        self.chart_segments: list[dict] = []
        self.decisions: list[dict] = []
        self.decision_index = -1
        self.decision_text = tk.StringVar(value="Run an algorithm to inspect scheduling decisions.")

        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("TFrame", background="#f3f6fb")
        style.configure("Card.TFrame", background="#ffffff", relief="flat")
        style.configure("TLabel", background="#f3f6fb", foreground="#182338", font=("Segoe UI", 10))
        style.configure("Title.TLabel", font=("Segoe UI", 19, "bold"), foreground="#12284c")
        style.configure("Subtitle.TLabel", font=("Segoe UI", 9), foreground="#68778f")
        style.configure("CardValue.TLabel", background="#ffffff", font=("Segoe UI", 14, "bold"), foreground="#176b87")
        style.configure("Treeview", rowheight=25, font=("Segoe UI", 9))
        style.configure("Treeview.Heading", font=("Segoe UI", 9, "bold"))
        style.configure("Accent.TButton", font=("Segoe UI", 9, "bold"))

        self.algorithm_var = tk.StringVar(value=ALGORITHMS[0][0])
        self.quantum_var = tk.IntVar(value=2)
        self.overhead_var = tk.IntVar(value=0)
        self.aging_var = tk.IntVar(value=5)
        self.use_aging_var = tk.BooleanVar(value=False)
        self.status_var = tk.StringVar(value="Ready. The GUI runs the C++17 scheduler as its backend.")
        self.metric_vars = {key: tk.StringVar(value="—") for key in ("waiting", "turnaround", "response", "util", "throughput", "switches")}
        self._build_ui()
        self.root.protocol("WM_DELETE_WINDOW", self.root.destroy)

    def _build_ui(self) -> None:
        outer = ttk.Frame(self.root, padding=14)
        outer.pack(fill="both", expand=True)

        header = ttk.Frame(outer)
        header.pack(fill="x", pady=(0, 12))
        ttk.Label(header, text="CPU Scheduling Simulator", style="Title.TLabel").pack(anchor="w")
        ttk.Label(header, text="Interactive Python/Tkinter interface · scheduling logic remains in tested C++17", style="Subtitle.TLabel").pack(anchor="w", pady=(3, 0))

        toolbar = ttk.Frame(outer)
        toolbar.pack(fill="x", pady=(0, 10))
        self.action_buttons = []
        for text, command in [
            ("Add", self.add_process), ("Edit", self.edit_process), ("Delete", self.delete_process),
            ("Clear all", self.clear_processes), ("Random data", self.generate_processes),
            ("Import CSV", self.import_csv), ("Export processes", self.export_processes),
        ]:
            button = ttk.Button(toolbar, text=text, command=command)
            button.pack(side="left", padx=(0, 5))
            self.action_buttons.append(button)

        controls = ttk.LabelFrame(outer, text="Scheduling settings", padding=(10, 8))
        controls.pack(fill="x", pady=(0, 10))
        ttk.Label(controls, text="Algorithm").grid(row=0, column=0, sticky="w", padx=(0, 5))
        algo = ttk.Combobox(controls, textvariable=self.algorithm_var, values=[label for label, _ in ALGORITHMS], state="readonly", width=31)
        algo.grid(row=0, column=1, sticky="w", padx=(0, 14))
        ttk.Label(controls, text="RR quantum").grid(row=0, column=2, sticky="w")
        ttk.Spinbox(controls, from_=1, to=1000000, textvariable=self.quantum_var, width=7).grid(row=0, column=3, sticky="w", padx=(5, 14))
        ttk.Label(controls, text="Switch overhead").grid(row=0, column=4, sticky="w")
        ttk.Spinbox(controls, from_=0, to=1000000, textvariable=self.overhead_var, width=7).grid(row=0, column=5, sticky="w", padx=(5, 14))
        ttk.Checkbutton(controls, text="Priority aging", variable=self.use_aging_var).grid(row=0, column=6, sticky="w")
        ttk.Label(controls, text="Interval").grid(row=0, column=7, sticky="w", padx=(8, 0))
        ttk.Spinbox(controls, from_=1, to=1000000, textvariable=self.aging_var, width=7).grid(row=0, column=8, sticky="w", padx=(5, 12))
        self.run_button = ttk.Button(controls, text="Run algorithm", style="Accent.TButton", command=self.run_selected)
        self.run_button.grid(row=0, column=9, padx=(4, 4))
        self.compare_button = ttk.Button(controls, text="Compare all", command=self.compare_all)
        self.compare_button.grid(row=0, column=10)
        self.action_buttons.extend([self.run_button, self.compare_button])

        metrics = ttk.Frame(outer)
        metrics.pack(fill="x", pady=(0, 10))
        metric_specs = [
            ("Avg waiting", "waiting"), ("Avg turnaround", "turnaround"), ("Avg response", "response"),
            ("CPU utilization", "util"), ("Throughput", "throughput"), ("Context switches", "switches"),
        ]
        for col, (title, key) in enumerate(metric_specs):
            card = ttk.Frame(metrics, style="Card.TFrame", padding=(10, 7))
            card.grid(row=0, column=col, sticky="ew", padx=(0 if col == 0 else 5, 0))
            ttk.Label(card, text=title, background="#ffffff", foreground="#68778f").pack(anchor="w")
            ttk.Label(card, textvariable=self.metric_vars[key], style="CardValue.TLabel").pack(anchor="w", pady=(4, 0))
            metrics.columnconfigure(col, weight=1)

        splitter = ttk.Panedwindow(outer, orient="horizontal")
        splitter.pack(fill="both", expand=True)
        left = ttk.Frame(splitter, padding=(0, 0, 8, 0))
        right = ttk.Frame(splitter, padding=(8, 0, 0, 0))
        splitter.add(left, weight=1)
        splitter.add(right, weight=3)
        ttk.Label(left, text="Processes", font=("Segoe UI", 11, "bold")).pack(anchor="w", pady=(0, 5))
        process_cols = ("id", "arrival", "burst", "priority")
        self.process_tree = ttk.Treeview(left, columns=process_cols, show="headings", selectmode="browse", height=19)
        for col, title, width in [("id", "ID", 100), ("arrival", "Arrival", 65), ("burst", "Burst", 60), ("priority", "Priority", 65)]:
            self.process_tree.heading(col, text=title)
            self.process_tree.column(col, width=width, anchor="center")
        pscroll = ttk.Scrollbar(left, orient="vertical", command=self.process_tree.yview)
        self.process_tree.configure(yscrollcommand=pscroll.set)
        self.process_tree.pack(side="left", fill="both", expand=True)
        pscroll.pack(side="right", fill="y")
        self.process_tree.bind("<Double-1>", lambda _event: self.edit_process())

        ttk.Label(right, text="Per-process results", font=("Segoe UI", 11, "bold")).pack(anchor="w", pady=(0, 5))
        result_cols = ("id", "arrival", "burst", "completion", "turnaround", "waiting", "response")
        self.result_tree = ttk.Treeview(right, columns=result_cols, show="headings", height=8)
        for col, title, width in [
            ("id", "Process", 100), ("arrival", "AT", 55), ("burst", "BT", 55), ("completion", "CT", 65),
            ("turnaround", "TAT", 65), ("waiting", "WT", 60), ("response", "RT", 60),
        ]:
            self.result_tree.heading(col, text=title)
            self.result_tree.column(col, width=width, anchor="center")
        self.result_tree.pack(fill="x", expand=False)

        chart_head = ttk.Frame(right)
        chart_head.pack(fill="x", pady=(12, 3))
        ttk.Label(chart_head, text="Gantt chart", font=("Segoe UI", 11, "bold")).pack(side="left")
        self.step_button = ttk.Button(chart_head, text="Next Gantt segment", command=self.next_step, state="disabled")
        self.step_button.pack(side="right")
        self.reset_step_button = ttk.Button(chart_head, text="Reset steps", command=self.reset_steps, state="disabled")
        self.reset_step_button.pack(side="right", padx=5)
        self.decision_button = ttk.Button(chart_head, text="Next decision", command=self.next_decision, state="disabled")
        self.decision_button.pack(side="right", padx=5)
        self.reset_decision_button = ttk.Button(chart_head, text="Reset decisions", command=self.reset_decisions, state="disabled")
        self.reset_decision_button.pack(side="right", padx=5)
        chart_frame = ttk.Frame(right)
        chart_frame.pack(fill="x")
        self.chart = tk.Canvas(chart_frame, height=125, background="#ffffff", highlightthickness=1, highlightbackground="#d9e2ef")
        hscroll = ttk.Scrollbar(chart_frame, orient="horizontal", command=self.chart.xview)
        self.chart.configure(xscrollcommand=hscroll.set)
        self.chart.pack(fill="x", expand=True)
        hscroll.pack(fill="x")
        decision_panel = ttk.LabelFrame(right, text="Step-by-step decision", padding=(8, 5))
        decision_panel.pack(fill="x", pady=(6, 0))
        ttk.Label(decision_panel, textvariable=self.decision_text, background="#ffffff", wraplength=800, justify="left").pack(anchor="w")

        ttk.Label(right, text="Execution timeline", font=("Segoe UI", 11, "bold")).pack(anchor="w", pady=(10, 4))
        timeline_frame = ttk.Frame(right)
        timeline_frame.pack(fill="both", expand=True)
        self.timeline_tree = ttk.Treeview(timeline_frame, columns=("start", "end", "activity"), show="headings", height=7)
        for col, title, width in [("start", "Start", 80), ("end", "End", 80), ("activity", "Activity", 180)]:
            self.timeline_tree.heading(col, text=title)
            self.timeline_tree.column(col, width=width, anchor="center")
        tscroll = ttk.Scrollbar(timeline_frame, orient="vertical", command=self.timeline_tree.yview)
        self.timeline_tree.configure(yscrollcommand=tscroll.set)
        self.timeline_tree.pack(side="left", fill="both", expand=True)
        tscroll.pack(side="right", fill="y")

        footer = ttk.Frame(outer)
        footer.pack(fill="x", pady=(9, 0))
        ttk.Button(footer, text="Export results", command=self.export_result).pack(side="right", padx=(5, 0))
        ttk.Button(footer, text="Export comparison", command=self.export_comparison).pack(side="right", padx=(5, 0))
        ttk.Button(footer, text="About / assumptions", command=self.show_about).pack(side="right", padx=(5, 0))
        ttk.Label(footer, textvariable=self.status_var, style="Subtitle.TLabel").pack(side="left", fill="x", expand=True)

    def _selected_process_index(self) -> int | None:
        selected = self.process_tree.selection()
        if not selected:
            return None
        try:
            return int(selected[0])
        except ValueError:
            return None

    def _validate_unique_id(self, process: dict, ignore_index: int | None = None) -> bool:
        for i, existing in enumerate(self.processes):
            if i != ignore_index and existing["id"] == process["id"]:
                messagebox.showerror("Duplicate ID", f"Process ID {process['id']!r} is already in the dataset.", parent=self.root)
                return False
        return True

    def _refresh_processes(self) -> None:
        self.process_tree.delete(*self.process_tree.get_children())
        for i, p in enumerate(self.processes):
            self.process_tree.insert("", "end", iid=str(i), values=(p["id"], p["arrival"], p["burst"], p["priority"]))

    def _dataset_changed(self) -> None:
        self.last_result = None
        self.last_comparison = None
        self.result_tree.delete(*self.result_tree.get_children())
        self.timeline_tree.delete(*self.timeline_tree.get_children())
        self.chart_segments = []
        self.step_index = -1
        self.decisions = []
        self.decision_index = -1
        self.decision_text.set("Run an algorithm to inspect scheduling decisions.")
        self.chart.delete("all")
        self.step_button.configure(state="disabled")
        self.reset_step_button.configure(state="disabled")
        self.decision_button.configure(state="disabled")
        self.reset_decision_button.configure(state="disabled")
        for variable in self.metric_vars.values():
            variable.set("—")

    def add_process(self) -> None:
        dialog = ProcessDialog(self.root, "Add process")
        self.root.wait_window(dialog)
        if dialog.result and self._validate_unique_id(dialog.result):
            self.processes.append(dialog.result)
            self._refresh_processes()
            self._dataset_changed()
            self.status_var.set(f"Added {dialog.result['id']}.")

    def edit_process(self) -> None:
        index = self._selected_process_index()
        if index is None:
            messagebox.showinfo("Select process", "Choose a process row to edit.", parent=self.root)
            return
        dialog = ProcessDialog(self.root, "Edit process", self.processes[index])
        self.root.wait_window(dialog)
        if dialog.result and self._validate_unique_id(dialog.result, index):
            self.processes[index] = dialog.result
            self._refresh_processes()
            self._dataset_changed()
            self.status_var.set(f"Updated {dialog.result['id']}.")

    def delete_process(self) -> None:
        index = self._selected_process_index()
        if index is None:
            messagebox.showinfo("Select process", "Choose a process row to delete.", parent=self.root)
            return
        pid = self.processes[index]["id"]
        del self.processes[index]
        self._refresh_processes()
        self._dataset_changed()
        self.status_var.set(f"Deleted {pid}.")

    def clear_processes(self) -> None:
        if self.processes and messagebox.askyesno("Clear dataset", "Remove every process from the current dataset?", parent=self.root):
            self.processes.clear()
            self._refresh_processes()
            self._dataset_changed()

    def generate_processes(self) -> None:
        count = simpledialog.askinteger("Random dataset", "Number of processes", minvalue=1, maxvalue=100000, parent=self.root)
        if count is None:
            return
        max_arrival = simpledialog.askinteger("Random dataset", "Maximum arrival time", minvalue=0, initialvalue=max(1, count), parent=self.root)
        max_burst = simpledialog.askinteger("Random dataset", "Maximum burst time", minvalue=1, initialvalue=20, parent=self.root)
        max_priority = simpledialog.askinteger("Random dataset", "Maximum priority", minvalue=1, initialvalue=10, parent=self.root)
        seed = simpledialog.askinteger("Random dataset", "Reproducible seed", minvalue=0, initialvalue=2026, parent=self.root)
        if None in (max_arrival, max_burst, max_priority, seed):
            return
        rng = random.Random(seed)
        self.processes = [
            {"id": f"P{i + 1}", "arrival": rng.randint(0, max_arrival), "burst": rng.randint(1, max_burst), "priority": rng.randint(1, max_priority)}
            for i in range(count)
        ]
        self._refresh_processes()
        self._dataset_changed()
        self.status_var.set(f"Generated {count} reproducible processes (seed {seed}).")

    @staticmethod
    def _canonical_header(header: str) -> str:
        return "".join(ch.lower() for ch in header if ch.isalnum())

    def import_csv(self) -> None:
        path = filedialog.askopenfilename(title="Import process CSV", filetypes=[("CSV files", "*.csv"), ("All files", "*.*")], parent=self.root)
        if not path:
            return
        try:
            with open(path, newline="", encoding="utf-8-sig") as handle:
                reader = csv.DictReader(handle)
                headers = {self._canonical_header(h): h for h in (reader.fieldnames or [])}
                aliases = {
                    "id": ("processid", "id"), "arrival": ("arrivaltime", "arrival", "at"),
                    "burst": ("bursttime", "burst", "bt"), "priority": ("priority",),
                }
                selected = {}
                for key, options in aliases.items():
                    selected[key] = next((headers[name] for name in options if name in headers), None)
                if any(value is None for value in selected.values()):
                    raise ValueError("CSV needs ProcessID, ArrivalTime, BurstTime, and Priority columns.")
                loaded = []
                for line_no, row in enumerate(reader, start=2):
                    p = {"id": (row[selected["id"]] or "").strip()}
                    p["arrival"] = int(row[selected["arrival"]])
                    p["burst"] = int(row[selected["burst"]])
                    p["priority"] = int(row[selected["priority"]])
                    if not p["id"] or p["arrival"] < 0 or p["burst"] <= 0:
                        raise ValueError(f"Invalid process values at CSV row {line_no}.")
                    loaded.append(p)
                if not loaded:
                    raise ValueError("The CSV contains no process rows.")
                ids = [p["id"] for p in loaded]
                if len(set(ids)) != len(ids):
                    raise ValueError("Process IDs must be unique.")
            self.processes = loaded
            self._refresh_processes()
            self._dataset_changed()
            self.status_var.set(f"Imported {len(loaded)} process(es) from {Path(path).name}.")
        except (OSError, csv.Error, ValueError, TypeError) as exc:
            messagebox.showerror("Import failed", str(exc), parent=self.root)

    def export_processes(self) -> None:
        if not self.processes:
            messagebox.showinfo("No processes", "There are no processes to export.", parent=self.root)
            return
        path = filedialog.asksaveasfilename(title="Export process CSV", defaultextension=".csv", filetypes=[("CSV files", "*.csv")], parent=self.root)
        if not path:
            return
        try:
            self._write_process_csv(path, self.processes)
            self.status_var.set(f"Saved process dataset to {path}.")
        except OSError as exc:
            messagebox.showerror("Export failed", str(exc), parent=self.root)

    @staticmethod
    def _write_process_csv(path: str | Path, processes: list[dict]) -> None:
        with open(path, "w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(["ProcessID", "ArrivalTime", "BurstTime", "Priority"])
            for p in processes:
                writer.writerow([p["id"], p["arrival"], p["burst"], p["priority"]])

    def _ensure_backend(self) -> Path:
        executable = ROOT_DIR / ("scheduler_gui_backend.exe" if os.name == "nt" else "scheduler_gui_backend")
        sources = [ROOT_DIR / "main.cpp", ROOT_DIR / "scheduler.hpp", ROOT_DIR / "io.hpp", ROOT_DIR / "tests.hpp"]
        if not executable.exists() or any(source.stat().st_mtime > executable.stat().st_mtime for source in sources):
            compiler = shutil.which("g++")
            if not compiler:
                raise RuntimeError("g++ was not found. Install a C++17 compiler and add g++ to PATH.")
            command = [compiler, "-std=c++17", "-O2", "main.cpp", "-o", str(executable)]
            built = subprocess.run(command, cwd=ROOT_DIR, capture_output=True, text=True, timeout=120)
            if built.returncode != 0:
                raise RuntimeError("Could not build the C++ scheduler:\n" + (built.stderr or built.stdout))
        return executable

    def _run_backend(self, processes: list[dict], algorithm: str, quantum: int, overhead: int, aging: int) -> dict:
        executable = self._ensure_backend()
        with tempfile.TemporaryDirectory(prefix="cpu-scheduler-gui-") as temp_dir:
            folder = Path(temp_dir)
            input_csv = folder / "processes.csv"
            result_csv = folder / "results.csv"
            timeline_csv = folder / "timeline.csv"
            decisions_csv = folder / "decisions.csv"
            self._write_process_csv(input_csv, processes)
            command = [str(executable), "--gui-run", algorithm, str(input_csv), str(result_csv), str(timeline_csv), str(decisions_csv), str(quantum), str(overhead), str(aging)]
            completed = subprocess.run(command, cwd=ROOT_DIR, capture_output=True, text=True, timeout=120)
            if completed.returncode != 0:
                raise RuntimeError(completed.stderr.strip() or completed.stdout.strip() or "C++ scheduler backend failed.")
            with open(result_csv, newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            process_rows = []
            metrics = {}
            for row in rows:
                if row["Type"] == "PROCESS":
                    process_rows.append({
                        "id": row["ProcessID"], "arrival": int(row["Arrival"]), "burst": int(row["Burst"]),
                        "priority": int(row["Priority"]), "completion": int(row["Completion"]),
                        "turnaround": int(row["Turnaround"]), "waiting": int(row["Waiting"]),
                        "response": int(row["Response"]), "first_start": int(row["FirstStart"]),
                    })
                elif row["Type"] == "METRICS":
                    metrics = {
                        "avg_waiting": float(row["AverageWaiting"]), "avg_turnaround": float(row["AverageTurnaround"]),
                        "avg_response": float(row["AverageResponse"]), "cpu_utilization": float(row["CPUUtilizationPercent"]),
                        "throughput": float(row["Throughput"]), "context_switches": int(row["ContextSwitches"]),
                    }
            with open(timeline_csv, newline="", encoding="utf-8") as handle:
                timeline = [{"start": int(row["Start"]), "end": int(row["End"]), "activity": row["Activity"]} for row in csv.DictReader(handle)]
            with open(decisions_csv, newline="", encoding="utf-8") as handle:
                decisions = [{"time": int(row["Time"]), "ready": row["Ready"], "selected": row["Selected"], "remaining": row["Remaining"]} for row in csv.DictReader(handle)]
            return {"algorithm": algorithm, "label": ALGORITHM_LABEL[algorithm], "processes": process_rows, "metrics": metrics, "timeline": timeline, "decisions": decisions}

    def _set_busy(self, busy: bool, message: str) -> None:
        self.busy = busy
        self.status_var.set(message)
        state = "disabled" if busy else "normal"
        for button in self.action_buttons:
            button.configure(state=state)

    def _run_async(self, message: str, operation, on_success) -> None:
        if self.busy:
            return
        self._set_busy(True, message)

        def worker() -> None:
            try:
                value = operation()
                self.root.after(0, lambda: self._finish_async(value, None, on_success))
            except Exception as exc:  # Error is surfaced in the GUI thread.
                self.root.after(0, lambda error=exc: self._finish_async(None, error, on_success))

        threading.Thread(target=worker, daemon=True).start()

    def _finish_async(self, value, error: Exception | None, on_success) -> None:
        self._set_busy(False, "Ready.")
        if error is not None:
            messagebox.showerror("Scheduling failed", str(error), parent=self.root)
            self.status_var.set("Operation failed.")
            return
        on_success(value)

    def _read_settings(self) -> tuple[str, int, int, int]:
        key = ALGORITHM_BY_LABEL[self.algorithm_var.get()]
        quantum, overhead, aging = self.quantum_var.get(), self.overhead_var.get(), self.aging_var.get()
        if quantum <= 0 or overhead < 0 or aging <= 0:
            raise ValueError("Quantum must be > 0, overhead must be >= 0, and aging interval must be > 0.")
        if not self.processes:
            raise ValueError("Add or import at least one process first.")
        active_aging = aging if self.use_aging_var.get() and key in ("priority-np", "priority-p") else 0
        return key, quantum, overhead, active_aging

    def run_selected(self) -> None:
        try:
            key, quantum, overhead, aging = self._read_settings()
        except (ValueError, tk.TclError) as exc:
            messagebox.showerror("Check settings", str(exc), parent=self.root)
            return
        data = [dict(p) for p in self.processes]
        self._run_async("Running C++ scheduler…", lambda: self._run_backend(data, key, quantum, overhead, aging), self._show_result)

    def compare_all(self) -> None:
        try:
            _key, quantum, overhead, aging = self._read_settings()
        except (ValueError, tk.TclError) as exc:
            messagebox.showerror("Check settings", str(exc), parent=self.root)
            return
        data = [dict(p) for p in self.processes]

        def operation() -> list[dict]:
            return [self._run_backend(data, key, quantum, overhead, aging if key in ("priority-np", "priority-p") else 0) for _, key in ALGORITHMS]

        self._run_async("Running all six algorithms for comparison…", operation, self._show_comparison)

    def _show_result(self, result: dict) -> None:
        self.last_result = result
        self._set_status_metrics(result["metrics"])
        self.result_tree.delete(*self.result_tree.get_children())
        for row in result["processes"]:
            self.result_tree.insert("", "end", values=(row["id"], row["arrival"], row["burst"], row["completion"], row["turnaround"], row["waiting"], row["response"]))
        self.timeline_tree.delete(*self.timeline_tree.get_children())
        for index, segment in enumerate(result["timeline"]):
            self.timeline_tree.insert("", "end", iid=str(index), values=(segment["start"], segment["end"], segment["activity"]))
        self.chart_segments = result["timeline"]
        self.step_index = -1
        self.decisions = result["decisions"]
        self.decision_index = -1
        self.decision_text.set(f"{len(self.decisions)} scheduling decision(s). Select Next decision to advance through them.")
        self._draw_gantt()
        self.step_button.configure(state="normal" if self.chart_segments else "disabled")
        self.reset_step_button.configure(state="normal" if self.chart_segments else "disabled")
        self.decision_button.configure(state="normal" if self.decisions else "disabled")
        self.reset_decision_button.configure(state="normal" if self.decisions else "disabled")
        self.status_var.set(f"{result['label']} complete · {len(result['processes'])} process(es), {len(result['timeline'])} timeline interval(s).")

    def _set_status_metrics(self, metrics: dict) -> None:
        self.metric_vars["waiting"].set(f"{metrics['avg_waiting']:.2f}")
        self.metric_vars["turnaround"].set(f"{metrics['avg_turnaround']:.2f}")
        self.metric_vars["response"].set(f"{metrics['avg_response']:.2f}")
        self.metric_vars["util"].set(f"{metrics['cpu_utilization']:.2f}%")
        self.metric_vars["throughput"].set(f"{metrics['throughput']:.4f}")
        self.metric_vars["switches"].set(str(metrics["context_switches"]))

    def _draw_gantt(self) -> None:
        self.chart.delete("all")
        if not self.chart_segments:
            self.chart.configure(scrollregion=(0, 0, 1000, 125))
            return
        max_show = 500
        segments = self.chart_segments[:max_show]
        if len(self.chart_segments) > max_show:
            self.chart.create_text(12, 12, anchor="nw", text=f"Showing first {max_show} intervals; all {len(self.chart_segments)} are in the timeline table.", fill="#7b4d12")
        durations = [max(1, item["end"] - item["start"]) for item in segments]
        scale = min(20.0, max(2.0, 900.0 / max(1, sum(durations))))
        x = 12.0
        y1, y2 = 37, 84
        colors = {"IDLE": "#dce4ee", "CS": "#f5c078"}
        palette = ["#74c0a8", "#6da9e8", "#b49ae5", "#e7a3b4", "#9bc56b", "#e1b35c", "#6fc3cd", "#d9915f"]
        for index, segment in enumerate(segments):
            activity = segment["activity"]
            width = max(76.0, min(230.0, durations[index] * scale + 22.0))
            color = colors.get(activity, palette[(sum(ord(ch) for ch in activity) % len(palette))])
            outline = "#182338" if index == self.step_index else "#ffffff"
            line_width = 3 if index == self.step_index else 1
            self.chart.create_rectangle(x, y1, x + width, y2, fill=color, outline=outline, width=line_width)
            self.chart.create_text(x + width / 2, (y1 + y2) / 2, text=activity, font=("Segoe UI", 9, "bold"))
            self.chart.create_text(x, y2 + 14, text=str(segment["start"]), anchor="nw", font=("Segoe UI", 8), fill="#42536c")
            self.chart.create_text(x + width, y2 + 14, text=str(segment["end"]), anchor="ne", font=("Segoe UI", 8), fill="#42536c")
            x += width + 5
        self.chart.configure(scrollregion=(0, 0, x + 8, 125))

    def next_step(self) -> None:
        if not self.chart_segments:
            return
        self.step_index = (self.step_index + 1) % min(500, len(self.chart_segments))
        self._draw_gantt()
        if str(self.step_index) in self.timeline_tree.get_children():
            self.timeline_tree.selection_set(str(self.step_index))
            self.timeline_tree.see(str(self.step_index))
        item = self.chart_segments[self.step_index]
        self.status_var.set(f"Step {self.step_index + 1}: {item['activity']} executes from {item['start']} to {item['end']}.")

    def reset_steps(self) -> None:
        self.step_index = -1
        self._draw_gantt()
        self.status_var.set("Gantt step selection reset.")

    def next_decision(self) -> None:
        if not self.decisions:
            return
        if self.decision_index + 1 < len(self.decisions):
            self.decision_index += 1
        decision = self.decisions[self.decision_index]
        self.decision_text.set(
            f"Decision {self.decision_index + 1}/{len(self.decisions)} at t={decision['time']}\n"
            f"Ready queue/set: {decision['ready']}\n"
            f"Selected / running: {decision['selected']}\n"
            f"Remaining before decision: {decision['remaining']}"
        )
        self.status_var.set(f"Decision {self.decision_index + 1} of {len(self.decisions)} at t={decision['time']}.")

    def reset_decisions(self) -> None:
        self.decision_index = -1
        self.decision_text.set(f"{len(self.decisions)} scheduling decision(s). Select Next decision to begin.")
        self.status_var.set("Scheduling-decision navigation reset.")

    def _show_comparison(self, results: list[dict]) -> None:
        self.last_comparison = results
        window = tk.Toplevel(self.root)
        window.title("Algorithm comparison")
        window.geometry("1000x400")
        frame = ttk.Frame(window, padding=14)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="Comparison on the current process dataset", font=("Segoe UI", 13, "bold")).pack(anchor="w", pady=(0, 10))
        columns = ("algorithm", "waiting", "turnaround", "response", "util", "throughput", "switches")
        tree = ttk.Treeview(frame, columns=columns, show="headings", height=8)
        titles = [("algorithm", "Algorithm"), ("waiting", "Avg waiting"), ("turnaround", "Avg turnaround"), ("response", "Avg response"), ("util", "Utilization %"), ("throughput", "Throughput"), ("switches", "Switches")]
        for key, title in titles:
            tree.heading(key, text=title)
            tree.column(key, anchor="center", width=145 if key == "algorithm" else 115)
        tree.pack(fill="both", expand=True)
        getters = {
            "waiting": lambda r: r["metrics"]["avg_waiting"],
            "turnaround": lambda r: r["metrics"]["avg_turnaround"],
            "response": lambda r: r["metrics"]["avg_response"],
            "util": lambda r: r["metrics"]["cpu_utilization"],
            "throughput": lambda r: r["metrics"]["throughput"],
            "switches": lambda r: r["metrics"]["context_switches"],
        }
        best = {
            "waiting": min(getters["waiting"](r) for r in results),
            "turnaround": min(getters["turnaround"](r) for r in results),
            "response": min(getters["response"](r) for r in results),
            "util": max(getters["util"](r) for r in results),
            "throughput": max(getters["throughput"](r) for r in results),
            "switches": min(getters["switches"](r) for r in results),
        }
        for r in results:
            m = r["metrics"]
            values = [r["label"]]
            for key in ("waiting", "turnaround", "response", "util", "throughput", "switches"):
                value = getters[key](r)
                best_key = "turnaround" if key == "turnaround" else key
                text = f"{value:.2f}" if isinstance(value, float) else str(value)
                if abs(float(value) - float(best[best_key])) < 1e-9:
                    text += " *"
                values.append(text)
            tree.insert("", "end", values=values)
        ttk.Label(frame, text="* Best value for that metric on this dataset. No policy is universally best.", style="Subtitle.TLabel").pack(anchor="w", pady=(8, 0))
        ttk.Button(frame, text="Close", command=window.destroy).pack(anchor="e", pady=(8, 0))
        self.status_var.set("Comparison complete for all six algorithms.")

    def export_result(self) -> None:
        if not self.last_result:
            messagebox.showinfo("No results", "Run an algorithm before exporting results.", parent=self.root)
            return
        path = filedialog.asksaveasfilename(title="Export schedule results", defaultextension=".csv", filetypes=[("CSV files", "*.csv")], parent=self.root)
        if not path:
            return
        try:
            with open(path, "w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                writer.writerow(["Type", "Algorithm", "ProcessID", "Arrival", "Burst", "Priority", "Completion", "Turnaround", "Waiting", "Response", "FirstStart", "ActivityStart", "ActivityEnd", "Activity", "AverageWaiting", "AverageTurnaround", "AverageResponse", "CPUUtilizationPercent", "Throughput", "ContextSwitches"])
                for p in self.last_result["processes"]:
                    writer.writerow(["PROCESS", self.last_result["label"], p["id"], p["arrival"], p["burst"], p["priority"], p["completion"], p["turnaround"], p["waiting"], p["response"], p["first_start"], "", "", "", "", "", "", "", "", ""])
                m = self.last_result["metrics"]
                writer.writerow(["METRICS", self.last_result["label"], "", "", "", "", "", "", "", "", "", "", "", "", m["avg_waiting"], m["avg_turnaround"], m["avg_response"], m["cpu_utilization"], m["throughput"], m["context_switches"]])
                for t in self.last_result["timeline"]:
                    writer.writerow(["TIMELINE", self.last_result["label"], "", "", "", "", "", "", "", "", "", t["start"], t["end"], t["activity"], "", "", "", "", "", ""])
            self.status_var.set(f"Exported result, metrics, and timeline to {path}.")
        except OSError as exc:
            messagebox.showerror("Export failed", str(exc), parent=self.root)

    def export_comparison(self) -> None:
        if not self.last_comparison:
            messagebox.showinfo("No comparison", "Run Compare all before exporting comparison results.", parent=self.root)
            return
        path = filedialog.asksaveasfilename(title="Export comparison CSV", defaultextension=".csv", filetypes=[("CSV files", "*.csv")], parent=self.root)
        if not path:
            return
        try:
            with open(path, "w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                writer.writerow(["Algorithm", "AverageWaiting", "AverageTurnaround", "AverageResponse", "CPUUtilizationPercent", "Throughput", "ContextSwitches"])
                for r in self.last_comparison:
                    m = r["metrics"]
                    writer.writerow([r["label"], m["avg_waiting"], m["avg_turnaround"], m["avg_response"], m["cpu_utilization"], m["throughput"], m["context_switches"]])
            self.status_var.set(f"Exported algorithm comparison to {path}.")
        except OSError as exc:
            messagebox.showerror("Export failed", str(exc), parent=self.root)

    def show_about(self) -> None:
        messagebox.showinfo(
            "About / simulation assumptions",
            "CPU Scheduling Simulator GUI\n\n"
            "The GUI invokes the C++17 scheduler backend; the six scheduling algorithms are not reimplemented in Python.\n\n"
            "Metrics: TAT = CT - AT; WT = TAT - BT; RT = first start - AT. CPU utilization counts process execution only and includes idle/switch time in the elapsed-time denominator. Throughput is process count divided by elapsed time from t=0.\n\n"
            "Smaller priority numbers are higher priority. Ties use arrival time then input order. Round Robin arrivals at a quantum boundary enter before the expired process is requeued. Switch overhead is charged on process-to-process transitions after the first dispatch.",
            parent=self.root,
        )


def main() -> int:
    root = tk.Tk()
    SchedulerGUI(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
