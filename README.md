# Low-Latency C++ Order Book and Matching Engine

A C++20 learning project implementing a single-threaded limit-order book and using controlled benchmarks to study the costs that matter in low-latency trading systems: matching, price-level traversal, allocation, cache locality, and tail latency.

The project deliberately starts with a readable, testable baseline. The near-term objective is a small working project that can be demonstrated and discussed in quant / low-latency software interviews; data-structure experiments, profiling, and concurrency improvements will follow incrementally.

## Current status

**Current milestone: functional single-threaded matching engine with deterministic tests, throughput microbenchmarks, and a custom per-operation latency harness.** This status describes the current source tree and user-reported local benchmark output. It is not a claim that this connector independently built or ran the project.

| Area | Current status |
|---|---|
| Language / standard | C++20; extensions disabled |
| Build | CMake 3.25+ with Windows x64 presets |
| Order model | `include/matching_engine/order.hpp` |
| Order-book API | `include/matching_engine/order_book.hpp` |
| Implementation | `src/order_book.cpp` |
| Matching design | Dense integer price ladder; FIFO queue per price level; optional best bid/ask |
| Trade output | `add_order(Order)` returns a vector of trade records |
| Unit tests | GoogleTest tests for resting orders, crossing trades, resting-price execution, FIFO, partial fills, multi-level matches, best-price updates/reinitialization, price bounds, and zero quantity |
| Throughput benchmark | Google Benchmark workloads for construction/lifecycle, non-crossing insertions, full matches in both directions, partial fills in both directions, multi-level sweeps, sparse traversal, and a controlled mixed sequence |
| Latency harness | Custom standalone executable recording individual `add_order()` call durations and reporting median, p95, p99, p99.9, and maximum |
| Concurrency / lock-free | Not implemented; intentionally deferred |
| Cancellation / order-ID index | Not implemented |
| Linux performance profiling / CPU pinning | Not yet established |

### Current order-book semantics

- Prices are integer ticks, not floating-point values.
- A BUY matches when its limit price crosses the best resting ask; a SELL matches when its limit crosses the best resting bid.
- The initial execution-price convention is the resting order's price.
- At a given price, orders are processed FIFO.
- Partial fills decrement remaining quantity; fully consumed resting orders leave their queue.
- A trade records buy order ID, sell order ID, execution price, and quantity.
- Best bid and ask are exposed as optionals, empty when that side has no resting orders.

The current implementation uses `std::vector<std::queue<Order>>` for both sides. This is a useful bounded-price baseline, not a claim that the representation is optimal.

## Repository layout

```text
.
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── include/matching_engine/
│   ├── order.hpp
│   └── order_book.hpp
├── src/
│   └── order_book.cpp
├── tests/
│   ├── order_tests.cpp
│   └── order_book_tests.cpp
└── benchmarks/
    ├── order_benchmark.cpp
    ├── order_book_benchmark.cpp
    └── order_book_latency.cpp
```

## Build and test

The project uses CMake FetchContent for GoogleTest 1.17.0 and Google Benchmark 1.9.4.

Run from a Visual Studio developer environment with the configured generator installed:

```powershell
cmake --preset windows-debug
cmake --build --preset debug
ctest --preset debug-tests
```

Build Release for performance work:

```powershell
cmake --preset windows-release
cmake --build --preset release
```

The presets currently name the generator `Visual Studio 18 2026`, target x64, and enable tests and benchmarks. Adjust the preset only if that generator is unavailable in the local toolchain.

Run the throughput benchmark executable in Release mode:

```powershell
.\build\release\Release\order_book_benchmark.exe --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_out=throughput.csv --benchmark_out_format=csv
```

Run the standalone latency executable:

```powershell
.\build\release\Release\order_book_latency.exe
```

The latency executable has a custom `main()`; Google Benchmark's `--benchmark_*` flags and CSV reporter do **not** apply to it. At present, its summary is printed to standard output. Redirect output to a text file when desired:

```powershell
.\build\release\Release\order_book_latency.exe > latency.txt
```

CSV output and plots are the next benchmark-harness improvements; do not pass Google Benchmark flags to the standalone latency program expecting them to create a file.

## Benchmarking approach

### Throughput microbenchmarks

The Google Benchmark executable times batches of calls with setup outside the measured interval in most operation benchmarks. Current cases include:

- Non-crossing BUY and SELL insertions.
- Full matching against asks and bids.
- Partial fills against a resting order.
- Book construction/destruction at different price bounds.
- A controlled multi-level matching workload.
- Sparse price-level traversal with varying tick gaps.
- A synthetic mixed sequence containing two inserts and two matches per cycle.

These are deliberately controlled microbenchmarks. They isolate behavior but do not represent a calibrated replay of real market data. The mixed sequence is synthetic and should not be described as a realistic market workload.

### Per-operation latency harness

The standalone latency harness records one `steady_clock` duration per `add_order()` call, then computes summary statistics after sorting the samples. Current workflows include:

- Resting insertion at one price level.
- Full matching with one resting order consumed per operation.
- Partial fills of a larger resting order.
- Best-ask exhaustion.
- Multi-level sweeps over 16, 64, and 256 levels.

The timer surrounds the public `add_order()` call. Incoming order construction is outside the timed region; creation of the returned trade vector is part of the API call, while destruction of the returned vector occurs after the stop timestamp. The returned trade count is accumulated into a checksum outside the timed interval. The harness is a first measurement instrument, not a cycle-accurate profiler.

Interpret each workload according to its setup:
- The insertion workload grows one queue throughout the run and may expose occasional storage growth/allocation costs.
- The full-match workload keeps a next ask available to avoid exhausting the sell side on every sample.
- The partial-fill workload keeps a large resting ask present.
- The exhaustion workload repeatedly prepares an ask outside timing and measures consuming it. It is intended to expose best-ask update/traversal behavior, but setup is not timed.
- Sweep setup is excluded from timing. Each incoming order crosses a controlled number of consecutive occupied price levels.

### Initial latency observations (user-reported local run)

These numbers were reported from one local Release-mode run and are illustrative, not portable performance guarantees:

| Workflow | Median | p99.9 | Maximum |
|---|---:|---:|---:|
| Resting insertion | 100 ns | 200 ns | 317,000 ns |
| Full match | 100 ns | 800 ns | 6,000 ns |
| Partial fill | 100 ns | 200 ns | 6,500 ns |
| Best-ask exhaustion | 300 ns | 1,000 ns | 223,800 ns |
| 16-level sweep | 1,000 ns | 5,600 ns | 133,000 ns |
| 64-level sweep | 2,400 ns | 6,400 ns | 77,600 ns |
| 256-level sweep | 5,400 ns | 12,800 ns | 12,800 ns |

Caveats:
- The 256-level case has only 500 samples, so its nearest-rank p99.9 coincides with its maximum. Collect more samples before using it to assess tail latency.
- A maximum far above p99.9 indicates a rare extreme sample, but does not identify the cause. OS scheduling, interrupts, page faults, allocator effects, timer behavior, and real code-path variation are all possibilities; profiling and repeated runs are needed.
- A median displayed as 100 ns is constrained by clock measurement granularity and overhead. It should not be interpreted as a cycle-accurate measurement.
- These results do not establish that one implementation is faster than another. A future optimization must be compared using the same compiler/build, machine, workload, and measurement method.

## Known limitations and next investigations

### Near-term: make measurements reproducible

1. Add persistent CSV output to the custom latency harness, ideally one summary row per workflow plus optional raw sample output.
2. Record run metadata: build configuration, compiler, OS, CPU, sample counts, and workload parameters.
3. Re-run workloads and compare repeatability; add timer-overhead/granularity checks.
4. Add more samples to large-sweep tail measurements and keep sample count explicit in every report.
5. Generate graphs from raw data or summary CSV (latency percentile comparison and workload scaling are initial useful plots).

### Next: profile the existing data path

1. Inspect queue allocation behavior, especially growth in the resting-insertion workload.
2. Measure how best-price updates scale with the number of empty ticks traversed.
3. Profile trade-vector creation on matching calls.
4. Compare dense and sparse occupancy workloads and identify whether traversal, allocation, or execution count dominates.
5. Make one optimization at a time and rerun all correctness tests and benchmark workloads.

### Later: data structures, memory, and concurrency

Potential experiments, only after measurements motivate them:
- A preallocated per-price FIFO or pooled order storage.
- An occupancy bitmap/hierarchical bitset to skip empty price ticks.
- A tree-based alternative for sparse price domains.
- An order-ID index and data structure supporting cancellation.
- Linux profiling, hardware counters where available, and controlled CPU-affinity experiments.
- A single-writer/event-loop architecture and bounded message queues before considering lock-free shared mutable state.

A lock-free matching engine is not a current feature. The project presently establishes a single-threaded base; any future concurrency work must define ownership and preserve deterministic matching semantics.

## Showcase scope

The first presentable milestone is a **single-threaded price-time-priority matching engine with deterministic tests and reproducible performance experiments**. For a resume/interview discussion, emphasize the choices, test coverage, benchmark methodology, measured trade-offs, and limitations. Do not claim exchange-grade behavior, lock-free concurrency, production readiness, or universal nanosecond performance.

## Working principles

- Keep matching logic readable and correct before low-level optimization.
- Preserve a known baseline and change one thing at a time.
- Treat tests as correctness evidence and benchmarks as workload-specific evidence.
- Clearly distinguish measured facts, hypotheses, and future plans.
- Defer CPU pinning, cache-line padding, and lock-free algorithms until a measured bottleneck or architectural requirement justifies them.
