# Changelog

## [Unreleased]

- Added an optional Python/Tkinter desktop front-end that calls the existing C++17 scheduler backend.
- Added a machine-readable C++ backend mode for process results and execution timelines, plus GUI process management, CSV operations, comparisons, Gantt/timeline display, and step navigation.
- Documented GUI setup and added ignore rules for its generated executable and Python caches.

## [v1.0.0] - 2026-11-03

Initial release.

- Implemented FCFS, SJF, SRTF, non-preemptive and preemptive priority, and Round Robin.
- Added process management, reproducible random data, and CSV import/export.
- Added per-process/aggregate metrics, ASCII Gantt charts, execution timeline, algorithm comparison, and CSV export.
- Added terminal-based algorithm explanations, benchmarking, starvation/aging demonstrations, configurable context-switch overhead, and step-by-step traces.
- Added regression tests, usage documentation, sample CSV, and MIT license.
