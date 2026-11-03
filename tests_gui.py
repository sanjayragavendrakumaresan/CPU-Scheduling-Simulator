"""Integration tests for the Python GUI's machine-readable C++ backend."""
import csv
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parent
ALGORITHMS = ("fcfs", "sjf", "srtf", "priority-np", "priority-p", "rr")


class GuiBackendIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("g++")
        if not compiler:
            raise unittest.SkipTest("g++ is required for GUI backend integration tests")
        cls.temp_root = tempfile.TemporaryDirectory(prefix="cpu-scheduling-gui-tests-")
        cls.temp_path = Path(cls.temp_root.name)
        cls.executable = cls.temp_path / ("scheduler-test.exe" if os.name == "nt" else "scheduler-test")
        command = [compiler, "-std=c++17", "-O2", "main.cpp", "-o", str(cls.executable)]
        if os.name == "nt":
            command.append("-lshell32")
        built = subprocess.run(
            command, cwd=PROJECT, capture_output=True, text=True, timeout=120,
        )
        if built.returncode != 0:
            raise RuntimeError(built.stderr or built.stdout)
        cls.input_csv = cls.temp_path / "processes.csv"
        with cls.input_csv.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(["ProcessID", "ArrivalTime", "BurstTime", "Priority"])
            writer.writerows([["P1", 0, 3, 2], ["P2", 1, 2, 1], ["P,3", 2, 1, 1]])

    @classmethod
    def tearDownClass(cls):
        cls.temp_root.cleanup()

    def run_backend(self, algorithm, quantum=2, overhead=0, aging=0):
        results = self.temp_path / f"{algorithm}-results.csv"
        timeline = self.temp_path / f"{algorithm}-timeline.csv"
        decisions = self.temp_path / f"{algorithm}-decisions.csv"
        completed = subprocess.run(
            [str(self.executable), "--gui-run", algorithm, str(self.input_csv), str(results), str(timeline), str(decisions), str(quantum), str(overhead), str(aging)],
            cwd=PROJECT, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        with results.open(newline="", encoding="utf-8") as handle:
            result_rows = list(csv.DictReader(handle))
        with timeline.open(newline="", encoding="utf-8") as handle:
            timeline_rows = list(csv.DictReader(handle))
        with decisions.open(newline="", encoding="utf-8") as handle:
            decision_rows = list(csv.DictReader(handle))
        return result_rows, timeline_rows, decision_rows

    def test_all_six_algorithms_export_complete_gui_data(self):
        for algorithm in ALGORITHMS:
            with self.subTest(algorithm=algorithm):
                results, timeline, decisions = self.run_backend(algorithm)
                process_rows = [row for row in results if row["Type"] == "PROCESS"]
                metrics = next(row for row in results if row["Type"] == "METRICS")
                self.assertEqual(len(process_rows), 3)
                self.assertGreater(float(metrics["AverageWaiting"]), -1)
                self.assertEqual(timeline[0]["Start"], "0")
                self.assertEqual(int(timeline[-1]["End"]), max(int(row["Completion"]) for row in process_rows))
                for left, right in zip(timeline, timeline[1:]):
                    self.assertEqual(left["End"], right["Start"], f"timeline gap in {algorithm}")
                executed = {row["ProcessID"]: 0 for row in process_rows}
                for segment in timeline:
                    if segment["Activity"] in executed:
                        executed[segment["Activity"]] += int(segment["End"]) - int(segment["Start"])
                for row in process_rows:
                    self.assertEqual(executed[row["ProcessID"]], int(row["Burst"]))
                    self.assertEqual(int(row["Turnaround"]), int(row["Completion"]) - int(row["Arrival"]))
                    self.assertEqual(int(row["Waiting"]), int(row["Turnaround"]) - int(row["Burst"]))
                    self.assertEqual(int(row["Response"]), int(row["FirstStart"]) - int(row["Arrival"]))
                self.assertGreaterEqual(len(decisions), 3)
                self.assertTrue(all("Selected" in row and "Ready" in row and "Remaining" in row for row in decisions))

    def test_priority_aging_and_context_switch_intervals_export(self):
        results, timeline, decisions = self.run_backend("priority-p", quantum=2, overhead=1, aging=2)
        self.assertTrue(any(row["Activity"] == "CS" for row in timeline))
        self.assertGreaterEqual(len(decisions), 3)
        metrics = next(row for row in results if row["Type"] == "METRICS")
        self.assertGreaterEqual(int(metrics["ContextSwitches"]), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
