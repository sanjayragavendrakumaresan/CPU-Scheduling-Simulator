#pragma once

#include "io.hpp"
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace cpusim {

inline void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

inline void expectSchedule(const SchedulingResult& r, const std::vector<std::string>& expected) {
    std::vector<std::string> actual;
    for (const auto& s : r.timeline) actual.push_back(s.label + "[" + std::to_string(s.start) + "," + std::to_string(s.end) + ")");
    expect(actual == expected, r.algorithm + " schedule mismatch");
}

inline void checkInvariants(const std::vector<Process>& input, const SchedulingResult& r) {
    expect(r.processes.size() == input.size(), "process result count");
    int previousEnd = 0;
    for (const auto& segment : r.timeline) {
        expect(segment.start == previousEnd, "timeline has a gap or overlap");
        expect(segment.end > segment.start, "invalid timeline interval");
        previousEnd = segment.end;
    }
    for (const auto& s : r.processes) {
        int executed = 0, lastEnd = -1, first = -1;
        for (const auto& segment : r.timeline) if (segment.label == s.process.id) {
            executed += segment.end - segment.start;
            if (first < 0) first = segment.start;
            lastEnd = segment.end;
        }
        expect(executed == s.process.burst, "process burst not executed exactly: " + s.process.id);
        expect(lastEnd == s.completion, "completion time mismatch: " + s.process.id);
        expect(first == s.firstStart, "first start mismatch: " + s.process.id);
        expect(s.turnaround == s.completion - s.process.arrival, "turnaround formula mismatch");
        expect(s.waiting == s.turnaround - s.process.burst && s.waiting >= 0, "waiting formula mismatch");
        expect(s.response == s.firstStart - s.process.arrival && s.response >= 0, "response formula mismatch");
    }
}

inline int runTests() {
    int passed = 0;
    auto test = [&](const std::string& name, const auto& fn) {
        fn(); ++passed; std::cout << "[PASS] " << name << '\n';
    };
    try {
        test("idle gap and single process", [&] {
            const std::vector<Process> p{{"P1", 3, 2, 1}};
            const auto r = schedule(p, Algorithm::FCFS);
            expectSchedule(r, {"IDLE[0,3)", "P1[3,5)"});
            const auto& s = r.processes[0];
            expect(s.completion == 5 && s.turnaround == 2 && s.waiting == 0 && s.response == 0, "single-process metrics");
            expect(std::abs(r.metrics.cpuUtilization - 40.0) < 1e-9, "idle utilization");
            checkInvariants(p, r);
        });
        test("FCFS tie and arrival order", [&] {
            const std::vector<Process> p{{"P1", 0, 3, 1}, {"P2", 0, 2, 1}, {"P3", 1, 1, 1}};
            const auto r = schedule(p, Algorithm::FCFS);
            expectSchedule(r, {"P1[0,3)", "P2[3,5)", "P3[5,6)"});
            expect(std::abs(r.metrics.avgWaiting - 7.0 / 3) < 1e-9, "FCFS average waiting");
            expect(r.metrics.contextSwitches == 2, "FCFS context switches");
            checkInvariants(p, r);
        });
        test("SJF versus SRTF preemption", [&] {
            const std::vector<Process> p{{"P1", 0, 7, 1}, {"P2", 2, 4, 1}, {"P3", 4, 1, 1}};
            expectSchedule(schedule(p, Algorithm::SJF), {"P1[0,7)", "P3[7,8)", "P2[8,12)"});
            const auto r = schedule(p, Algorithm::SRTF);
            expectSchedule(r, {"P1[0,2)", "P2[2,4)", "P3[4,5)", "P2[5,7)", "P1[7,12)"});
            expect(r.processes[0].completion == 12 && r.processes[0].waiting == 5 && r.processes[0].response == 0, "SRTF P1 metrics");
            expect(r.processes[1].completion == 7 && r.processes[1].waiting == 1 && r.processes[1].response == 0, "SRTF P2 metrics");
            expect(r.processes[2].completion == 5 && r.processes[2].waiting == 0 && r.processes[2].response == 0, "SRTF P3 metrics");
            checkInvariants(p, r);
        });
        test("priority preemption and equal priorities", [&] {
            const std::vector<Process> p{{"P1", 0, 4, 2}, {"P2", 1, 2, 1}, {"P3", 1, 1, 1}};
            expectSchedule(schedule(p, Algorithm::PriorityNonPreemptive), {"P1[0,4)", "P2[4,6)", "P3[6,7)"});
            const auto r = schedule(p, Algorithm::PriorityPreemptive);
            expectSchedule(r, {"P1[0,1)", "P2[1,3)", "P3[3,4)", "P1[4,7)"});
            checkInvariants(p, r);
        });
        test("round robin boundary order and quantum", [&] {
            const std::vector<Process> p{{"P1", 0, 5, 1}, {"P2", 1, 3, 1}, {"P3", 2, 1, 1}};
            const auto r = schedule(p, Algorithm::RoundRobin, 2);
            expectSchedule(r, {"P1[0,2)", "P2[2,4)", "P3[4,5)", "P1[5,7)", "P2[7,8)", "P1[8,9)"});
            expect(r.processes[0].completion == 9 && r.processes[0].waiting == 4, "RR P1 metrics");
            expect(r.processes[1].completion == 8 && r.processes[1].waiting == 4, "RR P2 metrics");
            expect(r.processes[2].completion == 5 && r.processes[2].waiting == 2, "RR P3 metrics");
            expectSchedule(schedule(p, Algorithm::RoundRobin, 1), {"P1[0,1)", "P2[1,2)", "P1[2,3)", "P3[3,4)", "P2[4,5)", "P1[5,6)", "P2[6,7)", "P1[7,9)"});
            checkInvariants(p, r);
        });
        test("context-switch overhead and metrics", [&] {
            const std::vector<Process> p{{"P1", 0, 2, 1}, {"P2", 0, 1, 1}};
            const auto r = schedule(p, Algorithm::FCFS, 2, 1);
            expectSchedule(r, {"P1[0,2)", "CS[2,3)", "P2[3,4)"});
            expect(r.processes[1].completion == 4 && r.processes[1].waiting == 3 && r.processes[1].response == 3, "overhead affects process metrics");
            expect(std::abs(r.metrics.cpuUtilization - 75.0) < 1e-9 && r.metrics.contextSwitches == 1, "overhead metrics");
            checkInvariants(p, r);
        });
        test("priority aging improves early wait", [&] {
            const std::vector<Process> p{{"LOW", 0, 2, 5}, {"H1", 0, 2, 1}, {"H2", 2, 2, 1}, {"H3", 4, 2, 1}, {"H4", 6, 2, 1}};
            const auto base = schedule(p, Algorithm::PriorityPreemptive);
            const auto aged = schedule(p, Algorithm::PriorityPreemptive, 2, 0, 1);
            expect(aged.processes[0].waiting < base.processes[0].waiting, "aging should reduce LOW's wait");
            checkInvariants(p, aged);
            const auto npBase = schedule(p, Algorithm::PriorityNonPreemptive);
            const auto npAged = schedule(p, Algorithm::PriorityNonPreemptive, 2, 0, 1);
            expect(npAged.processes[0].waiting < npBase.processes[0].waiting, "non-preemptive aging should reduce LOW's wait");
            checkInvariants(p, npAged);
        });
        test("all six algorithms and metric consistency", [&] {
            const std::vector<Process> p{{"A", 0, 4, 2}, {"B", 1, 2, 1}, {"C", 5, 3, 3}};
            expect(allAlgorithms().size() == 6, "six algorithms available");
            for (auto a : allAlgorithms()) checkInvariants(p, schedule(p, a, 2));
        });
        test("validation rejects bad workload and quantum", [&] {
            bool bad = false;
            try { schedule({{"P1", -1, 1, 1}}, Algorithm::FCFS); } catch (const std::invalid_argument&) { bad = true; }
            expect(bad, "negative arrival should be rejected");
            bad = false;
            try { schedule({{"P1", 0, 0, 1}}, Algorithm::FCFS); } catch (const std::invalid_argument&) { bad = true; }
            expect(bad, "zero burst should be rejected");
            bad = false;
            try { schedule({{"P1", 0, 1, 1}}, Algorithm::RoundRobin, 0); } catch (const std::invalid_argument&) { bad = true; }
            expect(bad, "zero quantum should be rejected");
            bad = false;
            try { schedule({{"P1", 0, 1, 1}, {"P1", 0, 2, 2}}, Algorithm::FCFS); } catch (const std::invalid_argument&) { bad = true; }
            expect(bad, "duplicate ID should be rejected");
        });
        test("CSV process round trip", [&] {
            const std::string path = "/tmp/cpu_scheduler_test.csv";
            const std::vector<Process> p{{"P, 1", 2, 3, -1}, {"P2", 0, 5, 4}};
            std::string error;
            expect(writeProcessesCsv(path, p, error), "write CSV");
            std::vector<Process> loaded;
            expect(readProcessesCsv(path, loaded, error), "read CSV: " + error);
            expect(loaded.size() == 2 && loaded[0].id == p[0].id && loaded[0].priority == -1, "CSV values round-trip");
            std::remove(path.c_str());
        });
        test("large deterministic workload", [&] {
            std::vector<Process> p;
            for (int i = 0; i < 2000; ++i) p.push_back({"P" + std::to_string(i), i % 101, 1 + i % 13, i % 9});
            const auto r = schedule(p, Algorithm::RoundRobin, 4, 0, 0, false);
            expect(r.processes.size() == p.size() && r.timeline.empty(), "large workload metrics-only execution");
            double total = 0;
            for (const auto& s : r.processes) { expect(s.waiting >= 0, "large workload negative waiting"); total += s.process.burst; }
            expect(total > 0, "large workload burst total");
        });
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
    std::cout << "\n" << passed << " tests passed.\n";
    return 0;
}

} // namespace cpusim
