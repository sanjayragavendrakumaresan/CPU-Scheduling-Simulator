#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cpusim {

struct Process {
    std::string id;
    int arrival = 0;
    int burst = 0;
    int priority = 0;
};

struct Segment {
    std::string label;
    int start = 0;
    int end = 0;
};

struct ProcessStats {
    Process process;
    int completion = 0;
    int turnaround = 0;
    int waiting = 0;
    int response = 0;
    int firstStart = 0;
};

struct Decision {
    int time = 0;
    std::string ready;
    std::string selected;
    std::string remaining;
};

struct Metrics {
    double avgWaiting = 0;
    double avgTurnaround = 0;
    double avgResponse = 0;
    double cpuUtilization = 0;
    double throughput = 0;
    int contextSwitches = 0;
};

struct SchedulingResult {
    std::string algorithm;
    std::vector<ProcessStats> processes;
    std::vector<Segment> timeline;
    std::vector<Decision> decisions;
    Metrics metrics;
};

enum class Algorithm {
    FCFS,
    SJF,
    SRTF,
    PriorityNonPreemptive,
    PriorityPreemptive,
    RoundRobin
};

inline const std::vector<Algorithm>& allAlgorithms() {
    static const std::vector<Algorithm> algorithms = {
        Algorithm::FCFS, Algorithm::SJF, Algorithm::SRTF,
        Algorithm::PriorityNonPreemptive, Algorithm::PriorityPreemptive,
        Algorithm::RoundRobin
    };
    return algorithms;
}

inline std::string algorithmName(Algorithm algorithm) {
    switch (algorithm) {
        case Algorithm::FCFS: return "FCFS";
        case Algorithm::SJF: return "SJF (Non-preemptive)";
        case Algorithm::SRTF: return "SRTF";
        case Algorithm::PriorityNonPreemptive: return "Priority (Non-preemptive)";
        case Algorithm::PriorityPreemptive: return "Priority (Preemptive)";
        case Algorithm::RoundRobin: return "Round Robin";
    }
    return "Unknown";
}

inline bool isPreemptive(Algorithm algorithm) {
    return algorithm == Algorithm::SRTF || algorithm == Algorithm::PriorityPreemptive || algorithm == Algorithm::RoundRobin;
}

inline void validateProcesses(const std::vector<Process>& processes) {
    if (processes.empty()) throw std::invalid_argument("At least one process is required.");
    long long totalBurst = 0;
    int latestArrival = 0;
    for (std::size_t i = 0; i < processes.size(); ++i) {
        const auto& p = processes[i];
        if (p.id.empty()) throw std::invalid_argument("Process ID must not be empty.");
        if (p.arrival < 0 || p.burst <= 0) throw std::invalid_argument("Arrival must be non-negative and burst must be positive.");
        totalBurst += p.burst;
        latestArrival = std::max(latestArrival, p.arrival);
        for (std::size_t j = 0; j < i; ++j)
            if (processes[j].id == p.id) throw std::invalid_argument("Process IDs must be unique: " + p.id);
    }
    if (static_cast<long long>(latestArrival) + totalBurst > std::numeric_limits<int>::max())
        throw std::invalid_argument("Workload exceeds supported simulation time range.");
}

class Scheduler {
public:
    virtual ~Scheduler() = default;
    virtual Algorithm algorithm() const = 0;
    SchedulingResult run(const std::vector<Process>& input, int quantum = 2,
                         int contextSwitchOverhead = 0, int agingInterval = 0,
                         bool recordDetails = true) const {
        return simulate(input, algorithm(), quantum, contextSwitchOverhead, agingInterval, recordDetails);
    }

protected:
    static SchedulingResult simulate(const std::vector<Process>& input, Algorithm algorithm,
                                     int quantum, int overhead, int agingInterval, bool recordDetails) {
        validateProcesses(input);
        if (algorithm == Algorithm::RoundRobin && quantum <= 0)
            throw std::invalid_argument("Round Robin quantum must be positive.");
        if (overhead < 0 || agingInterval < 0)
            throw std::invalid_argument("Overhead and aging interval must be non-negative.");

        const int n = static_cast<int>(input.size());
        long long totalBurstBound = 0;
        int latestArrivalBound = 0;
        for (const auto& p : input) { totalBurstBound += p.burst; latestArrivalBound = std::max(latestArrivalBound, p.arrival); }
        const bool nonPreemptive = algorithm == Algorithm::FCFS || algorithm == Algorithm::SJF ||
                                   algorithm == Algorithm::PriorityNonPreemptive;
        const long long switchBound = nonPreemptive ? n : totalBurstBound;
        const long long availableClock = std::numeric_limits<int>::max() -
                                         static_cast<long long>(latestArrivalBound) - totalBurstBound;
        if (availableClock < 0 || (overhead > 0 && switchBound > availableClock / overhead))
            throw std::invalid_argument("Workload plus context-switch overhead exceeds supported simulation time range.");
        std::vector<int> remaining(n);
        std::vector<int> firstStart(n, -1), completion(n, -1);
        for (int i = 0; i < n; ++i) remaining[i] = input[i].burst;

        SchedulingResult result;
        result.algorithm = algorithmName(algorithm);
        int now = 0;
        int completedCount = 0;
        int lastArrivalIndex = 0;
        std::vector<int> ordered(n);
        for (int i = 0; i < n; ++i) ordered[i] = i;
        std::stable_sort(ordered.begin(), ordered.end(), [&](int a, int b) {
            if (input[a].arrival != input[b].arrival) return input[a].arrival < input[b].arrival;
            return a < b;
        });
        std::vector<int> ready;
        std::queue<int> rrQueue;
        int lastRun = -1;
        int contextSwitches = 0;
        int busy = 0;

        auto addSegment = [&](const std::string& label, int start, int end) {
            if (!recordDetails) return;
            if (end <= start) return;
            if (!result.timeline.empty() && result.timeline.back().label == label && result.timeline.back().end == start) {
                result.timeline.back().end = end;
            } else {
                result.timeline.push_back({label, start, end});
            }
        };
        auto syncArrivals = [&]() {
            while (lastArrivalIndex < n && input[ordered[lastArrivalIndex]].arrival <= now) {
                const int idx = ordered[lastArrivalIndex++];
                if (algorithm == Algorithm::RoundRobin) {
                    rrQueue.push(idx);
                } else {
                    ready.push_back(idx);
                }
            }
        };
        auto nextArrival = [&]() -> int {
            if (lastArrivalIndex >= n) return std::numeric_limits<int>::max();
            return input[ordered[lastArrivalIndex]].arrival;
        };
        auto removeReady = [&](int selected) {
            ready.erase(std::remove(ready.begin(), ready.end(), selected), ready.end());
        };
        auto before = [&](int a, int b) {
            if (input[a].arrival != input[b].arrival) return input[a].arrival < input[b].arrival;
            return a < b;
        };
        auto readyDescription = [&]() {
            std::ostringstream out;
            if (algorithm == Algorithm::RoundRobin) {
                auto copy = rrQueue;
                bool first = true;
                while (!copy.empty()) {
                    if (!first) out << ", ";
                    out << input[copy.front()].id;
                    first = false;
                    copy.pop();
                }
            } else {
                for (std::size_t k = 0; k < ready.size(); ++k) {
                    if (k) out << ", ";
                    out << input[ready[k]].id;
                }
            }
            return out.str().empty() ? std::string("(empty)") : out.str();
        };
        auto remainingDescription = [&]() {
            std::ostringstream out;
            for (int i = 0; i < n; ++i) {
                if (i) out << ", ";
                out << input[i].id << '=' << remaining[i];
            }
            return out.str();
        };
        auto chooseFromReady = [&]() {
            int best = -1;
            for (int idx : ready) {
                if (best < 0) { best = idx; continue; }
                bool take = false;
                if (algorithm == Algorithm::SJF) {
                    take = input[idx].burst < input[best].burst ||
                           (input[idx].burst == input[best].burst && before(idx, best));
                } else if (algorithm == Algorithm::SRTF) {
                    take = remaining[idx] < remaining[best] ||
                           (remaining[idx] == remaining[best] && before(idx, best));
                } else if (algorithm == Algorithm::PriorityNonPreemptive || algorithm == Algorithm::PriorityPreemptive) {
                    const long long priIdx = effectivePriority(input[idx], remaining[idx], now, agingInterval, algorithm);
                    const long long priBest = effectivePriority(input[best], remaining[best], now, agingInterval, algorithm);
                    take = priIdx < priBest || (priIdx == priBest && before(idx, best));
                } else {
                    take = before(idx, best);
                }
                if (take) best = idx;
            }
            return best;
        };
        auto nextAgingBoundary = [&](int selected) {
            if (algorithm != Algorithm::PriorityPreemptive || agingInterval <= 0) return std::numeric_limits<int>::max();
            int boundary = std::numeric_limits<int>::max();
            for (int idx : ready) {
                if (idx == selected) continue;
                const int executed = input[idx].burst - remaining[idx];
                const int waited = std::max(0, now - input[idx].arrival - executed);
                const int delta = agingInterval - (waited % agingInterval);
                const long long candidate = static_cast<long long>(now) + delta;
                boundary = std::min(boundary, static_cast<int>(std::min<long long>(candidate, std::numeric_limits<int>::max())));
            }
            return boundary;
        };

        while (completedCount < n) {
            syncArrivals();
            int selected = -1;
            if (algorithm == Algorithm::RoundRobin) {
                if (!rrQueue.empty()) {
                    selected = rrQueue.front();
                    rrQueue.pop();
                }
            } else if (!ready.empty()) {
                selected = chooseFromReady();
            }

            if (selected < 0) {
                const int arrival = nextArrival();
                if (arrival == std::numeric_limits<int>::max()) throw std::logic_error("No runnable process remains.");
                if (arrival > now) {
                    addSegment("IDLE", now, arrival);
                    now = arrival;
                }
                continue;
            }

            if (recordDetails) result.decisions.push_back({now, readyDescription(), input[selected].id, remainingDescription()});
            if (lastRun >= 0 && lastRun != selected) {
                ++contextSwitches;
                if (overhead > 0) {
                    addSegment("CS", now, now + overhead);
                    now += overhead;
                    // Arrivals during switch overhead become ready, but the already-selected process is dispatched.
                    syncArrivals();
                }
            }
            if (firstStart[selected] < 0) firstStart[selected] = now;

            int duration = remaining[selected];
            if (algorithm == Algorithm::RoundRobin) {
                duration = std::min(duration, quantum);
            } else if (algorithm == Algorithm::SRTF || algorithm == Algorithm::PriorityPreemptive) {
                duration = std::min(duration, std::max(1, nextArrival() - now));
                if (algorithm == Algorithm::PriorityPreemptive && agingInterval > 0)
                    duration = std::min(duration, std::max(1, nextAgingBoundary(selected) - now));
            }
            addSegment(input[selected].id, now, now + duration);
            now += duration;
            remaining[selected] -= duration;
            busy += duration;

            if (algorithm == Algorithm::RoundRobin) {
                syncArrivals();
                if (remaining[selected] == 0) {
                    completion[selected] = now;
                    ++completedCount;
                } else {
                    rrQueue.push(selected);
                }
            } else {
                syncArrivals();
                if (remaining[selected] == 0) {
                    completion[selected] = now;
                    ++completedCount;
                    removeReady(selected);
                }
            }
            lastRun = selected;
        }

        result.processes.reserve(input.size());
        double sumWaiting = 0, sumTurnaround = 0, sumResponse = 0;
        for (int i = 0; i < n; ++i) {
            ProcessStats s;
            s.process = input[i];
            s.completion = completion[i];
            s.turnaround = s.completion - input[i].arrival;
            s.waiting = s.turnaround - input[i].burst;
            s.response = firstStart[i] - input[i].arrival;
            s.firstStart = firstStart[i];
            if (s.waiting < 0 || s.response < 0) throw std::logic_error("Scheduler produced invalid metrics.");
            sumWaiting += s.waiting;
            sumTurnaround += s.turnaround;
            sumResponse += s.response;
            result.processes.push_back(s);
        }
        const int span = now; // Inputs have non-negative arrivals; the clock starts at zero.
        result.metrics.avgWaiting = sumWaiting / n;
        result.metrics.avgTurnaround = sumTurnaround / n;
        result.metrics.avgResponse = sumResponse / n;
        result.metrics.cpuUtilization = span == 0 ? 0 : 100.0 * busy / span;
        result.metrics.throughput = span == 0 ? 0 : static_cast<double>(n) / span;
        result.metrics.contextSwitches = contextSwitches;
        return result;
    }

private:
    static long long effectivePriority(const Process& p, int remaining, int now,
                                      int agingInterval, Algorithm algorithm) {
        if ((algorithm != Algorithm::PriorityNonPreemptive && algorithm != Algorithm::PriorityPreemptive) || agingInterval <= 0)
            return p.priority;
        const int executed = p.burst - remaining;
        const int waiting = std::max(0, now - p.arrival - executed);
        return static_cast<long long>(p.priority) - waiting / agingInterval;
    }
};

class FCFSScheduler final : public Scheduler { public: Algorithm algorithm() const override { return Algorithm::FCFS; } };
class SJFScheduler final : public Scheduler { public: Algorithm algorithm() const override { return Algorithm::SJF; } };
class SRTFScheduler final : public Scheduler { public: Algorithm algorithm() const override { return Algorithm::SRTF; } };
class PriorityNonPreemptiveScheduler final : public Scheduler { public: Algorithm algorithm() const override { return Algorithm::PriorityNonPreemptive; } };
class PriorityPreemptiveScheduler final : public Scheduler { public: Algorithm algorithm() const override { return Algorithm::PriorityPreemptive; } };
class RoundRobinScheduler final : public Scheduler { public: Algorithm algorithm() const override { return Algorithm::RoundRobin; } };

inline SchedulingResult schedule(const std::vector<Process>& processes, Algorithm algorithm,
                                 int quantum = 2, int overhead = 0, int agingInterval = 0,
                                 bool recordDetails = true) {
    switch (algorithm) {
        case Algorithm::FCFS: return FCFSScheduler{}.run(processes, quantum, overhead, agingInterval, recordDetails);
        case Algorithm::SJF: return SJFScheduler{}.run(processes, quantum, overhead, agingInterval, recordDetails);
        case Algorithm::SRTF: return SRTFScheduler{}.run(processes, quantum, overhead, agingInterval, recordDetails);
        case Algorithm::PriorityNonPreemptive: return PriorityNonPreemptiveScheduler{}.run(processes, quantum, overhead, agingInterval, recordDetails);
        case Algorithm::PriorityPreemptive: return PriorityPreemptiveScheduler{}.run(processes, quantum, overhead, agingInterval, recordDetails);
        case Algorithm::RoundRobin: return RoundRobinScheduler{}.run(processes, quantum, overhead, agingInterval, recordDetails);
    }
    throw std::invalid_argument("Unknown algorithm.");
}

} // namespace cpusim
