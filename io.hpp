#pragma once

#include "scheduler.hpp"
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace cpusim {

inline std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

inline std::vector<std::string> csvFields(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
            else quoted = !quoted;
        } else if (ch == ',' && !quoted) {
            fields.push_back(trim(field)); field.clear();
        } else field += ch;
    }
    if (quoted) throw std::runtime_error("Unclosed quote in CSV row.");
    fields.push_back(trim(field));
    return fields;
}

inline std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) return value;
    std::string out = "\"";
    for (char ch : value) { if (ch == '"') out += '"'; out += ch; }
    return out + '"';
}

inline bool readProcessesCsv(const std::string& path, std::vector<Process>& processes, std::string& error) {
    std::ifstream in(path);
    if (!in) { error = "Could not open file: " + path; return false; }
    std::vector<Process> loaded;
    std::string line;
    int row = 0;
    try {
        while (std::getline(in, line)) {
            ++row;
            if (trim(line).empty()) continue;
            const auto f = csvFields(line);
            if (row == 1 && f.size() >= 3 && (f[0] == "ProcessID" || f[0] == "ID" || f[0] == "Process ID")) continue;
            if (f.size() != 4) throw std::runtime_error("Expected four columns at row " + std::to_string(row));
            std::size_t usedA = 0, usedB = 0, usedP = 0;
            const int arrival = std::stoi(f[1], &usedA);
            const int burst = std::stoi(f[2], &usedB);
            const int priority = std::stoi(f[3], &usedP);
            if (usedA != f[1].size() || usedB != f[2].size() || usedP != f[3].size())
                throw std::runtime_error("Invalid numeric value at row " + std::to_string(row));
            loaded.push_back({f[0], arrival, burst, priority});
        }
        validateProcesses(loaded);
    } catch (const std::exception& e) { error = e.what(); return false; }
    processes = std::move(loaded);
    error.clear();
    return true;
}

inline bool writeProcessesCsv(const std::string& path, const std::vector<Process>& processes, std::string& error) {
    std::ofstream out(path);
    if (!out) { error = "Could not write file: " + path; return false; }
    out << "ProcessID,ArrivalTime,BurstTime,Priority\n";
    for (const auto& p : processes)
        out << csvEscape(p.id) << ',' << p.arrival << ',' << p.burst << ',' << p.priority << '\n';
    if (!out) { error = "Write failed for: " + path; return false; }
    error.clear();
    return true;
}

inline std::string fixed(double value, int precision = 2) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

inline void printProcesses(const std::vector<Process>& processes) {
    if (processes.empty()) { std::cout << "No processes loaded.\n"; return; }
    std::cout << std::left << std::setw(12) << "Process" << std::right << std::setw(12) << "Arrival"
              << std::setw(12) << "Burst" << std::setw(12) << "Priority" << '\n';
    std::cout << std::string(48, '-') << '\n';
    for (const auto& p : processes)
        std::cout << std::left << std::setw(12) << p.id << std::right << std::setw(12) << p.arrival
                  << std::setw(12) << p.burst << std::setw(12) << p.priority << '\n';
}

inline void printGantt(const SchedulingResult& result, std::size_t perLine = 8) {
    if (result.timeline.empty()) { std::cout << "(empty timeline)\n"; return; }
    std::cout << "Gantt chart (each band contains at most " << perLine << " intervals):\n";
    for (std::size_t begin = 0; begin < result.timeline.size(); begin += perLine) {
        const std::size_t end = std::min(result.timeline.size(), begin + perLine);
        std::vector<int> widths;
        for (std::size_t i = begin; i < end; ++i)
            widths.push_back(std::max<int>(7, static_cast<int>(result.timeline[i].label.size()) + 2));
        std::cout << '+';
        for (int width : widths) std::cout << std::string(width, '-') << '+';
        std::cout << '\n' << '|';
        for (std::size_t i = begin; i < end; ++i) {
            const int width = widths[i - begin];
            const std::string label = result.timeline[i].label;
            const int left = (width - static_cast<int>(label.size())) / 2;
            std::cout << std::string(left, ' ') << label << std::string(width - left - label.size(), ' ') << '|';
        }
        std::cout << '\n' << '+';
        for (int width : widths) std::cout << std::string(width, '-') << '+';
        std::cout << '\n' << std::left << std::setw(1) << result.timeline[begin].start;
        for (std::size_t i = begin; i < end; ++i) std::cout << std::right << std::setw(widths[i - begin] + 1) << result.timeline[i].end;
        std::cout << "\n\n";
    }
}

inline void printResult(const SchedulingResult& result, bool showChart = true) {
    std::cout << "\n=== " << result.algorithm << " ===\n";
    std::cout << std::left << std::setw(12) << "Process" << std::right << std::setw(8) << "AT"
              << std::setw(8) << "BT" << std::setw(8) << "CT" << std::setw(8) << "TAT"
              << std::setw(8) << "WT" << std::setw(8) << "RT" << '\n';
    for (const auto& s : result.processes)
        std::cout << std::left << std::setw(12) << s.process.id << std::right << std::setw(8) << s.process.arrival
                  << std::setw(8) << s.process.burst << std::setw(8) << s.completion << std::setw(8) << s.turnaround
                  << std::setw(8) << s.waiting << std::setw(8) << s.response << '\n';
    const auto& m = result.metrics;
    std::cout << "Average waiting time:    " << fixed(m.avgWaiting) << "\n"
              << "Average turnaround time: " << fixed(m.avgTurnaround) << "\n"
              << "Average response time:   " << fixed(m.avgResponse) << "\n"
              << "CPU utilization:         " << fixed(m.cpuUtilization) << "%\n"
              << "Throughput:              " << fixed(m.throughput, 4) << " processes/time-unit\n"
              << "Context switches:        " << m.contextSwitches << '\n';
    if (showChart) printGantt(result);
    std::cout << "Execution timeline (start -> end | activity):\n";
    for (const auto& s : result.timeline)
        std::cout << "  " << s.start << " -> " << s.end << " | " << s.label << '\n';
}

inline bool writeComparisonCsv(const std::string& path, const std::vector<SchedulingResult>& results, std::string& error) {
    std::ofstream out(path);
    if (!out) { error = "Could not write file: " + path; return false; }
    out << "Algorithm,AverageWaiting,AverageTurnaround,AverageResponse,CPUUtilizationPercent,Throughput,ContextSwitches\n";
    for (const auto& r : results) {
        const auto& m = r.metrics;
        out << csvEscape(r.algorithm) << ',' << m.avgWaiting << ',' << m.avgTurnaround << ',' << m.avgResponse << ','
            << m.cpuUtilization << ',' << m.throughput << ',' << m.contextSwitches << '\n';
    }
    if (!out) { error = "Write failed for: " + path; return false; }
    error.clear(); return true;
}

inline int readInt(const std::string& prompt, int minimum, int maximum = std::numeric_limits<int>::max()) {
    while (true) {
        std::cout << prompt;
        std::string line;
        if (!std::getline(std::cin, line)) return minimum - 1;
        try {
            std::size_t used = 0;
            const int value = std::stoi(trim(line), &used);
            if (used == trim(line).size() && value >= minimum && value <= maximum) return value;
        } catch (...) {}
        std::cout << "Enter a whole number in [" << minimum << ", " << maximum << "].\n";
    }
}

inline std::string readText(const std::string& prompt) {
    std::cout << prompt;
    std::string value;
    std::getline(std::cin, value);
    return trim(value);
}

inline int findProcess(const std::vector<Process>& processes, const std::string& id) {
    for (std::size_t i = 0; i < processes.size(); ++i) if (processes[i].id == id) return static_cast<int>(i);
    return -1;
}

} // namespace cpusim
