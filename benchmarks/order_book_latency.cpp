
#include "matching_engine/order_book.hpp"
#include "matching_engine/order.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using matching_engine::Order;
using matching_engine::OrderBook;
using matching_engine::Side;

// The latency harness stores one elapsed time per operation.
struct LatencyStats {
    double median_ns{};
    uint64_t p95_ns{};
    uint64_t p99_ns{};
    uint64_t p999_ns{};
    uint64_t max_ns{};
};

// Nearest-rank percentile:
// rank = ceil(n * numerator / denominator), using 1-based ranks.
uint64_t percentile_nearest_rank(
    const std::vector<uint64_t>& sorted,
    uint64_t numerator,
    uint64_t denominator)
{
    const uint64_t n = sorted.size();
    const uint64_t rank =
        (n * numerator + denominator - 1) / denominator;

    return sorted[rank - 1];
}

LatencyStats calculate_stats(std::vector<uint64_t>& samples)
{
    if (samples.empty()) {
        throw std::invalid_argument("Latency samples cannot be empty");
    }

    // Sort in place to avoid allocating and copying a second vector.
    std::sort(samples.begin(), samples.end());

    const std::size_t n = samples.size();

    LatencyStats stats{};

    if (n % 2 == 0) {
        // Promote before addition to avoid uint64_t addition overflow.
        stats.median_ns =
            (static_cast<double>(samples[n / 2 - 1]) +
             static_cast<double>(samples[n / 2])) / 2.0;
    } else {
        stats.median_ns = static_cast<double>(samples[n / 2]);
    }

    stats.p95_ns  = percentile_nearest_rank(samples, 95, 100);
    stats.p99_ns  = percentile_nearest_rank(samples, 99, 100);
    stats.p999_ns = percentile_nearest_rank(samples, 999, 1000);
    stats.max_ns  = samples.back();

    return stats;
}

void print_stats(
    std::string_view name,
    const std::vector<uint64_t>& samples,
    const LatencyStats& stats,
    uint64_t checksum)
{
    std::cout << "\nWorkflow: " << name << '\n'
              << "Samples:  " << samples.size() << '\n'
              << std::fixed << std::setprecision(2)
              << "Median:   " << stats.median_ns << " ns\n"
              << "P95:      " << stats.p95_ns << " ns\n"
              << "P99:      " << stats.p99_ns << " ns\n"
              << "P99.9:    " << stats.p999_ns << " ns\n"
              << "Max:      " << stats.max_ns << " ns\n"
              << "Checksum: " << checksum << '\n';
}

// Times only fn(). The result is consumed after the stop timestamp.
// This assumes add_order() returns std::vector<Trade>.
template <typename Fn>
uint64_t time_one_call(Fn&& fn, uint64_t& checksum)
{
    const auto start = Clock::now();
    auto trades = fn();
    const auto stop = Clock::now();

    // Keep the returned result observable, outside the timed interval.
    checksum += static_cast<uint64_t>(trades.size());

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            stop - start);

    return static_cast<uint64_t>(elapsed.count());
}

template <typename Fn>
void run_unmeasured(Fn&& fn, uint64_t& checksum)
{
    auto trades = fn();
    checksum += static_cast<uint64_t>(trades.size());
}

Order make_order(
    uint64_t id,
    int64_t price,
    uint32_t quantity,
    Side side)
{
    return Order{
        .order_id = id,
        .price = price,
        .quantity = quantity,
        .side = side
    };
}

// ------------------------------------------------------------------
// 1. Resting insertion
//
// Measures adding non-crossing buy orders to the same price level.
// The queue grows over time, so this can reveal allocation-related
// latency spikes as its underlying storage grows.
// ------------------------------------------------------------------
void run_resting_insert(
    std::size_t warmup_count,
    std::size_t sample_count)
{
    OrderBook book(1024);
    std::vector<uint64_t> samples(sample_count);
    uint64_t checksum = 0;
    uint64_t next_id = 1;

    for (std::size_t i = 0; i < warmup_count; ++i) {
        auto order = make_order(next_id++, 400, 1, Side::Buy);
        run_unmeasured(
            [&] { return book.add_order(order); }, checksum);
    }

    for (std::size_t i = 0; i < sample_count; ++i) {
        auto order = make_order(next_id++, 400, 1, Side::Buy);

        samples[i] = time_one_call(
            [&] { return book.add_order(order); }, checksum);
    }

    const auto stats = calculate_stats(samples);
    print_stats("Resting insertion", samples, stats, checksum);
}

// ------------------------------------------------------------------
// 2. Full match
//
// Preload individual one-unit asks. Each incoming buy consumes one
// entire resting order. One extra ask prevents best-ask exhaustion
// during the measured samples.
// ------------------------------------------------------------------
void run_full_match(
    std::size_t warmup_count,
    std::size_t sample_count)
{
    OrderBook book(1024);
    std::vector<uint64_t> samples(sample_count);
    uint64_t checksum = 0;
    uint64_t next_id = 1;

    const std::size_t resting_count =
        warmup_count + sample_count + 1;

    for (std::size_t i = 0; i < resting_count; ++i) {
        auto ask = make_order(next_id++, 500, 1, Side::Sell);
        run_unmeasured(
            [&] { return book.add_order(ask); }, checksum);
    }

    next_id = 1'000'000;

    for (std::size_t i = 0; i < warmup_count; ++i) {
        auto buy = make_order(next_id++, 501, 1, Side::Buy);
        run_unmeasured(
            [&] { return book.add_order(buy); }, checksum);
    }

    for (std::size_t i = 0; i < sample_count; ++i) {
        auto buy = make_order(next_id++, 501, 1, Side::Buy);

        samples[i] = time_one_call(
            [&] { return book.add_order(buy); }, checksum);
    }

    const auto stats = calculate_stats(samples);
    print_stats("Full match", samples, stats, checksum);
}

// ------------------------------------------------------------------
// 3. Partial fill of a resting order
//
// Each incoming buy has quantity 1, while the resting ask has enough
// quantity to remain present throughout all measured operations.
// ------------------------------------------------------------------
void run_partial_fill(
    std::size_t warmup_count,
    std::size_t sample_count)
{
    if (warmup_count + sample_count >=
        std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument("Too many samples for order quantity");
    }

    OrderBook book(1024);
    std::vector<uint64_t> samples(sample_count);
    uint64_t checksum = 0;

    const auto resting_quantity =
        static_cast<uint32_t>(warmup_count + sample_count + 1);

    auto ask = make_order(1, 500, resting_quantity, Side::Sell);
    run_unmeasured(
        [&] { return book.add_order(ask); }, checksum);

    uint64_t next_id = 1'000'000;

    for (std::size_t i = 0; i < warmup_count; ++i) {
        auto buy = make_order(next_id++, 501, 1, Side::Buy);
        run_unmeasured(
            [&] { return book.add_order(buy); }, checksum);
    }

    for (std::size_t i = 0; i < sample_count; ++i) {
        auto buy = make_order(next_id++, 501, 1, Side::Buy);

        samples[i] = time_one_call(
            [&] { return book.add_order(buy); }, checksum);
    }

    const auto stats = calculate_stats(samples);
    print_stats("Partial fill", samples, stats, checksum);
}

// ------------------------------------------------------------------
// 4. Best-ask exhaustion
//
// Reinsert one ask outside the timed region before each operation.
// The timed buy consumes it, forcing the engine to update best_ask.
// ------------------------------------------------------------------
void run_best_ask_exhaustion(
    std::size_t warmup_count,
    std::size_t sample_count)
{
    OrderBook book(1024);
    std::vector<uint64_t> samples(sample_count);
    uint64_t checksum = 0;
    uint64_t next_id = 1;

    auto prepare_ask = [&] {
        auto ask = make_order(next_id++, 500, 1, Side::Sell);
        run_unmeasured(
            [&] { return book.add_order(ask); }, checksum);
    };

    uint64_t incoming_id = 1'000'000;

    for (std::size_t i = 0; i < warmup_count; ++i) {
        prepare_ask();

        auto buy = make_order(incoming_id++, 500, 1, Side::Buy);
        run_unmeasured(
            [&] { return book.add_order(buy); }, checksum);
    }

    for (std::size_t i = 0; i < sample_count; ++i) {
        prepare_ask(); // Excluded from timing.

        auto buy = make_order(incoming_id++, 500, 1, Side::Buy);

        samples[i] = time_one_call(
            [&] { return book.add_order(buy); }, checksum);
    }

    const auto stats = calculate_stats(samples);
    print_stats("Best-ask exhaustion", samples, stats, checksum);
}

// ------------------------------------------------------------------
// 5. Multi-level sweep
//
// Every incoming buy consumes one ask at each of 'levels' consecutive
// prices. A persistent ask at price 900 ensures the book has a known
// next best ask after the sweep. Setup/replenishment is untimed.
// ------------------------------------------------------------------
void run_multilevel_sweep(
    std::size_t warmup_count,
    std::size_t sample_count,
    uint32_t levels)
{
    if (levels == 0 || 500 + levels >= 900) {
        throw std::invalid_argument("Invalid sweep level count");
    }

    OrderBook book(1024);
    std::vector<uint64_t> samples(sample_count);
    uint64_t checksum = 0;
    uint64_t next_id = 1;

    // Persistent sentinel ask: the sweep never reaches price 900.
    auto sentinel = make_order(next_id++, 900, 1, Side::Sell);
    run_unmeasured(
        [&] { return book.add_order(sentinel); }, checksum);

    auto prepare_levels = [&] {
        for (uint32_t level = 0; level < levels; ++level) {
            auto ask = make_order(
                next_id++, 500 + level, 1, Side::Sell);

            run_unmeasured(
                [&] { return book.add_order(ask); }, checksum);
        }
    };

    uint64_t incoming_id = 1'000'000;

    for (std::size_t i = 0; i < warmup_count; ++i) {
        prepare_levels();

        auto buy = make_order(
            incoming_id++, 499 + levels, levels, Side::Buy);

        run_unmeasured(
            [&] { return book.add_order(buy); }, checksum);
    }

    for (std::size_t i = 0; i < sample_count; ++i) {
        prepare_levels(); // Excluded from timing.

        auto buy = make_order(
            incoming_id++, 499 + levels, levels, Side::Buy);

        samples[i] = time_one_call(
            [&] { return book.add_order(buy); }, checksum);
    }

    const auto stats = calculate_stats(samples);

    const std::string label =
        "Multi-level sweep (" + std::to_string(levels) + " levels)";

    print_stats(label, samples, stats, checksum);
}

} // namespace

int main()
{
    constexpr std::size_t warmup_count = 10'000;
    constexpr std::size_t sample_count = 100'000;

    run_resting_insert(warmup_count, sample_count);
    run_full_match(warmup_count, sample_count);
    run_partial_fill(warmup_count, sample_count);

    // Each measured exhaustion scans from price 500 toward the
    // upper end of the ladder, so start with fewer samples.
    run_best_ask_exhaustion(2'000, 20'000);

    // Replenishment cost is excluded, but larger sweeps require
    // considerably more setup work per sample.
    run_multilevel_sweep(1'000, 10'000, 16);
    run_multilevel_sweep(500, 2'000, 64);
    run_multilevel_sweep(100, 500, 256);
}
