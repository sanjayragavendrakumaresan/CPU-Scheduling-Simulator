# Changelog

## [Unreleased]

- Added a Python/Tkinter desktop front-end that calls the existing C++17 scheduler backend.
- Moved GUI workload generation, CSV validation/normalization, comparison ranking, and result exports into C++; Python remains the Tkinter interface and thin data bridge.
- Added C++ backend commands for process results, timelines, and step-by-step decision traces.
- Documented GUI setup and added ignore rules for its generated executable and Python caches.
- Made the GUI the default application startup and added a Windows `.pyw` double-click launcher; the terminal menu is now opt-in with `--terminal`.

## [v1.0.0] - 2026-11-03

Initial release.

- Implemented FCFS, SJF, SRTF, non-preemptive and preemptive priority, and Round Robin.
- Added process management, reproducible random data, and CSV import/export.
- Added per-process/aggregate metrics, ASCII Gantt charts, execution timeline, algorithm comparison, and CSV export.
- Added terminal-based algorithm explanations, benchmarking, starvation/aging demonstrations, configurable context-switch overhead, and step-by-step traces.
- Added regression tests, usage documentation, sample CSV, and MIT license.
