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
| Trade output | Current API returns `std::vector<Trade>`; this is a measured design choice, not the intended final low-allocation interface |
| Unit tests | GoogleTest tests for resting orders, crossing trades, resting-price execution, FIFO, partial fills, multi-level matches, best-price updates/reinitialization, price bounds, and zero quantity |
| Throughput benchmark | Google Benchmark workloads for construction/lifecycle, non-crossing insertions, full matches in both directions, partial fills in both directions, multi-level sweeps, sparse traversal, and a controlled mixed sequence; plotting utility added |
| Latency harness | Custom standalone executable; each run writes timestamped Markdown/CSV output, raw samples, environment metadata, and timer-pair baseline statistics |
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

The current implementation uses `std::vector<std::queue<Order>>` for both sides. This is a useful bounded-price baseline, not a claim that the representation is optimal. The current `add_order()` return value also constructs a per-call trade vector; the next API-design experiment should compare it with streaming per-execution `Trade` events to a caller-owned sink and returning a small order-level summary.

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
├── benchmarks/
│   ├── order_benchmark.cpp
│   ├── order_book_benchmark.cpp
│   └── order_book_latency.cpp
└── tools/
    ├── plot_latency.py
    └── run_latency_report.ps1
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

Run the throughput benchmark executable in Release mode and save its CSV output:

```powershell
.\build\release\Release\order_book_benchmark.exe --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_out=throughput.csv --benchmark_out_format=csv
```

Create throughput overview and batch-scaling charts from the resulting Google Benchmark CSV:

```powershell
py -3 tools/plot_throughput.py throughput.csv
```

The script writes a Markdown report and plots into a new timestamped subdirectory under `benchmark_results/`. The overview compares available mean items-per-second metrics, while the scaling graph compares selected workloads as batch size changes; do not use cross-workload throughput as a universal ranking when the work counted by each benchmark differs.

Run the standalone latency executable:

```powershell
.\build\release\Release\order_book_latency.exe
```

The latency executable has a custom `main()`; Google Benchmark's `--benchmark_*` flags and CSV reporter do **not** apply to it. Each execution automatically creates a new timestamped directory under `benchmark_results/` with:

- `summary.md` and `summary.csv`: per-workflow min, median, mean, population standard deviation, p95, p99, p99.9, maximum, sample count, and result checksum.
- `samples.csv`: individual latency samples in measurement order, which allows time-series analysis.
- `metadata.txt`: compiler, platform, build-mode hint, clock details, hardware-concurrency hint, and measurement-method notes.

To run the latency executable and generate charts in one step (from PowerShell):

```powershell
.\tools\run_latency_report.ps1
```

The wrapper expects the Release executable at `build\release\Release\order_book_latency.exe`. Install the plotting dependency once if needed:

```powershell
py -3 -m pip install matplotlib
```

To generate plots for the latest report manually:

```powershell
py -3 tools/plot_latency.py
```

Or pass a specific report directory to analyze an earlier run. The plotting script writes a percentile comparison chart, an empirical CDF, and a latency trace for each workflow into that report directory. Do not pass Google Benchmark flags to the standalone latency program expecting them to create a file.

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

The standalone latency harness records one `steady_clock` duration per `add_order()` call, then computes summary statistics from a sorted copy while preserving the original sample order for trace analysis. Each run also measures a clock-pair baseline; it is reported separately and not subtracted. Current workflows include:

- Resting insertion at one price level.
- Full matching with one resting order consumed per operation.
- Partial fills of a larger resting order.
- Best-ask exhaustion.
- Multi-level sweeps over 16, 64, and 256 adjacent price levels.
- Repeated-gap sparse best-ask traversal with gaps of 1, 16, 64, and 256 ticks.
- A seeded wide-ladder traversal (seed `0x5EED1234`) that visits distinct occupied levels over a much larger price range with gaps selected from 1, 16, 64, and 128 ticks, reporting both overall and per-gap latency.

The timer surrounds the public `add_order()` call. Incoming order construction is outside the timed region; creation of the returned trade vector is part of the API call, while destruction of the returned vector occurs after the stop timestamp. The returned trade count is accumulated into a checksum outside the timed interval. The harness is a first measurement instrument, not a cycle-accurate profiler.

Interpret each workload according to its setup:
- The insertion workload grows one queue throughout the run and may expose occasional storage growth/allocation costs.
- The full-match workload keeps a next ask available to avoid exhausting the sell side on every sample.
- The partial-fill workload keeps a large resting ask present.
- The exhaustion workload repeatedly prepares an ask outside timing and measures consuming it. It is intended to expose best-ask update/traversal behavior, but setup is not timed.
- Sweep setup is excluded from timing. The next resting ask is adjacent to the swept levels, so the sweep does not accidentally include a long empty-price scan.
- Repeated-gap sparse traversal keeps a sentinel ask at a controlled tick gap and revisits the same ladder region. It acts as a warm-working-set control.
- The seeded wide-ladder traversal preloads asks at distinct price levels and consumes them in ascending order with gaps selected from `{1, 16, 64, 128}`. The ladder's working set is much larger than the fixed-gap test and the seed makes the sequence reproducible. Per-gap rows are also reported. This is a wider-working-set test, not a guaranteed cold-cache test: cache state is not explicitly flushed.

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
- The historical 256-level observation shown above came from a run with only 500 samples, so its nearest-rank p99.9 coincided with its maximum. The report-producing harness now collects 2,000 samples for that workflow; rerun it to obtain a newer baseline rather than treating this historical row as current.
- A maximum far above p99.9 indicates a rare extreme sample, but does not identify the cause. OS scheduling, interrupts, page faults, allocator effects, timer behavior, and real code-path variation are all possibilities; profiling and repeated runs are needed.
- A median displayed as 100 ns is constrained by clock measurement granularity and overhead. It should not be interpreted as a cycle-accurate measurement.
- These results do not establish that one implementation is faster than another. A future optimization must be compared using the same compiler/build, machine, workload, and measurement method.

## Known limitations and next investigations

### Near-term: make measurements reproducible

1. Re-run the report-producing harness several times and compare variability across runs.
2. Record the exact CPU model and clock/power state alongside the automatically captured environment metadata; these host-specific details are not currently queried automatically.
3. Interpret the measured clock-pair baseline, but do not mechanically subtract it from every sample. `DoNotOptimize` and `ClobberMemory` constrain compiler optimization; they do not flush CPU caches.
4. Use the raw sample traces and empirical CDF to investigate spikes and compare tail behavior.
5. Use the throughput batch-scaling chart alongside the latency traces; then profile queue/storage allocation and best-price traversal before selecting an optimization.

### Next: improve the public execution-output contract

1. Preserve the current vector-returning API as a baseline until an alternative is measured.
2. Add a small order-level result (filled quantity, leaves quantity, execution count) and an execution-event sink that receives one `Trade` per fill.
3. Compare a reusable caller-owned sink/buffer with the current fresh vector on single-fill and multi-level workloads. A single `Trade` cannot represent multiple executions against different resting orders.
4. Define sink capacity/back-pressure behavior. The engine must never silently drop an execution event; persistence or network I/O should not run synchronously inside the matching hot path.

### Then: profile the existing data path

1. Inspect queue allocation behavior, especially growth in the resting-insertion workload.
2. Measure how best-price updates scale with the number of empty ticks traversed and compare repeated-gap vs seeded wide-ladder workloads.
3. Profile trade-vector creation on matching calls and compare it with the event-sink implementation.
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
