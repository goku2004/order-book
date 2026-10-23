#include "matching_engine/order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using matching_engine::Order;
using matching_engine::OrderBook;
using matching_engine::Side;

struct LatencyStats {
    uint64_t min_ns{};
    double median_ns{};
    double mean_ns{};
    double stddev_ns{}; // Population standard deviation.
    uint64_t p95_ns{};
    uint64_t p99_ns{};
    uint64_t p999_ns{};
    uint64_t max_ns{};
};

uint64_t percentile_nearest_rank(
    const std::vector<uint64_t>& sorted,
    uint64_t numerator,
    uint64_t denominator)
{
    const auto n = static_cast<uint64_t>(sorted.size());
    const uint64_t rank = (n * numerator + denominator - 1) / denominator;
    return sorted[static_cast<std::size_t>(rank - 1)];
}

LatencyStats calculate_stats(const std::vector<uint64_t>& samples)
{
    if (samples.empty()) {
        throw std::invalid_argument("Latency samples cannot be empty");
    }

    // Sort a copy so the original vector remains in time order for trace plots.
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());

    const std::size_t n = sorted.size();
    long double total = 0.0L;
    for (const auto sample : samples) {
        total += static_cast<long double>(sample);
    }
    const long double mean = total / static_cast<long double>(n);

    long double squared_deviations = 0.0L;
    for (const auto sample : samples) {
        const long double delta = static_cast<long double>(sample) - mean;
        squared_deviations += delta * delta;
    }

    LatencyStats stats{};
    stats.min_ns = sorted.front();
    stats.max_ns = sorted.back();
    stats.mean_ns = static_cast<double>(mean);
    stats.stddev_ns = static_cast<double>(
        std::sqrt(squared_deviations / static_cast<long double>(n)));

    if (n % 2 == 0) {
        stats.median_ns =
            (static_cast<double>(sorted[n / 2 - 1]) +
             static_cast<double>(sorted[n / 2])) / 2.0;
    } else {
        stats.median_ns = static_cast<double>(sorted[n / 2]);
    }

    stats.p95_ns = percentile_nearest_rank(sorted, 95, 100);
    stats.p99_ns = percentile_nearest_rank(sorted, 99, 100);
    stats.p999_ns = percentile_nearest_rank(sorted, 999, 1000);
    return stats;
}

std::string csv_escape(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string timestamp_for_path()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;

    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif

    std::ostringstream out;
    out << std::put_time(&local_time, "%Y%m%d_%H%M%S")
        << '_' << std::setw(3) << std::setfill('0') << millis;
    return out.str();
}

std::string platform_name()
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

std::string compiler_name()
{
#if defined(_MSC_VER)
    return "MSVC _MSC_VER=" + std::to_string(_MSC_VER) +
           " _MSC_FULL_VER=" + std::to_string(_MSC_FULL_VER);
#elif defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#else
    return "Unknown compiler";
#endif
}

struct ReportRow {
    std::string name;
    std::size_t sample_count{};
    LatencyStats stats{};
    uint64_t checksum{};
};

class LatencyReport {
public:
    LatencyReport()
    {
        const auto root = std::filesystem::current_path() / "benchmark_results";
        std::filesystem::create_directories(root);

        const auto stem = "latency_" + timestamp_for_path();
        directory_ = root / stem;
        for (unsigned suffix = 1; std::filesystem::exists(directory_); ++suffix) {
            directory_ = root / (stem + "_" + std::to_string(suffix));
        }
        std::filesystem::create_directories(directory_);

        summary_.open(directory_ / "summary.csv", std::ios::out);
        samples_.open(directory_ / "samples.csv", std::ios::out);
        if (!summary_ || !samples_) {
            throw std::runtime_error("Could not open latency report CSV files");
        }

        summary_ << "workflow,sample_count,min_ns,median_ns,mean_ns,stddev_ns,"
                    "p95_ns,p99_ns,p99_9_ns,max_ns,checksum\n";
        samples_ << "workflow,sample_index,latency_ns\n";

        write_metadata();
    }

    const std::filesystem::path& directory() const noexcept
    {
        return directory_;
    }

    void record(
        std::string_view name,
        const std::vector<uint64_t>& samples,
        const LatencyStats& stats,
        uint64_t checksum)
    {
        const auto workflow = csv_escape(name);

        summary_ << workflow << ',' << samples.size() << ','
                 << stats.min_ns << ','
                 << std::fixed << std::setprecision(3)
                 << stats.median_ns << ','
                 << stats.mean_ns << ','
                 << stats.stddev_ns << ','
                 << stats.p95_ns << ','
                 << stats.p99_ns << ','
                 << stats.p999_ns << ','
                 << stats.max_ns << ','
                 << checksum << '\n';

        // Preserve sample order: this enables spotting time-localized spikes.
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples_ << workflow << ',' << i << ',' << samples[i] << '\n';
        }

        rows_.push_back(ReportRow{
            .name = std::string(name),
            .sample_count = samples.size(),
            .stats = stats,
            .checksum = checksum
        });

        summary_.flush();
        samples_.flush();
        if (!summary_ || !samples_) {
            throw std::runtime_error("Failed while writing latency CSV report");
        }
    }

    void finish()
    {
        summary_.flush();
        samples_.flush();

        std::ofstream markdown(directory_ / "summary.md", std::ios::out);
        if (!markdown) {
            throw std::runtime_error("Could not open summary.md");
        }

        markdown << "# Order-book latency report\n\n"
                 << "Each row summarizes per-call timings from this run. "
                    "See metadata.txt for environment details and "
                    "samples.csv for individual samples in original order.\n\n"
                 << "Percentiles use the nearest-rank convention. Times are in "
                    "nanoseconds. Clock-pair overhead is measured as its own "
                    "workflow and is not subtracted from operation samples.\n\n"
                 << "| Workflow | Samples | Min (ns) | Median (ns) | Mean (ns) | "
                    "Stddev (ns) | p95 (ns) | p99 (ns) | p99.9 (ns) | "
                    "Max (ns) | Checksum |\n"
                 << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";

        markdown << std::fixed << std::setprecision(2);
        for (const auto& row : rows_) {
            markdown << "| " << row.name << " | " << row.sample_count
                     << " | " << row.stats.min_ns
                     << " | " << row.stats.median_ns
                     << " | " << row.stats.mean_ns
                     << " | " << row.stats.stddev_ns
                     << " | " << row.stats.p95_ns
                     << " | " << row.stats.p99_ns
                     << " | " << row.stats.p999_ns
                     << " | " << row.stats.max_ns
                     << " | " << row.checksum << " |\n";
        }

        markdown << "\n## Interpretation notes\n\n"
                 << "- A sample includes two steady_clock::now() calls and the "
                    "public add_order call. Incoming order construction is outside "
                    "the timed region.\n"
                 << "- The returned trade vector is constructed during add_order; "
                    "its destruction and checksum update occur after the stop timestamp.\n"
                 << "- These synthetic workloads isolate specific paths; they are not "
                    "a replay of real exchange traffic.\n"
                 << "- Tail outliers alone do not identify their cause. Repeat runs and "
                    "profiling are needed before attributing them to the allocator, "
                    "the operating system, or matching logic.\n";

        markdown.flush();
        if (!markdown) {
            throw std::runtime_error("Failed while writing summary.md");
        }
    }

private:
    std::filesystem::path directory_;
    std::ofstream summary_;
    std::ofstream samples_;
    std::vector<ReportRow> rows_;

    void write_metadata()
    {
        std::ofstream metadata(directory_ / "metadata.txt", std::ios::out);
        if (!metadata) {
            throw std::runtime_error("Could not open metadata.txt");
        }

        metadata << "platform=" << platform_name() << '\n'
                 << "compiler=" << compiler_name() << '\n'
#ifdef NDEBUG
                 << "build_mode=NDEBUG defined (typically Release)\n"
#else
                 << "build_mode=NDEBUG not defined (typically Debug)\n"
#endif
                 << "cplusplus=" << __cplusplus << '\n'
                 << "hardware_concurrency_hint=" << std::thread::hardware_concurrency() << '\n'
                 << "clock=std::chrono::steady_clock\n"
                 << "clock_is_steady=" << std::boolalpha << Clock::is_steady << '\n'
                 << "clock_period_num=" << Clock::period::num << '\n'
                 << "clock_period_den=" << Clock::period::den << '\n'
                 << "working_directory=" << std::filesystem::current_path().string() << '\n'
                 << "latency_method=one steady_clock interval per add_order call\n"
                 << "percentile_method=nearest rank\n"
                 << "timer_overhead=measured as a separate workflow; not subtracted\n"
                 << "cpu_model=not automatically captured\n";
    }
};

void print_stats(
    std::string_view name,
    const std::vector<uint64_t>& samples,
    const LatencyStats& stats,
    uint64_t checksum,
    LatencyReport& report)
{
    std::cout << "\nWorkflow: " << name << '\n'
              << "Samples:  " << samples.size() << '\n'
              << std::fixed << std::setprecision(2)
              << "Min:      " << stats.min_ns << " ns\n"
              << "Median:   " << stats.median_ns << " ns\n"
              << "Mean:     " << stats.mean_ns << " ns\n"
              << "Stddev:   " << stats.stddev_ns << " ns\n"
              << "P95:      " << stats.p95_ns << " ns\n"
              << "P99:      " << stats.p99_ns << " ns\n"
              << "P99.9:    " << stats.p999_ns << " ns\n"
              << "Max:      " << stats.max_ns << " ns\n"
              << "Checksum: " << checksum << '\n';

    report.record(name, samples, stats, checksum);
}

template <typename Fn>
uint64_t time_one_call(Fn&& fn, uint64_t& checksum)
{
    const auto start = Clock::now();
    auto trades = fn();
    const auto stop = Clock::now();

    // The returned trade vector is created inside the timed API call.
    // Its destruction and checksum update happen after the stop timestamp.
    checksum += static_cast<uint64_t>(trades.size());

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);
    return static_cast<uint64_t>(elapsed.count());
}

template <typename Fn>
void run_unmeasured(Fn&& fn, uint64_t& checksum)
{
    auto trades = fn();
    checksum += static_cast<uint64_t>(trades.size());
}

Order make_order(uint64_t id, int64_t price, uint32_t quantity, Side side)
{
    return Order{
        .order_id = id,
        .price = price,
        .quantity = quantity,
        .side = side
    };
}

void run_clock_pair_baseline(LatencyReport& report)
{
    constexpr std::size_t sample_count = 100'000;
    std::vector<uint64_t> samples(sample_count);

    for (std::size_t i = 0; i < sample_count; ++i) {
        const auto start = Clock::now();
        const auto stop = Clock::now();
        samples[i] = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                stop - start).count());
    }

    const auto stats = calculate_stats(samples);
    print_stats("Clock-pair baseline (not subtracted)", samples, stats, 0, report);
}

void run_resting_insert(
    std::size_t warmup_count,
    std::size_t sample_count,
    LatencyReport& report)
{
    OrderBook book(1024);
    std::vector<uint64_t> samples(sample_count);
    uint64_t checksum = 0;
    uint64_t next_id = 1;

    for (std::size_t i = 0; i < warmup_count; ++i) {
        auto order = make_order(next_id++, 400, 1, Side::Buy);
        run_unmeasured([&] { return book.add_order(order); }, checksum);
    }

    for (std::size_t i = 0; i < sample_count; ++i) {
        auto order = make_order(next_id++, 400, 1, Side::Buy);
        samples[i] = time_one_call([&] { return book.add_order(order); }, checksum);
    }

    const auto stats = calculate_stats(samples);
    print_stats("Resting insertion", samples, stats, checksum, report);
}

void run_full_match(
    std::size_t warmup_count,
    std::size_t sample_count,
    LatencyReport& report)
{
    OrderBook book(1024);
    std::vector<uint64_t> samples(sample_count);
    uint64_t checksum = 0;
    uint64_t next_id = 1;

    // An extra resting ask ensures the sell side remains occupied throughout.
    const std::size_t resting_count = warmup_count + sample_count + 1;
    for (std::size_t i = 0; i < resting_count; ++i) {
        auto ask = make_order(next_id++, 500, 1, Side::Sell);
        run_unmeasured([&] { return book.add_order(ask); }, checksum);
    }

    next_id = 1'000'000;
    for (std::size_t i = 0; i < warmup_count; ++i) {
        auto buy = make_order(next_id++, 501, 1, Side::Buy);
        run_unmeasured([&] { return book.add_order(buy); }, checksum);
    }

    for (std::size_t i = 0; i < sample_count; ++i) {
        auto buy = make_order(next_id++, 501, 1, Side::Buy);
        samples[i] = time_one_call([&] { return book.add_order(buy); }, checksum);
    }

    const auto stats = calculate_stats(samples);
    print_stats("Full match", samples, stats, checksum, report);
}

void run_partial_fill(
    std::size_t warmup_count,
    std::size_t sample_count,
    LatencyReport& report)
{
    if (warmup_count + sample_count >=
        std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument("Too many samples for resting-order quantity");
    }

    OrderBook book(1024);
    std::vector<uint64_t> samples(sample_count);
    uint64_t checksum = 0;

    const auto resting_quantity =
        static_cast<uint32_t>(warmup_count + sample_count + 1);
    auto ask = make_order(1, 500, resting_quantity, Side::Sell);
    run_unmeasured([&] { return book.add_order(ask); }, checksum);

    uint64_t next_id = 1'000'000;
    for (std::size_t i = 0; i < warmup_count; ++i) {
        auto buy = make_order(next_id++, 501, 1, Side::Buy);
        run_unmeasured([&] { return book.add_order(buy); }, checksum);
    }

    for (std::size_t i = 0; i < sample_count; ++i) {
        auto buy = make_order(next_id++, 501, 1, Side::Buy);
        samples[i] = time_one_call([&] { return book.add_order(buy); }, checksum);
    }

    const auto stats = calculate_stats(samples);
    print_stats("Partial fill", samples, stats, checksum, report);
}

void run_best_ask_exhaustion(
    std::size_t warmup_count,
    std::size_t sample_count,
    LatencyReport& report)
{
    OrderBook book(1024);
    std::vector<uint64_t> samples(sample_count);
    uint64_t checksum = 0;
    uint64_t next_id = 1;

    auto prepare_ask = [&] {
        auto ask = make_order(next_id++, 500, 1, Side::Sell);
        run_unmeasured([&] { return book.add_order(ask); }, checksum);
    };

    uint64_t incoming_id = 1'000'000;
    for (std::size_t i = 0; i < warmup_count; ++i) {
        prepare_ask();
        auto buy = make_order(incoming_id++, 500, 1, Side::Buy);
        run_unmeasured([&] { return book.add_order(buy); }, checksum);
    }

    for (std::size_t i = 0; i < sample_count; ++i) {
        prepare_ask(); // Setup is deliberately outside the timed interval.
        auto buy = make_order(incoming_id++, 500, 1, Side::Buy);
        samples[i] = time_one_call([&] { return book.add_order(buy); }, checksum);
    }

    const auto stats = calculate_stats(samples);
    print_stats("Best-ask exhaustion", samples, stats, checksum, report);
}

void run_multilevel_sweep(
    std::size_t warmup_count,
    std::size_t sample_count,
    uint32_t levels,
    LatencyReport& report)
{
    if (levels == 0 || 500 + levels >= 1024) {
        throw std::invalid_argument("Invalid sweep level count");
    }

    OrderBook book(1024);
    std::vector<uint64_t> samples(sample_count);
    uint64_t checksum = 0;
    uint64_t next_id = 1;

    // Keep the next ask adjacent so this workload isolates occupied-level sweeps.
    auto sentinel = make_order(next_id++, 500 + levels, 1, Side::Sell);
    run_unmeasured([&] { return book.add_order(sentinel); }, checksum);

    auto prepare_levels = [&] {
        for (uint32_t level = 0; level < levels; ++level) {
            auto ask = make_order(next_id++, 500 + level, 1, Side::Sell);
            run_unmeasured([&] { return book.add_order(ask); }, checksum);
        }
    };

    uint64_t incoming_id = 1'000'000;
    for (std::size_t i = 0; i < warmup_count; ++i) {
        prepare_levels();
        auto buy = make_order(
            incoming_id++, 499 + levels, static_cast<uint32_t>(levels), Side::Buy);
        run_unmeasured([&] { return book.add_order(buy); }, checksum);
    }

    for (std::size_t i = 0; i < sample_count; ++i) {
        prepare_levels(); // Deliberately excluded from the measured operation.
        auto buy = make_order(
            incoming_id++, 499 + levels, static_cast<uint32_t>(levels), Side::Buy);
        samples[i] = time_one_call([&] { return book.add_order(buy); }, checksum);
    }

    const auto stats = calculate_stats(samples);
    const std::string label =
        "Multi-level sweep (" + std::to_string(levels) + " levels)";
    print_stats(label, samples, stats, checksum, report);
}

// Inserts one ask at price 100, then consumes it. A resting sentinel at
// 100 + gap makes best_ask maintenance scan a controlled number of empty ticks.
void run_sparse_gap_traversal(
    std::size_t warmup_count,
    std::size_t sample_count,
    uint32_t gap,
    LatencyReport& report)
{
    if (gap == 0 || 100 + gap >= 1024) {
        throw std::invalid_argument("Invalid sparse traversal gap");
    }

    OrderBook book(1024);
    std::vector<uint64_t> samples(sample_count);
    uint64_t checksum = 0;
    uint64_t next_id = 1;

    auto sentinel = make_order(next_id++, 100 + gap, 1, Side::Sell);
    run_unmeasured([&] { return book.add_order(sentinel); }, checksum);

    auto prepare_best_ask = [&] {
        auto ask = make_order(next_id++, 100, 1, Side::Sell);
        run_unmeasured([&] { return book.add_order(ask); }, checksum);
    };

    uint64_t incoming_id = 1'000'000;
    for (std::size_t i = 0; i < warmup_count; ++i) {
        prepare_best_ask();
        auto buy = make_order(incoming_id++, 100, 1, Side::Buy);
        run_unmeasured([&] { return book.add_order(buy); }, checksum);
    }

    for (std::size_t i = 0; i < sample_count; ++i) {
        prepare_best_ask(); // Excluded: only consuming the best ask is timed.
        auto buy = make_order(incoming_id++, 100, 1, Side::Buy);
        samples[i] = time_one_call([&] { return book.add_order(buy); }, checksum);
    }

    const auto stats = calculate_stats(samples);
    const std::string label =
        "Sparse traversal (gap " + std::to_string(gap) + " ticks)";
    print_stats(label, samples, stats, checksum, report);
}

} // namespace

int main()
{
    try {
        LatencyReport report;
        constexpr std::size_t warmup_count = 10'000;
        constexpr std::size_t sample_count = 100'000;

        run_clock_pair_baseline(report);
        run_resting_insert(warmup_count, sample_count, report);
        run_full_match(warmup_count, sample_count, report);
        run_partial_fill(warmup_count, sample_count, report);
        run_best_ask_exhaustion(2'000, 20'000, report);
        run_multilevel_sweep(1'000, 10'000, 16, report);
        run_multilevel_sweep(500, 2'000, 64, report);
        run_multilevel_sweep(100, 2'000, 256, report);

        // Hold the number of resting orders constant while varying empty ticks.
        run_sparse_gap_traversal(2'000, 20'000, 1, report);
        run_sparse_gap_traversal(2'000, 20'000, 16, report);
        run_sparse_gap_traversal(2'000, 20'000, 64, report);
        run_sparse_gap_traversal(2'000, 20'000, 256, report);

        report.finish();
        std::cout << "\nLatency report saved to: "
                  << report.directory().string() << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "Latency benchmark failed: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
