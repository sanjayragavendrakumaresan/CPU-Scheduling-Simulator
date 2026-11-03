# Design and simulation assumptions

## Scheduling engine

`Scheduler` is the polymorphic base class; each of the six policy classes selects its algorithm independently and shares the event-driven simulation/metrics infrastructure. The simulator advances directly to the next meaningful event (completion, arrival, Round Robin quantum expiration, or priority-aging boundary) rather than incrementing one CPU tick at a time. All times are integer-valued. Ready-set choices are deterministic: earlier arrival, then original input order. Round Robin uses a FIFO queue; boundary arrivals are enqueued before an unfinished process is put back at the queue tail.

## Metrics

For each process, the simulator reports completion time, turnaround (`CT-AT`), waiting (`TAT-BT`), and response (`first start-AT`). Average values are arithmetic means. The time origin is zero, so an initial idle gap is included in elapsed time. Utilization is process execution only divided by elapsed time; idle and context-switch intervals count against utilization. Throughput is completed process count divided by elapsed simulation time.

A context switch means a dispatch to a different process after a prior process executed. The initial dispatch is not a switch; a change after idle is a switch. Configured overhead is charged only for such changes; no overhead is charged when the same process continues. Overhead is shown as `CS`, not attributed to a process's burst, and affects completion/wait/response metrics. The process chosen before overhead is dispatched afterward even if arrivals happen during the overhead interval; those arrivals enter the ready set for the next decision.

## Priority aging

Aging is enabled per run for either priority policy. For a process currently ready, `effectivePriority = basePriority - floor(readyWaitingTime / agingInterval)`. A lower number is higher priority. Ready waiting excludes its executed CPU service and accrues while it is waiting. The implementation has no lower bound on effective priority. Non-preemptive priority reevaluates aged priorities at dispatches; preemptive priority reevaluates on arrivals and aging boundaries. The usual arrival/input-order tie rule applies. Aging improves fairness in some workloads but is not a guarantee of globally optimal waiting metrics.

## Benchmarking

Benchmarks use `std::chrono::steady_clock`, fixed-seed generated datasets at the four advertised sizes, and avoid terminal formatting inside the timed region. Dataset generation is outside the timing interval. The reported numbers are machine- and compiler-specific observations; theoretical work is given separately in the menu and README. It is a lightweight educational benchmark, not a statistically rigorous performance claim.
