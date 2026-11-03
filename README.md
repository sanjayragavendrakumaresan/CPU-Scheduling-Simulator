# CPU Scheduling Simulator

A portable CPU-scheduling simulator whose normal launch opens the **Python/Tkinter desktop GUI**. The original C++17 terminal menu remains available as an optional mode. Both interfaces use the same tested C++ scheduling engine for all six policies, per-process and aggregate metrics, context-switch overhead, and priority aging.

## Features

- FCFS, non-preemptive SJF, preemptive SRTF, non-preemptive priority, preemptive priority, and Round Robin.
- Different arrivals, burst lengths and priorities; idle time; preemption; simultaneous arrivals; deterministic ties.
- Add, edit, delete, list, clear, randomly generate, import, and export process datasets.
- Per-process completion, turnaround, waiting, and response times; averages, CPU utilization, throughput, and context-switch count.
- Segmented ASCII Gantt chart, detailed interval timeline, and step-by-step scheduling decisions with ready-set and remaining-time snapshots.
- All-algorithm comparison on one dataset, per-metric best markers, comparison CSV, and measured benchmarks for 10, 100, 1,000, and 10,000 processes.
- Reproducible random inputs using a configurable seed; optional context-switch overhead and priority aging.
- GUI-first startup with editable process table, random data, CSV import/export, algorithm controls, comparison, metrics, Gantt chart, timeline, and execution-step navigation.
- The Python GUI compiles/invokes the C++17 backend; it does not duplicate the scheduling algorithms and uses no third-party Python packages.
- A built-in regression suite available from the menu application or as a separate executable.

## Requirements

- The GUI requires Python 3.10+ with Tkinter, plus a C++17 compiler such as `g++` on `PATH` for its scheduler backend. Tkinter is part of Python's standard library; on Debian/Ubuntu it may need the system package `python3-tk`. No third-party Python packages are used.
- The optional terminal menu and test runner use the same C++17 compiler. No Makefile or CMake is used.

## Compile and run

From the project directory:

```sh
g++ -std=c++17 main.cpp -o scheduler
./scheduler
```

Running `scheduler` with no arguments now opens the GUI. To use the original terminal menu instead, run `./scheduler --terminal`.

On Windows (MinGW g++):

```powershell
g++ -std=c++17 main.cpp -o scheduler.exe -lshell32
.\scheduler.exe
```

On Windows, the simplest no-console launch is to **double-click `cpu_scheduler_gui.pyw`**. It opens the desktop GUI directly; no terminal commands or terminal menu are needed. The `.pyw` launcher uses the installed Python windowed association.

## GUI behavior

The normal application launch opens the GUI. It automatically compiles the C++ backend on first use (or when its sources change). On Linux/macOS, the Python entry point can also be launched with:

```sh
python3 cpu_scheduler_gui.py
```

The GUI supports process add/edit/delete/clear, reproducible random workloads, CSV import/export, all six scheduling policies, quantum/overhead/aging controls, per-process and aggregate metrics, all-policy comparison with per-metric best markers, result/comparison CSV exports, a horizontally scrollable Gantt chart, a complete execution timeline, and decision-by-decision stepping that shows the ready set, selected process, and remaining bursts. The GUI limits the drawn chart to the first 500 intervals for responsiveness while retaining every interval in the timeline table and exports.

The C++ executable also accepts `--test` to run the regression tests:

```sh
./scheduler --test
```

The original terminal menu is available with `./scheduler --terminal`.

Or compile the test runner separately:

```sh
g++ -std=c++17 tests.cpp -o scheduler_tests
./scheduler_tests
```

## Menu

```text
========================================
       CPU SCHEDULING SIMULATOR
========================================
1. Process Management
2. Run Scheduling Algorithm
3. Compare All Algorithms
4. Gantt Chart
5. Algorithm Analysis
6. Performance Benchmarking
7. Starvation Demonstration
8. Priority Aging
9. Step-by-Step Simulation
10. Settings
11. Export Results
0. Exit
========================================
```

A typical output includes a process-statistics table, aggregate metrics, Gantt bands, and explicit timeline intervals. Example for P1(AT=0,BT=3), P2(AT=1,BT=2), P3(AT=2,BT=1) under FCFS:

```text
=== FCFS ===
Process           AT      BT      CT     TAT      WT      RT
P1                 0       3       3       3       0       0
P2                 1       2       5       4       2       2
P3                 2       1       6       4       3       3
Average waiting time:    1.67
Average turnaround time: 3.67
Average response time:   1.67
CPU utilization:         100.00%
Throughput:              0.5000 processes/time-unit
Context switches:        2

+---------+---------+---------+
|   P1    |   P2    |   P3    |
+---------+---------+---------+
0         3         5         6
```

Gantt output is split into bounded-width bands for long traces. `IDLE` denotes an idle CPU interval; `CS` denotes context-switch overhead.

## Input CSV

Four columns are required: `ProcessID,ArrivalTime,BurstTime,Priority`. A header is accepted. The bundled `sample_processes.csv` is a working example. IDs must be unique and non-empty; arrival must be nonnegative; burst and Round Robin quantum must be positive; overhead and aging interval must be nonnegative/positive as prompted.

## Metrics and simulation assumptions

For each process:

- `TAT = CT - AT`
- `WT = TAT - BT`
- `RT = First CPU Start Time - AT`

Overall:

- Average values are arithmetic means over all processes.
- CPU utilization is **useful process-execution time / elapsed simulation time × 100**. Context-switch overhead and idle intervals are in the elapsed-time denominator, but overhead is not counted as useful process work.
- Throughput is **number of completed processes / elapsed simulation time**. The clock starts at time zero, so initial idle time before the first arrival is included.
- A context switch is a dispatch transition to a different process after a prior process has run. The initial dispatch is not counted. A transition after idle still counts if it changes process. The configured overhead is charged on these transitions, not on the initial dispatch or when the same process continues.
- Overhead appears as a `CS` timeline interval and affects wall-clock completion, turnaround, waiting, response, utilization, and throughput, but never increases a process's burst execution. The scheduler retains the process selected before overhead; arrivals during overhead are added to the ready structure but do not trigger a second selection until the following scheduling event.
- CPU time is modeled in integer units. The simulation begins at time zero and supports nonnegative arrivals and positive integer bursts.

## Deterministic policy rules

- For equal arrival times, input order is the tie-break. For SJF, SRTF, and priority ties, earlier arrival then input order wins. Priority values use **smaller number = higher priority**.
- FCFS dispatches ready jobs in arrival/input order. SJF uses original burst length; SRTF uses remaining burst and reconsiders at each arrival.
- Priority aging is available for either priority policy and uses `effectivePriority = basePriority - floor(readyWait / agingInterval)` without a lower bound; only waiting time accumulates (CPU execution does not). Non-preemptive priority recalculates at dispatches; preemptive priority also reconsiders at arrivals and aging boundaries. Ties still use arrival/input order.
- Round Robin uses a FIFO ready queue. Arrivals at a quantum boundary enter the queue before the just-expired process is requeued. A nonpositive quantum is rejected.
- Adjacent identical timeline labels are coalesced; non-adjacent repeated execution remains distinct.

These are explicit simulation assumptions, not universal operating-system rules.

## Algorithm overview and complexity

The interactive **Algorithm Analysis** module provides each algorithm's working principle, pseudocode, trade-offs, and examples. Complexity here describes this implementation: arrivals are sorted once in `O(n log n)`, and the non-RR policies scan the active ready set at each scheduling event. If `e` is the number of scheduling events, their candidate-selection work is `O(n·e)`; without aging, event counts are `O(n)` and worst-case work is `O(n²)`. Enabled priority aging can add boundary events proportional to elapsed time divided by the aging interval, so its event count is workload/time dependent. RR uses FIFO queue operations in `O(1)` per slice, for `O(n log n + s)` including arrival sorting, where `s` is the number of slices. Space is `O(n)` for scheduler state, plus `O(e)` when the timeline/step trace is retained. The exact mathematical average-case is workload-dependent, so a universal average bound is not asserted.

Qualitatively: FCFS is simple but can have convoy delays; SJF may reduce average waiting when burst lengths are known but can starve long jobs; SRTF improves response to shorter arrivals but adds preemption and can starve long work; priority scheduling expresses importance but may starve low priorities; Round Robin gives time-sharing fairness with a quantum-dependent overhead/response tradeoff. Priority aging reduces prolonged waits by promoting waiting work. No algorithm is universally best.

## Benchmarking methodology

The benchmark uses `std::chrono::steady_clock` around each scheduler call and keeps dataset generation and terminal printing outside the timed interval. It generates deterministic workloads at 10, 100, 1,000, and 10,000 processes using the entered seed (with separate deterministic seeds per workload size). It displays elapsed milliseconds for each of the six algorithms and supports CSV export. These are actual measurements from the current machine/compiler, not fabricated sample data; results depend on hardware, compiler, workload distribution, and implementation. The benchmark's process arrivals, bursts, and priorities are generated from fixed ranges in `main.cpp`. It is an educational comparison, not a statistically controlled performance study.

## Tests

The C++ test runner checks all six algorithms, single-process and idle cases, simultaneous and later arrivals, SJF/SRTF differences, preemption, priority ties, Round Robin at multiple quantums and its boundary order, context-switch overhead, aging fairness, CSV round trips, invalid inputs, timeline continuity, exact burst execution, metric formulas, and a 2,000-process workload. Run `./scheduler --test` or build `tests.cpp` separately. Python's standard-library integration tests compile the backend and verify result/timeline/decision CSV output for all six policies with `python3 tests_gui.py`.

## Project structure

```text
main.cpp                 Interactive menu, management, analysis, benchmark, demos
scheduler.hpp            C++17 scheduling engine, policies, timeline and metrics
io.hpp                   CSV handling, terminal formatting and input helpers
cpu_scheduler_gui.py     Optional Tkinter desktop front-end using the C++ backend
cpu_scheduler_gui.pyw    Windowed Windows double-click launcher (no console)
tests_gui.py             Standard-library integration tests for the GUI backend contract
tests.hpp                Assertion-based regression suite
tests.cpp                Standalone test-runner entry point
sample_processes.csv     Example workload
docs/DESIGN.md           Detailed model and accounting assumptions
README.md                Usage and methodology
CHANGELOG.md             Release notes
LICENSE                  MIT license
.gitignore               Local build artifacts
```

## License

MIT; see [`LICENSE`](LICENSE).
