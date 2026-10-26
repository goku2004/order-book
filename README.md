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
| Unit tests | GoogleTest covers matching semantics and seeded replay ratios, determinism, and workload invariants |
| Throughput benchmark | Google Benchmark microbenchmarks plus a seeded 45/25/20/10 mixed replay; throughput plotting utility added |
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
│   ├── order_book_tests.cpp
│   └── seeded_replay_tests.cpp
├── benchmarks/
│   ├── order_benchmark.cpp
│   ├── order_book_benchmark.cpp
│   ├── order_book_latency.cpp
│   ├── seeded_replay.hpp
│   └── seeded_replay.cpp
├── tools/
│   ├── plot_latency.py
│   ├── plot_throughput.py
│   └── run_latency_report.ps1
└── .github/
    └── workflows/
        └── windows-build.yml
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

Create throughput overview, batch-scaling, and seeded-profile category-rate charts from the resulting Google Benchmark CSV:

```powershell
py -3 tools/plot_throughput.py throughput.csv
```

The script writes a Markdown report and plots into a new timestamped subdirectory under `benchmark_results/`. The overview compares available mean items-per-second metrics, the scaling graph compares selected workloads as batch size changes, and `seeded_profile_category_rates.png` compares the 4,000-operation profiles' category operation rates. Category rates are contributions to overall replay throughput, not isolated per-operation latencies. Do not use cross-workload throughput as a universal ranking when the work counted by each benchmark differs.

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

The Google Benchmark executable measures controlled operation batches and includes a separate seeded replay workload. Existing microbenchmarks cover:

- Non-crossing BUY and SELL insertions.
- Full matching against asks and bids.
- Partial fills against a resting order.
- Book construction/destruction at different price bounds.
- Multi-level matching, sparse traversal, and a synthetic mixed sequence.

These microbenchmarks isolate behavior. They are not a calibrated replay of real market data.

#### Seeded mixed replay

The reusable generator lives in `benchmarks/seeded_replay.hpp/.cpp`; `BM_SeededMixedReplay` is registered in the existing `order_book_benchmark` executable, so no second benchmark entry point is needed. The generator creates its operation sequence before timing and uses a fixed seed.

Three exact workload profiles are available. Since every percentage is a multiple of 5%, operation counts must be divisible by 20.

| Profile | Passive insertion | Single-level match | Multi-level sweep | Sparse-level traversal |
|---|---:|---:|---:|---:|
| Balanced | 45% | 25% | 20% | 10% |
| Insertion-heavy | 60% | 25% | 10% | 5% |
| Matching-heavy | 25% | 40% | 25% | 10% |

For 4,000 operations, these translate to:

| Profile | Passive | Single-level | Sweeps | Sparse |
|---|---:|---:|---:|---:|
| Balanced | 1,800 | 1,000 | 800 | 400 |
| Insertion-heavy | 2,400 | 1,000 | 400 | 200 |
| Matching-heavy | 1,000 | 1,600 | 1,000 | 400 |

The current registered cases compare seven scenarios:

| Operations | Profile | Seed | Purpose |
|---:|---|---|---|
| 4,000 | Balanced | `0x5EED` | Baseline mix |
| 4,000 | Balanced | `0xC0FFEE` | Seed/order sensitivity |
| 4,000 | Balanced | `0x12345678` | Additional seed sensitivity |
| 4,000 | Balanced | `0xDEADBEEF` | Additional seed sensitivity |
| 4,000 | Insertion-heavy | `0x5EED` | Emphasize resting insertion |
| 4,000 | Matching-heavy | `0x5EED` | Emphasize match and sweep paths |
| 8,000 | Balanced | `0x5EED` | Larger replay and book-depth scaling |

Comparison design:
- **Seed sensitivity:** compare the four 4,000-operation balanced cases (seeds `0x5EED`, `0xC0FFEE`, `0x12345678`, and `0xDEADBEEF`).
- **Profile sensitivity:** compare the 4,000-operation balanced, insertion-heavy, and matching-heavy cases, all using seed `0x5EED`.
- **Size/depth sensitivity:** compare the 4,000- and 8,000-operation balanced cases using seed `0x5EED`. This is not a pure stream-length test: the current fixture also doubles its seeded primary-book depth as operation count doubles.

The profile changes both category counts and the generated sequence; the same seed does not make different profiles contain the same operations in the same order. A seed reproduces a workload for a particular size, profile, standard library, and toolchain. C++ does not guarantee identical `std::shuffle` or distribution mappings across all standard libraries.

For each Google Benchmark iteration, dense and sparse books are rebuilt and seeded while timing is paused. The timed region executes the operation stream against the existing `add_order(Order)` API. This makes the benchmark a repeatable synthetic replay, not a measurement of startup or book-seeding time. It currently measures the vector-returning API and its result-vector lifecycle; it does not yet represent the future execution-sink API.

The benchmark reports incoming API calls/s, trade events/s, executed quantity/s, per-category operation rates, the numeric profile ID and seed, trade-count mismatches, passive insertion crossings, and trade events per API call. The benchmark's single-level match category can generate a trade and leave residual quantity resting; sweep operations generate 4 or 8 executions against unit-sized resting liquidity. The seeded active liquidity has unit quantity, so the replay's executed-quantity count currently equals its trade-event count. The expected diagnostic values are **zero** for `trade_count_mismatches_per_replay` and `passive_insert_crossings_per_replay`; do not treat a run with nonzero diagnostics as a valid performance baseline.

#### Latest seeded-replay throughput results

The following mean aggregates are from the latest user-supplied Release-mode report (five Google Benchmark repetitions). Rates are rounded for readability.

| Replay | API calls/s | Trade events/s | Trades/API call |
|---|---:|---:|---:|
| Balanced, 4,000 ops, seed `0x5EED` | 3.220M | 5.003M | 1.554 |
| Balanced, 4,000 ops, seed `0xC0FFEE` | 3.257M | 4.937M | 1.516 |
| Balanced, 4,000 ops, seed `0x12345678` | 3.109M | 4.809M | 1.547 |
| Balanced, 4,000 ops, seed `0xDEADBEEF` | 3.088M | 4.730M | 1.532 |
| Insertion-heavy, 4,000 ops, seed `0x5EED` | 4.729M | 4.294M | 0.908 |
| Matching-heavy, 4,000 ops, seed `0x5EED` | 2.433M | 4.891M | 2.010 |
| Balanced, 8,000 ops, seed `0x5EED` | 2.129M | 3.309M | 1.555 |

These are workload-specific results from one machine/run configuration, not general performance guarantees. The uploaded summary contains mean aggregates, not enough per-repetition detail to determine statistical significance of the seed-to-seed differences.

**Interpretation, with limitations:**

- The insertion-heavy profile has the highest API-call rate: about 47% higher than the 4,000-op balanced `0x5EED` case. This is evidence that passive insertion is not the obvious dominant cost in this particular mix. It is not proof that insertion is intrinsically cheap in all book states: the profile also has fewer sweeps, fewer sparse operations, and fewer trade events per call.
- The matching-heavy profile processes about 24% fewer API calls/s than balanced, while its trade-event rate is only about 2% lower. It generates 2.010 trade events per API call versus 1.554 for balanced. This suggests that matching/output work consumes more time per incoming API call and that API calls/s alone undervalues operations that produce multiple executions.
- The balanced cases with four seeds range from 3.088M to 3.257M API calls/s, a roughly 5.5% max-to-min range. Without the individual repetition values or confidence intervals, that spread cannot be attributed solely to workload ordering; system noise may contribute.
- The 8,000-op balanced result is about 34% lower in API-call throughput than the 4,000-op balanced `0x5EED` result. The fixture also doubles the number of preloaded active price levels with operation count, so this is a combined replay-size/book-depth effect, not a clean demonstration that a longer stream alone is slower.
- Trade events/s stays in a narrower band (about 4.29M–5.00M/s) across the 4,000-op profiles than API calls/s (about 2.43M–4.73M/s). This makes trade output a useful second throughput metric, but it does not isolate the per-trade cost by itself.

**Important plot interpretation:** `seeded_profile_category_rates.png` derives each category's operations/s by dividing that category's operation count by the total measured replay time. Its bars therefore equal the category share multiplied by whole-replay API throughput. They show each category's contribution to the rate, **not** a separately timed per-category speed. Use isolated microbenchmarks to attribute costs.

The top-level evidence points toward prioritizing the combined matching/sweep/trade-output path before optimizing passive insertion. It does not yet identify whether the key cost is the match loop, queue removal, empty-level traversal, or the temporary `std::vector<Trade>`. The next experiments should isolate those components.

Run only the seeded replay and save its results:

```powershell
.\build\release\Release\order_book_benchmark.exe --benchmark_filter=BM_SeededMixedReplay --benchmark_repetitions=5 --benchmark_report_aggregates_only=true --benchmark_out=seeded_replay.csv --benchmark_out_format=csv
```

This workload is closer to an end-to-end stateful test than isolated microbenchmarks, but it is still synthetic. The passive price bands, unit-sized active liquidity, separate sparse book, and fixed operation categories are test-fixture assumptions—not claims about actual exchange order-flow distributions.

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

### Next: choose the optimization using isolated measurements

1. **Matching versus insertion:** use the existing single-level full-match, partial-fill, non-crossing insertion, and multi-level sweep microbenchmarks as the first attribution controls. Keep each workload's book state and amount of work explicit.
2. **Trade-output cost:** compare the current vector-returning `add_order()` with a future caller-owned preallocated sink/counting sink. Exercise one execution and multi-execution sweeps separately; preserve the vector implementation as the baseline.
3. **Match-loop scaling:** measure sweep sizes (for example 1, 2, 4, 8, 16, 64 executions) and report both API calls/s and trade events/s. This can reveal whether cost scales mainly with executions or shows additional nonlinear allocation/traversal effects.
4. **Best-price traversal:** compare repeated-gap and seeded wide-ladder sparse workloads. Do not infer that cache misses occur on every access; the wide-ladder test enlarges the working set but does not flush caches.
5. **Memory allocation:** use a profiler or allocation instrumentation to determine whether queue/deque storage or the returned trade vector is responsible for meaningful time/allocation counts.
6. Make one change at a time; rerun correctness tests and the same benchmark seeds, and report per-repetition variability as well as mean results.

The current mixed-replay plots suggest the match/output path is the more productive first target than passive insertion, but category-rate bars are not per-operation timings. Keep that conclusion provisional until the isolated measurements agree.

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
