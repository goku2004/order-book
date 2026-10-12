# Low-Latency C++ Order Book and Matching Engine

A C++20 learning project for building a limit-order matching engine and progressively investigating the systems techniques used in low-latency trading infrastructure.

The goal is to establish a correct, measurable single-threaded baseline first, then explore data structures, memory layout, allocation behavior, concurrency, and operating-system tuning. Optimizations should be motivated by measurements rather than introduced speculatively.

## Project goals

- Implement price-time-priority matching for limit orders.
- Understand the trade-offs between dense price ladders and sparse price-level structures.
- Measure throughput and latency with reproducible benchmarks.
- Study cache locality, memory allocation, custom pools, cache coherence, false sharing, and the C++ memory model.
- Explore concurrency and lock-free techniques only after the single-threaded engine has a defined contract and a correctness test suite.
- Develop a path from Windows-based development to Linux profiling and deployment.

## Current status

**Stage: early single-threaded implementation.** The data model and CMake scaffolding exist; the matching engine is not yet integrated into the build or covered by behavior tests.

| Area | Current state |
|---|---|
| Language | C++20, extensions disabled |
| Build system | CMake 3.25+ |
| Windows presets | Visual Studio 18 2026, x64 |
| Order model | `include/matching_engine/order.hpp` |
| Order-book implementation | `src/order_book.cpp` |
| Unit tests | GoogleTest; currently tests `Order` construction only |
| Benchmark | Google Benchmark; currently measures `Order` construction only |
| Matching-engine build target | Currently compiles `src/order.cpp` only |
| Order-book API | No public `order_book.hpp` exists yet |
| End-to-end matching tests | Not yet implemented |
| Performance baseline for matching | Not yet measured |
| Concurrency | Not implemented; intentionally deferred |

The repository contains an initial implementation of `OrderBook` in `src/order_book.cpp`. It uses a dense price ladder with FIFO queues and optional best-price trackers. The current implementation must not yet be treated as a validated or benchmarked matching engine.

## Repository layout

```text
.
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── include/
│   └── matching_engine/
│       └── order.hpp
├── src/
│   └── order_book.cpp
├── tests/
│   └── order_tests.cpp
└── benchmarks/
    └── order_benchmark.cpp
```

The intended next structural change is to introduce a public declaration at `include/matching_engine/order_book.hpp`, keep the implementation in `src/order_book.cpp`, and register the implementation and order-book tests with CMake.

## Domain model

The current order model is:

```cpp
namespace matching_engine {

enum class Side : uint8_t {
    Buy,
    Sell
};

struct Order {
    uint64_t order_id;
    int64_t price;
    uint32_t quantity;
    Side side;
};

}
```

Prices are represented as integer ticks instead of floating-point values. The current model has an order ID, price, remaining quantity, and side; it does not yet define trade events, cancellation, duplicate-ID behavior, or an accounting interface.

## Current order-book design

The current implementation in `src/order_book.cpp` contains:

- `std::vector<std::queue<Order>> buy_levels`
- `std::vector<std::queue<Order>> sell_levels`
- `int64_t max_price`
- `std::optional<int64_t> best_bid`
- `std::optional<int64_t> best_ask`

The vectors use the price tick as the index. Each price level uses a FIFO queue, so insertion order within a price level represents time priority.

Conceptually:

```text
Price tick -> level queue

BUY:  higher prices have priority
      ... [price 103] [price 102] [price 101] ...

SELL: lower prices have priority
      ... [price 101] [price 102] [price 103] ...
```

For an incoming BUY, matching can continue while the best ask is less than or equal to the incoming limit price. For an incoming SELL, matching can continue while the best bid is greater than or equal to the incoming limit price. At a crossed level, the intended fill quantity is the minimum of the incoming and resting quantities. Fully consumed resting orders are removed from the front of their queue.

### Data-structure trade-offs

**Why start with a dense ladder?**

- Direct price-to-index lookup is simple and predictable.
- A vector keeps price-level queue objects contiguous.
- Price levels can be updated without tree lookup.
- The bounded price domain makes this a useful baseline for experiments.

**What to measure or revisit later:**

- A `std::queue<Order>` normally uses `std::deque<Order>` as its underlying container; queue insertion and allocator behavior should be measured, not assumed allocation-free.
- A queue object exists for every possible price tick, including empty levels.
- Walking through empty prices may be expensive when the price range is wide or liquidity is sparse.
- A dense ladder can consume substantial memory for a large, sparsely occupied price domain.
- A FIFO queue does not provide efficient arbitrary cancellation by order ID. Supporting cancellation will likely require an order-ID index and a different per-level representation.

Possible later comparisons include a tree-based price-level map for sparse ranges, an occupancy bitmap for skipping empty levels, a compact level structure, and pooled order storage. Keep the initial implementation straightforward until benchmarks justify a change.

## Correctness work required

The following items are based on inspection of the current `src/order_book.cpp`; they have not been validated by running the program.

1. **Empty best-price optionals during insertion.** `add_order()` currently dereferences `best_bid` or `best_ask` inside `std::max` / `std::min`. Those optionals can be empty when the first order of that side is inserted. Handle initialization explicitly before comparing prices.
2. **Best-ask exhaustion.** After consuming a sell level, the upward scan stops at `max_price` even if that last level is empty, then assigns that price to `best_ask`. If no resting ask remains, `best_ask` must become `std::nullopt`; otherwise the tracker claims an empty level is the best ask.
3. **Price bounds.** `order.price` is used as a vector index. Validate the supported range before indexing and define the behavior for negative or out-of-range prices.
4. **Constructor bounds and allocation size.** Validate `max_price` and ensure that `max_price + 1` is representable and a valid vector size before allocating both ladders.
5. **API and ownership contract.** The current `add_order(Order&)` and `match(Order&)` functions modify the caller's incoming order quantity. Decide and document whether that is intentional; consider whether the public API should instead take an order by value or use a result type.
6. **Input invariants.** Define the behavior for zero-quantity orders, invalid sides if they can enter through non-enum data, duplicate order IDs, and invalid prices.
7. **Build integration.** The order book currently has only a class definition inside its implementation file, so other translation units cannot use the type through a public interface. Add a header and include `src/order_book.cpp` in the `matching_engine` library target.
8. **Observable fills.** Matching currently decrements quantities but does not emit trades. Add a trade/result interface before considering the matching logic complete.
9. **Accounting separation.** Keep cash, P&L, and account balances out of the basic order-book matching algorithm. Report executions as trade events and let a separate layer handle accounting.
10. **No proven test or performance status.** Do not claim matching correctness, passing order-book tests, or matching throughput until these are actually tested and recorded.

## Matching semantics to implement

Use these as the initial behavioral contract unless the implementation discussion explicitly revises them:

- **Price-time priority:** best price first; FIFO among orders at the same price.
- **Crossing BUY:** an incoming buy limit order can match when `incoming.price >= best_ask`.
- **Crossing SELL:** an incoming sell limit order can match when `incoming.price <= best_bid`.
- **Execution quantity:** `min(incoming_remaining_quantity, resting_remaining_quantity)`.
- **Partial fill:** keep the remaining quantity of the order that is not fully executed.
- **Full fill:** remove a resting order when its remaining quantity reaches zero; do not add an exhausted incoming order to the book.
- **Execution price:** use the resting order's price as the initial convention.
- **Trade event:** record buy order ID, sell order ID, execution price, and executed quantity.
- **Best-price invariants:** `best_bid` is empty exactly when there are no resting buy orders; otherwise it equals the highest occupied buy price. `best_ask` is empty exactly when there are no resting sell orders; otherwise it equals the lowest occupied sell price.

## Build and test workflow

The project uses CMake presets and FetchContent for GoogleTest 1.17.0 and Google Benchmark 1.9.4.

From a Visual Studio developer environment with the configured CMake generator available:

```powershell
cmake --preset windows-debug
cmake --build --preset debug
ctest --preset debug-tests
```

For a release build:

```powershell
cmake --preset windows-release
cmake --build --preset release
```

The current presets use the `Visual Studio 18 2026` generator, x64 architecture, and enable tests and benchmarks by default. If CMake configuration or compilation fails, record the full diagnostic before changing toolchain settings.

**Status caveat:** The repository configuration has been inspected, but a successful full build, test run, and benchmark run have not been established by this README update.

## Roadmap

### Milestone 0 — Build and API structure

- Create `include/matching_engine/order_book.hpp`.
- Move the class declaration into the public header and keep method definitions in `src/order_book.cpp`.
- Add `src/order_book.cpp` to `add_library(matching_engine ...)`.
- Make the project compile with warnings enabled and run the existing tests.

### Milestone 1 — Correct single-threaded matching

- Fix optional best-price initialization and exhausted-side handling.
- Validate constructor parameters and incoming prices before indexing.
- Specify input ownership and invalid-order behavior.
- Implement crossing, partial fills, full fills, multi-level matching, and resting-price execution.
- Add a trade event/result representation.
- Keep the logic single-threaded and easy to reason about.

### Milestone 2 — Behavioral tests and invariants

Cover at least:

- Empty book and one-sided book.
- Non-crossing buy and sell orders.
- Exact-price crossing and price improvement.
- Partial and full fills.
- Several resting orders at one price (FIFO).
- Matches spanning multiple price levels.
- Exhaustion of the last occupied level and re-insertion afterward.
- Best bid/ask updates after insertion and consumption.
- Boundary and invalid prices, invalid constructor bounds, and zero quantities.
- Randomized sequences checked against a simple reference model once deterministic tests pass.

### Milestone 3 — Matching benchmarks

Replace or supplement the order-construction benchmark with benchmarks for:

- Inserting a non-crossing order.
- An order that immediately fully matches.
- Partial matches.
- Matching against multiple FIFO orders at the same price.
- Matching across multiple price levels.
- Dense versus sparse price occupancy and varying queue depths.

Keep setup outside the measured section when it is not part of the operation being measured. Separate debug correctness checks from release performance runs. Record compiler, build type, CPU, operating system, input distribution, and benchmark command. Measure throughput and latency; investigate percentile latency with an appropriate harness rather than inferring it from a simple average.

### Milestone 4 — Data-structure experiments

Compare the dense ladder with alternatives only after baseline behavior and measurements are reliable:

- Tree-based price levels for sparse price distributions.
- Occupancy bitmaps or hierarchical bitsets to skip empty levels.
- More compact price-level metadata.
- An order-ID index and a representation suitable for cancellation.
- A pool or arena for order storage if allocation measurements show it matters.

Record both the workload assumptions and the trade-offs. Do not assume a structure is faster solely from asymptotic complexity.

### Milestone 5 — Memory and microarchitecture

Investigate, using profiling and measurements:

- Cache misses and access locality.
- Allocation frequency and allocator overhead.
- Object layout, padding, and alignment.
- Data movement and branch behavior.
- CPU affinity and scheduling effects.
- Hardware counters where supported.

Use `alignas` and cache-line separation only when a measured layout or sharing problem motivates them.

### Milestone 6 — Concurrency architecture

Only after the single-threaded core is correct:

- Define ownership of each mutable book.
- Consider a single-writer event loop and message passing as an initial architecture.
- Add bounded queues and explicit back-pressure if required.
- Introduce atomics and memory-ordering choices only for identified shared-state needs.
- Test under contention and use race-detection tools on supported platforms.

A lock-free implementation is not an initial requirement; correctness, ownership, and measurable bottlenecks come first.

### Milestone 7 — Linux investigation and deployment tuning

Port or validate builds on Linux, then investigate CPU pinning, scheduler behavior, NUMA placement, page faults, memory locking where appropriate, and system-level profiling. Keep operating-system-specific tuning separate from portable matching logic.

## Working approach for future project chats

Use this README as the handoff point for continuing the project.

- First inspect the current repository files and build state; treat this document as context, not proof that an item is implemented or tested.
- Explain the relevant mechanism briefly before introducing a low-level optimization.
- For core C++ algorithms, prefer incremental hints and review over dropping in a complete implementation. The learner should implement the matching logic and then receive a strict correctness review.
- Be explicit about invariants, undefined behavior, ownership, memory allocation, cache locality, concurrency hazards, and complexity.
- It is appropriate to make build-system and repository-plumbing changes directly when they unblock progress.
- Benchmark before claiming a performance improvement. Keep the first version readable and correct.
- Do not start with lock-free algorithms, CPU pinning, or cache-line padding before there is a meaningful baseline.

## Immediate next actions

1. Add the public `OrderBook` header and register the implementation in CMake.
2. Fix the empty-`optional` insertion path and best-ask exhaustion path.
3. Validate price bounds before indexing and define constructor behavior for invalid ranges.
4. Write deterministic order-book tests for side initialization, crossing, partial/full fills, FIFO, multi-level matching, and best-price invariants.
5. Build and run tests on Windows; record the actual result.
6. Add trade events and matching benchmarks, then establish a reproducible baseline.
