#include "io.hpp"
#include "tests.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace cpusim {

struct Settings {
    int quantum = 2;
    int overhead = 0;
    int agingInterval = 5;
    unsigned int randomSeed = 2026;
};

static std::vector<Process> processes;
static Settings settings;
static std::vector<SchedulingResult> lastComparison;
static std::vector<std::vector<std::string>> lastBenchmarks;

static Algorithm chooseAlgorithm() {
    std::cout << "\nSelect algorithm:\n"
              << "1. FCFS\n2. SJF (Non-preemptive)\n3. SRTF\n"
              << "4. Priority (Non-preemptive)\n5. Priority (Preemptive)\n6. Round Robin\n";
    const int choice = readInt("Choice: ", 1, 6);
    return allAlgorithms()[choice - 1];
}

static void addProcess() {
    Process p;
    p.id = readText("Process ID: ");
    if (p.id.empty() || findProcess(processes, p.id) >= 0) { std::cout << "ID must be non-empty and unique.\n"; return; }
    p.arrival = readInt("Arrival time (>= 0): ", 0);
    p.burst = readInt("Burst time (> 0): ", 1);
    p.priority = readInt("Priority (smaller = higher): ", std::numeric_limits<int>::min());
    processes.push_back(p);
    std::cout << "Process added.\n";
}

static void editProcess() {
    const std::string id = readText("ID to edit: ");
    const int index = findProcess(processes, id);
    if (index < 0) { std::cout << "Process not found.\n"; return; }
    Process replacement;
    replacement.id = readText("New ID: ");
    const int duplicate = findProcess(processes, replacement.id);
    if (replacement.id.empty() || (duplicate >= 0 && duplicate != index)) { std::cout << "ID must be non-empty and unique.\n"; return; }
    replacement.arrival = readInt("Arrival time (>= 0): ", 0);
    replacement.burst = readInt("Burst time (> 0): ", 1);
    replacement.priority = readInt("Priority (smaller = higher): ", std::numeric_limits<int>::min());
    processes[index] = replacement;
    std::cout << "Process updated.\n";
}

static void deleteProcess() {
    const std::string id = readText("ID to delete: ");
    const int index = findProcess(processes, id);
    if (index < 0) { std::cout << "Process not found.\n"; return; }
    processes.erase(processes.begin() + index);
    std::cout << "Process deleted.\n";
}

static void generateProcesses() {
    const int count = readInt("Number of processes: ", 1, 100000);
    const int maxArrival = readInt("Maximum arrival time: ", 0);
    const int maxBurst = readInt("Maximum burst time: ", 1);
    const int maxPriority = readInt("Maximum priority (minimum is 1): ", 1);
    settings.randomSeed = static_cast<unsigned int>(readInt("Random seed: ", 0));
    std::mt19937 rng(settings.randomSeed);
    std::uniform_int_distribution<int> arrivalDist(0, maxArrival);
    std::uniform_int_distribution<int> burstDist(1, maxBurst);
    std::uniform_int_distribution<int> priorityDist(1, maxPriority);
    processes.clear();
    for (int i = 0; i < count; ++i)
        processes.push_back({"P" + std::to_string(i + 1), arrivalDist(rng), burstDist(rng), priorityDist(rng)});
    std::cout << "Generated " << count << " reproducible processes (seed " << settings.randomSeed << ").\n";
}

static void processManagement() {
    while (true) {
        std::cout << "\n--- Process Management ---\n1. Add\n2. Edit\n3. Delete\n4. View all\n5. Clear all\n6. Generate random\n7. Load CSV\n8. Save CSV\n0. Back\n";
        const int choice = readInt("Choice: ", 0, 8);
        if (choice == 0) return;
        if (choice == 1) addProcess();
        else if (choice == 2) editProcess();
        else if (choice == 3) deleteProcess();
        else if (choice == 4) printProcesses(processes);
        else if (choice == 5) { processes.clear(); std::cout << "All processes cleared.\n"; }
        else if (choice == 6) generateProcesses();
        else if (choice == 7) {
            const std::string path = readText("CSV path: ");
            std::string error;
            if (readProcessesCsv(path, processes, error)) std::cout << "Loaded " << processes.size() << " process(es).\n";
            else std::cout << "Load failed: " << error << '\n';
        } else if (choice == 8) {
            const std::string path = readText("CSV path: ");
            std::string error;
            if (writeProcessesCsv(path, processes, error)) std::cout << "Saved processes to " << path << ".\n";
            else std::cout << error << '\n';
        }
    }
}

static bool requireProcesses() {
    if (processes.empty()) { std::cout << "Add or load processes first.\n"; return false; }
    return true;
}

static SchedulingResult runSelected(bool details = true) {
    const Algorithm algorithm = chooseAlgorithm();
    int q = settings.quantum;
    if (algorithm == Algorithm::RoundRobin) q = readInt("Round Robin time quantum (> 0): ", 1);
    int aging = 0;
    if (algorithm == Algorithm::PriorityPreemptive || algorithm == Algorithm::PriorityNonPreemptive) {
        std::cout << "Use aging for this run? (1=yes, 0=no)\n";
        if (readInt("Choice: ", 0, 1)) aging = readInt("Aging interval (> 0): ", 1);
    }
    return schedule(processes, algorithm, q, settings.overhead, aging, details);
}

static void runOne() {
    if (!requireProcesses()) return;
    try { printResult(runSelected()); }
    catch (const std::exception& e) { std::cout << "Scheduling failed: " << e.what() << '\n'; }
}

static void compareAll() {
    if (!requireProcesses()) return;
    int q = readInt("Round Robin time quantum for comparison (> 0): ", 1);
    lastComparison.clear();
    for (Algorithm a : allAlgorithms())
        lastComparison.push_back(schedule(processes, a, q, settings.overhead, 0, false));
    auto minMetric = [&](auto getter) {
        double best = getter(lastComparison.front());
        for (const auto& r : lastComparison) best = std::min(best, static_cast<double>(getter(r)));
        return best;
    };
    auto maxMetric = [&](auto getter) {
        double best = getter(lastComparison.front());
        for (const auto& r : lastComparison) best = std::max(best, static_cast<double>(getter(r)));
        return best;
    };
    const double bestWait = minMetric([](const auto& r) { return r.metrics.avgWaiting; });
    const double bestTat = minMetric([](const auto& r) { return r.metrics.avgTurnaround; });
    const double bestResponse = minMetric([](const auto& r) { return r.metrics.avgResponse; });
    const double bestUtil = maxMetric([](const auto& r) { return r.metrics.cpuUtilization; });
    const double bestThroughput = maxMetric([](const auto& r) { return r.metrics.throughput; });
    const double bestSwitch = minMetric([](const auto& r) { return r.metrics.contextSwitches; });
    std::cout << "\nComparison (best metric values marked with *; ties are all marked):\n";
    std::cout << std::left << std::setw(28) << "Algorithm" << std::right << std::setw(12) << "Avg WT"
              << std::setw(12) << "Avg TAT" << std::setw(12) << "Avg RT" << std::setw(12) << "Util %"
              << std::setw(12) << "Throughput" << std::setw(12) << "Switches" << '\n';
    for (const auto& r : lastComparison) {
        const auto& m = r.metrics;
        auto star = [](double v, double best) { return std::abs(v - best) < 1e-9 ? "*" : ""; };
        std::cout << std::left << std::setw(28) << r.algorithm << std::right
                  << std::setw(9) << fixed(m.avgWaiting) << star(m.avgWaiting, bestWait)
                  << std::setw(9) << fixed(m.avgTurnaround) << star(m.avgTurnaround, bestTat)
                  << std::setw(9) << fixed(m.avgResponse) << star(m.avgResponse, bestResponse)
                  << std::setw(9) << fixed(m.cpuUtilization) << star(m.cpuUtilization, bestUtil)
                  << std::setw(9) << fixed(m.throughput, 4) << star(m.throughput, bestThroughput)
                  << std::setw(9) << m.contextSwitches << star(m.contextSwitches, bestSwitch) << '\n';
    }
    std::cout << "A star marks a per-metric best for this dataset; no algorithm is universally best.\n";
}

static void algorithmAnalysis() {
    std::cout << "\n--- DAA Algorithm Analysis ---\n"
              << "Tie rule: earlier arrival wins; simultaneous ties follow input order. RR preserves FIFO arrival order; arrivals at a quantum boundary join before the expired process is requeued.\n"
              << "Preemptive scheduling may interrupt a running process at a scheduling event; non-preemptive scheduling holds it until completion.\n"
              << "Priority convention: smaller integer means higher priority. Aging can be enabled for either priority policy and lowers waiting-process effective priority by floor(wait/interval), with no lower bound; non-preemptive policy reevaluates at dispatch, preemptive policy also at aging boundaries/arrivals.\n"
              << "The implementation uses a sorted arrival list and scans the ready set for best candidates. Let n be process count and e be decision events: general non-RR selection is O(n*e), worst O(n^2) without aging; enabled aging adds events proportional to elapsed time / aging interval. Arrival ordering adds O(n log n). RR queue operations are O(1) per slice, so O(n log n + s), with s the number of slices. Space is O(n+e) when trace is retained, O(n) without trace.\n\n";
    std::cout << "FCFS — Serve ready processes by arrival order.\n"
              << "Pseudocode: sort by arrival/input order; for each process, jump to its arrival if idle, then run to completion.\n"
              << "Best/average/worst selection work: O(n log n) / O(n^2) / O(n^2) in this scan-based implementation; space O(n).\n"
              << "Advantages: simple, predictable, no starvation. Disadvantages: convoy effect and poor response for short jobs. Applications: simple batch queues.\n\n";
    std::cout << "SJF (non-preemptive) — Choose the shortest burst among ready jobs, then run it to completion.\n"
              << "Pseudocode: add arrivals; choose minimum burst (arrival/input order tie); execute fully; repeat.\n"
              << "Best/average/worst selection work: O(n log n) / O(n^2) / O(n^2); space O(n).\n"
              << "Advantages: low average waiting when bursts are known. Disadvantages: long jobs can starve; exact future burst is often unknown. Applications: batch workloads with estimates.\n\n";
    std::cout << "SRTF — Choose the smallest remaining burst, reconsidering at each arrival.\n"
              << "Pseudocode: while unfinished, select ready minimum remaining; run until completion or next arrival; repeat.\n"
              << "Best/average/worst selection work: O(n log n) / O(n*e) / O(n^2); space O(n).\n"
              << "Advantages: responsive to short jobs. Disadvantages: preemption/context overhead and possible starvation of long jobs. Applications: interactive/batch systems with burst estimates.\n\n";
    std::cout << "Priority non-preemptive — Select the ready process with the smallest priority number and run to completion.\n"
              << "Pseudocode: add arrivals; choose minimum priority (then arrival/input order); execute fully.\n"
              << "Best/average/worst selection work: O(n log n) / O(n^2) / O(n^2); space O(n).\n"
              << "Advantages: expresses importance simply. Disadvantages: low-priority starvation. Applications: service classes and urgent batch jobs.\n\n";
    std::cout << "Priority preemptive — Select the smallest effective priority; reassess on arrivals (and aging interval boundaries, if enabled).\n"
              << "Pseudocode: select highest-priority ready job; run to the next arrival, completion, or aging boundary; reassess.\n"
              << "Best/average/worst selection work: O(n log n) / O(n*e) / O(n^2); space O(n).\n"
              << "Advantages: urgent work can preempt. Disadvantages: starvation and context-switch overhead. Aging improves fairness but changes ordering. Applications: real-time-like priority queues (not hard-real-time guarantees).\n\n";
    std::cout << "Round Robin — Give each ready job up to q time units in FIFO order.\n"
              << "Pseudocode: pop head; execute min(q,remaining); enqueue arrivals at the boundary; requeue unfinished job.\n"
              << "Best/average/worst: O(n log n+s) for this queue implementation, s slices; space O(n+s) with trace.\n"
              << "Advantages: fair time sharing and bounded first response. Disadvantages: quantum selection trades overhead against responsiveness. Applications: interactive time sharing.\n\n";
    std::cout << "Fairness and starvation: RR provides bounded turn-taking among continuously ready jobs; FCFS avoids indefinite overtaking but can delay later jobs. SJF/SRTF and fixed priority can starve long/low-priority work. Aging gradually promotes waiting jobs, though priority selection and finite workloads still determine exact fairness.\n";
}

static std::vector<Process> randomDataset(int count, unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> arrival(0, std::max(1, count * 2));
    std::uniform_int_distribution<int> burst(1, 20);
    std::uniform_int_distribution<int> priority(1, 10);
    std::vector<Process> data;
    data.reserve(count);
    for (int i = 0; i < count; ++i) data.push_back({"P" + std::to_string(i + 1), arrival(rng), burst(rng), priority(rng)});
    return data;
}

static void benchmarking() {
    const unsigned int seed = static_cast<unsigned int>(readInt("Reproducible benchmark seed: ", 0));
    lastBenchmarks.clear();
    std::cout << "\nBenchmark runtimes (milliseconds; timing excludes output and dataset creation):\n";
    std::cout << std::left << std::setw(27) << "Algorithm" << std::right << std::setw(12) << "n=10"
              << std::setw(12) << "n=100" << std::setw(12) << "n=1000" << std::setw(12) << "n=10000" << '\n';
    const int sizes[] = {10, 100, 1000, 10000};
    std::vector<std::vector<Process>> datasets;
    for (int i = 0; i < 4; ++i) datasets.push_back(randomDataset(sizes[i], seed + i));
    for (Algorithm algorithm : allAlgorithms()) {
        std::vector<std::string> row{algorithmName(algorithm)};
        for (const auto& data : datasets) {
            const auto start = std::chrono::steady_clock::now();
            const auto result = schedule(data, algorithm, settings.quantum, settings.overhead, 0, false);
            const auto stop = std::chrono::steady_clock::now();
            (void)result;
            const double ms = std::chrono::duration<double, std::milli>(stop - start).count();
            row.push_back(fixed(ms, 3));
        }
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i == 0) std::cout << std::left << std::setw(27) << row[i];
            else std::cout << std::right << std::setw(12) << row[i];
        }
        std::cout << '\n';
        lastBenchmarks.push_back(row);
    }
    std::cout << "Theoretical note: sorted arrivals cost O(n log n); ready-set selection is O(n) per decision in the scan-based policies (worst O(n^2) overall), while RR queue work is O(number of slices). Measurements vary with hardware/compiler; this small benchmark is not a universal ranking. Seed=" << seed << ".\n";
}

static void starvationDemo() {
    const std::vector<Process> data = {
        {"LOW", 0, 1, 3}, {"H1", 0, 3, 2}, {"H2", 2, 3, 2},
        {"H3", 4, 3, 2}, {"H4", 6, 3, 2}, {"H5", 8, 3, 2}, {"H6", 10, 3, 2}
    };
    std::cout << "Fixed-priority preemptive workload; LOW has lower priority and may wait behind arriving work.\n";
    const auto r = schedule(data, Algorithm::PriorityPreemptive, settings.quantum, settings.overhead, 0);
    printResult(r);
}

static void agingDemo() {
    const std::vector<Process> data = {
        {"LOW", 0, 1, 3}, {"H1", 0, 3, 2}, {"H2", 2, 3, 2},
        {"H3", 4, 3, 2}, {"H4", 6, 3, 2}, {"H5", 8, 3, 2}, {"H6", 10, 3, 2}
    };
    const auto without = schedule(data, Algorithm::PriorityPreemptive, settings.quantum, settings.overhead, 0);
    const auto with = schedule(data, Algorithm::PriorityPreemptive, settings.quantum, settings.overhead, settings.agingInterval);
    std::cout << "Without aging (waiting times):\n";
    for (const auto& s : without.processes) std::cout << "  " << s.process.id << ": " << s.waiting << '\n';
    std::cout << "With aging interval " << settings.agingInterval << " (effective priority = base - floor(ready wait / interval)):\n";
    for (const auto& s : with.processes) std::cout << "  " << s.process.id << ": " << s.waiting << '\n';
    std::cout << "Aging can reduce prolonged low-priority waits; it does not promise universally optimal averages.\n";
    printGantt(with);
}

static void stepSimulation() {
    if (!requireProcesses()) return;
    try {
        const auto result = runSelected();
        std::cout << "Decision trace for " << result.algorithm << ":\n";
        for (std::size_t i = 0; i < result.decisions.size(); ++i) {
            const auto& d = result.decisions[i];
            std::cout << "\nStep " << i + 1 << " at t=" << d.time << "\n"
                      << "Ready queue/set: " << d.ready << "\n"
                      << "Selected / running: " << d.selected << "\n"
                      << "Remaining burst before decision: " << d.remaining << '\n';
            if (i + 1 < result.decisions.size()) {
                std::cout << "Press Enter for the next decision, or type q to stop: ";
                std::string line;
                if (!std::getline(std::cin, line) || line == "q" || line == "Q") break;
            }
        }
        printGantt(result);
    } catch (const std::exception& e) { std::cout << "Simulation failed: " << e.what() << '\n'; }
}

static void settingsMenu() {
    while (true) {
        std::cout << "\n--- Settings ---\n1. Default RR quantum: " << settings.quantum
                  << "\n2. Context-switch overhead: " << settings.overhead
                  << "\n3. Aging interval: " << settings.agingInterval
                  << "\n4. Random seed: " << settings.randomSeed << "\n0. Back\n";
        const int choice = readInt("Choice: ", 0, 4);
        if (choice == 0) return;
        if (choice == 1) settings.quantum = readInt("New quantum (> 0): ", 1);
        else if (choice == 2) settings.overhead = readInt("Overhead time units (>= 0): ", 0);
        else if (choice == 3) settings.agingInterval = readInt("Aging interval (> 0): ", 1);
        else if (choice == 4) settings.randomSeed = static_cast<unsigned int>(readInt("Seed: ", 0));
    }
}

static void exportMenu() {
    std::cout << "\n1. Export current process dataset to CSV\n2. Export last comparison\n3. Export last benchmark\n0. Back\n";
    const int choice = readInt("Choice: ", 0, 3);
    const std::string path = choice == 0 ? "" : readText("Output CSV path: ");
    std::string error;
    if (choice == 1) {
        if (writeProcessesCsv(path, processes, error)) std::cout << "Exported processes.\n"; else std::cout << error << '\n';
    } else if (choice == 2) {
        if (lastComparison.empty()) { std::cout << "Run a comparison first.\n"; return; }
        if (writeComparisonCsv(path, lastComparison, error)) std::cout << "Exported comparison.\n"; else std::cout << error << '\n';
    } else if (choice == 3) {
        if (lastBenchmarks.empty()) { std::cout << "Run a benchmark first.\n"; return; }
        std::ofstream out(path);
        if (!out) { std::cout << "Could not write " << path << '\n'; return; }
        out << "Algorithm,n=10_ms,n=100_ms,n=1000_ms,n=10000_ms\n";
        for (const auto& row : lastBenchmarks) {
            for (std::size_t i = 0; i < row.size(); ++i) { if (i) out << ','; out << csvEscape(row[i]); }
            out << '\n';
        }
        if (!out) std::cout << "Write failed.\n"; else std::cout << "Exported benchmark.\n";
    }
}

static int parseArgumentInt(const char* text, const std::string& name) {
    const std::string value = text;
    std::size_t used = 0;
    const int parsed = std::stoi(value, &used);
    if (used != value.size()) throw std::invalid_argument("Invalid " + name + ": " + value);
    return parsed;
}

static Algorithm parseAlgorithmKey(const std::string& key) {
    if (key == "fcfs") return Algorithm::FCFS;
    if (key == "sjf") return Algorithm::SJF;
    if (key == "srtf") return Algorithm::SRTF;
    if (key == "priority-np") return Algorithm::PriorityNonPreemptive;
    if (key == "priority-p") return Algorithm::PriorityPreemptive;
    if (key == "rr") return Algorithm::RoundRobin;
    throw std::invalid_argument("Unknown algorithm key: " + key);
}

static void writeGuiRow(std::ofstream& out, const std::vector<std::string>& fields) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) out << ',';
        out << csvEscape(fields[i]);
    }
    out << '\n';
}

static int runGuiBackend(int argc, char* argv[]) {
    if (argc != 10) {
        std::cerr << "Usage: scheduler --gui-run <algorithm> <processes.csv> <results.csv> <timeline.csv> <decisions.csv> <quantum> <overhead> <aging-interval>\n";
        return 2;
    }
    try {
        const Algorithm algorithm = parseAlgorithmKey(argv[2]);
        const int quantum = parseArgumentInt(argv[7], "quantum");
        const int overhead = parseArgumentInt(argv[8], "overhead");
        const int agingInterval = parseArgumentInt(argv[9], "aging interval");
        std::vector<Process> input;
        std::string error;
        if (!readProcessesCsv(argv[3], input, error)) throw std::runtime_error(error);
        const SchedulingResult result = schedule(input, algorithm, quantum, overhead, agingInterval, true);

        std::ofstream processOut(argv[4]);
        if (!processOut) throw std::runtime_error("Could not write results CSV: " + std::string(argv[4]));
        writeGuiRow(processOut, {"Type", "ProcessID", "Arrival", "Burst", "Priority", "Completion", "Turnaround", "Waiting", "Response", "FirstStart", "AverageWaiting", "AverageTurnaround", "AverageResponse", "CPUUtilizationPercent", "Throughput", "ContextSwitches"});
        for (const auto& s : result.processes) {
            std::vector<std::string> row(16);
            row[0] = "PROCESS"; row[1] = s.process.id;
            row[2] = std::to_string(s.process.arrival); row[3] = std::to_string(s.process.burst);
            row[4] = std::to_string(s.process.priority); row[5] = std::to_string(s.completion);
            row[6] = std::to_string(s.turnaround); row[7] = std::to_string(s.waiting);
            row[8] = std::to_string(s.response); row[9] = std::to_string(s.firstStart);
            writeGuiRow(processOut, row);
        }
        std::vector<std::string> metricRow(16);
        metricRow[0] = "METRICS";
        metricRow[10] = std::to_string(result.metrics.avgWaiting);
        metricRow[11] = std::to_string(result.metrics.avgTurnaround);
        metricRow[12] = std::to_string(result.metrics.avgResponse);
        metricRow[13] = std::to_string(result.metrics.cpuUtilization);
        metricRow[14] = std::to_string(result.metrics.throughput);
        metricRow[15] = std::to_string(result.metrics.contextSwitches);
        writeGuiRow(processOut, metricRow);
        if (!processOut) throw std::runtime_error("Failed while writing results CSV.");

        std::ofstream timelineOut(argv[5]);
        if (!timelineOut) throw std::runtime_error("Could not write timeline CSV: " + std::string(argv[5]));
        writeGuiRow(timelineOut, {"Start", "End", "Activity"});
        for (const auto& segment : result.timeline)
            writeGuiRow(timelineOut, {std::to_string(segment.start), std::to_string(segment.end), segment.label});
        if (!timelineOut) throw std::runtime_error("Failed while writing timeline CSV.");

        std::ofstream decisionsOut(argv[6]);
        if (!decisionsOut) throw std::runtime_error("Could not write decisions CSV: " + std::string(argv[6]));
        writeGuiRow(decisionsOut, {"Time", "Ready", "Selected", "Remaining"});
        for (const auto& decision : result.decisions)
            writeGuiRow(decisionsOut, {std::to_string(decision.time), decision.ready, decision.selected, decision.remaining});
        if (!decisionsOut) throw std::runtime_error("Failed while writing decisions CSV.");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "GUI backend error: " << e.what() << '\n';
        return 2;
    }
}

static int launchGui(const char* executable) {
    std::error_code pathError;
    std::filesystem::path binaryPath = std::filesystem::absolute(executable, pathError);
    if (pathError) binaryPath = std::filesystem::path(executable);
    const std::filesystem::path directory = binaryPath.parent_path();
#ifdef _WIN32
    const std::filesystem::path guiEntry = directory / "cpu_scheduler_gui.pyw";
#else
    const std::filesystem::path guiEntry = directory / "cpu_scheduler_gui.py";
#endif
    if (!std::filesystem::exists(guiEntry)) {
        std::cerr << "Could not find the GUI launcher beside the application: " << guiEntry.string() << '\n';
        return 2;
    }

#ifdef _WIN32
    const HINSTANCE opened = ShellExecuteW(nullptr, L"open", guiEntry.c_str(), nullptr, directory.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(opened) <= 32) {
        std::cerr << "Could not open the GUI. Use the cpu_scheduler_gui.pyw file association or install Python.\n";
        return 2;
    }
    return 0;
#else
    const pid_t child = fork();
    if (child < 0) {
        std::cerr << "Could not start the GUI process.\n";
        return 2;
    }
    if (child == 0) {
        execlp("python3", "python3", guiEntry.c_str(), static_cast<char*>(nullptr));
        execlp("python", "python", guiEntry.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 2;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

static void showMenu() {
    std::cout << "\n========================================\n"
              << "       CPU SCHEDULING SIMULATOR\n"
              << "========================================\n"
              << "1. Process Management\n2. Run Scheduling Algorithm\n3. Compare All Algorithms\n"
              << "4. Gantt Chart\n5. Algorithm Analysis\n6. Performance Benchmarking\n"
              << "7. Starvation Demonstration\n8. Priority Aging\n9. Step-by-Step Simulation\n"
              << "10. Settings\n11. Export Results\n0. Exit\n"
              << "========================================\n";
}

} // namespace cpusim

int main(int argc, char* argv[]) {
    using namespace cpusim;
    if (argc > 1 && std::string(argv[1]) == "--test") return runTests();
    if (argc > 1 && std::string(argv[1]) == "--gui-run") return runGuiBackend(argc, argv);
    if (argc == 1) return launchGui(argv[0]);
    if (argc > 1 && std::string(argv[1]) != "--terminal") {
        std::cerr << "Usage: scheduler [--terminal | --test]\n";
        return 2;
    }
    while (true) {
        showMenu();
        const int choice = readInt("Choice: ", 0, 11);
        if (!std::cin && choice < 0) break;
        if (choice == 0) { std::cout << "Goodbye.\n"; break; }
        switch (choice) {
            case 1: processManagement(); break;
            case 2: runOne(); break;
            case 3: compareAll(); break;
            case 4: if (requireProcesses()) printGantt(runSelected()); break;
            case 5: algorithmAnalysis(); break;
            case 6: benchmarking(); break;
            case 7: starvationDemo(); break;
            case 8: agingDemo(); break;
            case 9: stepSimulation(); break;
            case 10: settingsMenu(); break;
            case 11: exportMenu(); break;
        }
    }
    return 0;
}
