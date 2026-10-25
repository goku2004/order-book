#include <array>
#include <cstddef>
#include <cstdint>
#include <benchmark/benchmark.h>

#include "matching_engine/order.hpp"
#include "matching_engine/order_book.hpp"
#include "seeded_replay.hpp"

using matching_engine::Order;
using matching_engine::OrderBook;
using matching_engine::Side;


static void KeepResult(const std::vector<matching_engine::Trade>& trades)
{
    benchmark::DoNotOptimize(trades.data());
    benchmark::DoNotOptimize(trades.size());
}

static void BM_BookLifecycle(benchmark::State& state)
{
    const auto max_price = state.range(0);

    for (auto _ : state) {
        OrderBook book{max_price};

        benchmark::DoNotOptimize(book.best_bid());
        benchmark::DoNotOptimize(book.best_ask());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations());
}


static void BM_SingleLevel_BuyAgainstAsk(benchmark::State& state)
{
    const auto batch_size =
        static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        state.PauseTiming();
        {
            OrderBook book{1000};

            for (std::size_t i = 0; i < batch_size; ++i) {
                book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(i + 1),
                    .price = 100,
                    .quantity = 1,
                    .side = Side::Sell
                });
            }

            // Keeps the next ask close when price 100 is exhausted.
            book.add_order(Order{
                .order_id = 1'000'000,
                .price = 101,
                .quantity = 1,
                .side = Side::Sell
            });

            state.ResumeTiming();

            for (std::size_t i = 0; i < batch_size; ++i) {
                auto trades = book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(2'000'000 + i),
                    .price = 100,
                    .quantity = 1,
                    .side = Side::Buy
                });

                KeepResult(trades);
            }
            benchmark::ClobberMemory();
            state.PauseTiming();
        } // Book destruction is excluded from the timed interval.
        state.ResumeTiming();
    }

    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

static void BM_SingleLevel_SellAgainstBid(benchmark::State& state)
{
    const auto batch_size =
        static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        state.PauseTiming();
        {
            OrderBook book{1000};

            for (std::size_t i = 0; i < batch_size; ++i) {
                book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(i + 1),
                    .price = 100,
                    .quantity = 1,
                    .side = Side::Buy
                });
            }

            // Keeps the next bid close when price 100 is exhausted.
            book.add_order(Order{
                .order_id = 1'000'000,
                .price = 99,
                .quantity = 1,
                .side = Side::Buy
            });

            state.ResumeTiming();

            for (std::size_t i = 0; i < batch_size; ++i) {
                auto trades = book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(2'000'000 + i),
                    .price = 100,
                    .quantity = 1,
                    .side = Side::Sell
                });

                KeepResult(trades);
            }
            benchmark::ClobberMemory();
            state.PauseTiming();
        }
        state.ResumeTiming();
    }

    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

static void BM_NonCrossingBids(benchmark::State& state)
{
    const auto batch_size =
        static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        state.PauseTiming();
        {
            OrderBook book{1000};
            state.ResumeTiming();

            for (std::size_t i = 0; i < batch_size; ++i) {
                auto trades = book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(i + 1),
                    .price = 100,
                    .quantity = 1,
                    .side = Side::Buy
                });

                KeepResult(trades);
            }
            benchmark::ClobberMemory();
            state.PauseTiming();
        }
        state.ResumeTiming();
    }

    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

static void BM_NonCrossingAsks(benchmark::State& state)
{
    const auto batch_size =
        static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        state.PauseTiming();
        {
            OrderBook book{1000};
            state.ResumeTiming();

            for (std::size_t i = 0; i < batch_size; ++i) {
                auto trades = book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(i + 1),
                    .price = 100,
                    .quantity = 1,
                    .side = Side::Sell
                });

                KeepResult(trades);
            }
            benchmark::ClobberMemory();
            state.PauseTiming();
        }
        state.ResumeTiming();
    }

    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}



static void BM_PartialBuyAgainstAsk(benchmark::State& state)
{
    const auto batch_size =
        static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        state.PauseTiming();
        {
            OrderBook book{1000};

            book.add_order(Order{
                .order_id = 1,
                .price = 100,
                .quantity = static_cast<std::uint32_t>(batch_size + 1),
                .side = Side::Sell
            });

            state.ResumeTiming();

            for (std::size_t i = 0; i < batch_size; ++i) {
                auto trades = book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(2'000'000 + i),
                    .price = 100,
                    .quantity = 1,
                    .side = Side::Buy
                });

                KeepResult(trades);
            }
            benchmark::ClobberMemory();
            state.PauseTiming();
        }
        state.ResumeTiming();
    }

    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

static void BM_PartialSellAgainstBid(benchmark::State& state)
{
    const auto batch_size =
        static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        state.PauseTiming();
        {
            OrderBook book{1000};

            book.add_order(Order{
                .order_id = 1,
                .price = 100,
                .quantity = static_cast<std::uint32_t>(batch_size + 1),
                .side = Side::Buy
            });

            state.ResumeTiming();

            for (std::size_t i = 0; i < batch_size; ++i) {
                auto trades = book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(2'000'000 + i),
                    .price = 100,
                    .quantity = 1,
                    .side = Side::Sell
                });

                KeepResult(trades);
            }
            benchmark::ClobberMemory();
            state.PauseTiming();
        }
        state.ResumeTiming();
    }

    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}


static void BM_MultiLevel_BuyAgainstAsks(benchmark::State& state)
{
    const auto batch_size =
        static_cast<std::size_t>(state.range(0));
    const auto level_count =
        static_cast<std::size_t>(state.range(1));

    constexpr std::int64_t base_price = 100;

    const auto total_levels = batch_size * level_count;
    const auto max_price =
        base_price + static_cast<std::int64_t>(total_levels);

    for (auto _ : state) {
        state.PauseTiming();
        {
            OrderBook book{max_price};

            // Each consecutive price has one resting sell order.
            for (std::size_t i = 0; i < total_levels; ++i) {
                book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(i + 1),
                    .price = base_price + static_cast<std::int64_t>(i),
                    .quantity = 1,
                    .side = Side::Sell
                });
            }

            // The sentinel prevents a final scan to max_price.
            book.add_order(Order{
                .order_id = 9'000'000,
                .price = max_price,
                .quantity = 1,
                .side = Side::Sell
            });

            state.ResumeTiming();

            for (std::size_t i = 0; i < batch_size; ++i) {
                // Operation i consumes exactly level_count asks.
                const auto limit_price =
                    base_price +
                    static_cast<std::int64_t>((i + 1) * level_count - 1);

                auto trades = book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(10'000'000 + i),
                    .price = limit_price,
                    .quantity = static_cast<std::uint32_t>(level_count),
                    .side = Side::Buy
                });

                KeepResult(trades);
            }
            benchmark::ClobberMemory();
            state.PauseTiming();
        }
        state.ResumeTiming();
    }

    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));

    state.counters["executions_per_second"] = benchmark::Counter(
        static_cast<double>(state.iterations()) *
            static_cast<double>(batch_size) *
            static_cast<double>(level_count),
        benchmark::Counter::kIsRate);
}


static void BM_SparseTraversal_BuyAgainstAsks(benchmark::State& state)
{
    const auto batch_size =
        static_cast<std::size_t>(state.range(0));
    const auto gap =
        static_cast<std::size_t>(state.range(1));

    constexpr std::int64_t base_price = 100;

    const auto last_price =
        base_price +
        static_cast<std::int64_t>(batch_size * gap);

    for (auto _ : state) {
        state.PauseTiming();
        {
            OrderBook book{last_price};

            // One sell order every 'gap' ticks.
            for (std::size_t i = 0; i < batch_size; ++i) {
                book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(i + 1),
                    .price = base_price +
                        static_cast<std::int64_t>(i * gap),
                    .quantity = 1,
                    .side = Side::Sell
                });
            }

            // The next occupied level after the last match.
            book.add_order(Order{
                .order_id = 9'000'000,
                .price = last_price,
                .quantity = 1,
                .side = Side::Sell
            });

            state.ResumeTiming();

            for (std::size_t i = 0; i < batch_size; ++i) {
                const auto price =
                    base_price +
                    static_cast<std::int64_t>(i * gap);

                auto trades = book.add_order(Order{
                    .order_id = static_cast<std::uint64_t>(10'000'000 + i),
                    .price = price,
                    .quantity = 1,
                    .side = Side::Buy
                });

                KeepResult(trades);
            }
            benchmark::ClobberMemory();
            state.PauseTiming();
        }
        state.ResumeTiming();
    }

    state.SetItemsProcessed(
        state.iterations() * static_cast<std::int64_t>(batch_size));
}


static void BM_SeededMixedReplay(benchmark::State& state)
{
    const auto operation_count =
        static_cast<std::size_t>(state.range(0));
    const auto seed =
        static_cast<std::uint32_t>(state.range(1));
    const auto profile =
        static_cast<seeded_replay::WorkloadProfile>(state.range(2));

    // Workload generation and all random choices happen before timing.
    const auto workload =
        seeded_replay::make_workload(operation_count, seed, profile);

    uint64_t total_trade_events = 0;
    uint64_t total_executed_quantity = 0;
    uint64_t total_mismatches = 0;
    uint64_t total_passive_crossings = 0;
    std::array<uint64_t, seeded_replay::kCategoryCount> category_ops{};

    for (auto _ : state) {
        state.PauseTiming();

        seeded_replay::ReplayMetrics metrics{};
        {
            // Recreate deterministic initial state for each replay iteration.
            // Book construction and liquidity seeding are deliberately excluded
            // from the timed replay, but still contribute to benchmark wall time.
            OrderBook primary{seeded_replay::kPrimaryBookMaxPrice};
            OrderBook sparse{workload.sparse_book_max_price};

            seeded_replay::seed_order_books(primary, sparse, workload);

            state.ResumeTiming();
            metrics = seeded_replay::run_workload(primary, sparse, workload);
            state.PauseTiming();
        } // Book destruction is outside the measured region.

        // Aggregate benchmark statistics while timing is paused.
        total_trade_events += metrics.total_trade_events;
        total_executed_quantity += metrics.total_executed_quantity;
        total_mismatches += metrics.trade_count_mismatches;
        total_passive_crossings += metrics.passive_insert_crossings;

        for (std::size_t i = 0; i < category_ops.size(); ++i) {
            category_ops[i] += metrics.categories[i].operation_count;
        }

        // The metrics are consumed after timing; no trade result is discarded.
        benchmark::DoNotOptimize(metrics.total_trade_events);
        benchmark::DoNotOptimize(metrics.total_executed_quantity);

        state.ResumeTiming();
    }

    const auto iterations = static_cast<uint64_t>(state.iterations());
    const auto total_api_calls = iterations *
        static_cast<uint64_t>(operation_count);

    state.SetItemsProcessed(static_cast<int64_t>(total_api_calls));

    state.counters["trade_events_per_second"] = benchmark::Counter(
        static_cast<double>(total_trade_events),
        benchmark::Counter::kIsRate);

    state.counters["executed_quantity_per_second"] = benchmark::Counter(
        static_cast<double>(total_executed_quantity),
        benchmark::Counter::kIsRate);

    state.counters["passive_inserts_per_second"] = benchmark::Counter(
        static_cast<double>(category_ops[seeded_replay::category_index(
            seeded_replay::OperationKind::PassiveInsert)]),
        benchmark::Counter::kIsRate);

    state.counters["single_level_match_ops_per_second"] = benchmark::Counter(
        static_cast<double>(category_ops[seeded_replay::category_index(
            seeded_replay::OperationKind::SingleLevelMatch)]),
        benchmark::Counter::kIsRate);

    state.counters["sweep_ops_per_second"] = benchmark::Counter(
        static_cast<double>(category_ops[seeded_replay::category_index(
            seeded_replay::OperationKind::MultiLevelSweep)]),
        benchmark::Counter::kIsRate);

    state.counters["sparse_ops_per_second"] = benchmark::Counter(
        static_cast<double>(category_ops[seeded_replay::category_index(
            seeded_replay::OperationKind::SparseLevel)]),
        benchmark::Counter::kIsRate);

    // Average diagnostic counts per replay; nonzero indicates a workload
    // assumption or matching behavior that needs investigation.
    state.counters["trade_count_mismatches_per_replay"] =
        benchmark::Counter(
            static_cast<double>(total_mismatches) /
            static_cast<double>(iterations));

    state.counters["passive_insert_crossings_per_replay"] =
        benchmark::Counter(
            static_cast<double>(total_passive_crossings) /
            static_cast<double>(iterations));

    state.counters["trade_events_per_api_call"] =
        benchmark::Counter(
            static_cast<double>(total_trade_events) /
            static_cast<double>(total_api_calls));

    // Plain metadata counters identify mix and seed in CSV output.
    state.counters["workload_profile_id"] =
        static_cast<double>(profile);
    state.counters["workload_seed"] =
        static_cast<double>(seed);
}

static void BM_MixedWorkload(benchmark::State& state)
{
    const auto batch_size =
        static_cast<std::size_t>(state.range(0));

    for (auto _ : state) {
        state.PauseTiming();
        {
            OrderBook book{1000};

            // Sentinels bound best-price scans.
            book.add_order(Order{
                .order_id = 1,
                .price = 498,
                .quantity = 1,
                .side = Side::Buy
            });

            book.add_order(Order{
                .order_id = 2,
                .price = 502,
                .quantity = 1,
                .side = Side::Sell
            });

            state.ResumeTiming();

            for (std::size_t i = 0; i < batch_size; ++i) {
                const auto id =
                    static_cast<std::uint64_t>(100 + i * 4);

                // 1. Non-crossing buy at 499.
                auto buy_insert = book.add_order(Order{
                    .order_id = id,
                    .price = 499,
                    .quantity = 1,
                    .side = Side::Buy
                });
                KeepResult(buy_insert);

                // 2. Non-crossing sell at 501.
                auto sell_insert = book.add_order(Order{
                    .order_id = id + 1,
                    .price = 501,
                    .quantity = 1,
                    .side = Side::Sell
                });
                KeepResult(sell_insert);

                // 3. Incoming buy consumes the sell at 501.
                auto buy_match = book.add_order(Order{
                    .order_id = id + 2,
                    .price = 501,
                    .quantity = 1,
                    .side = Side::Buy
                });
                KeepResult(buy_match);

                // 4. Incoming sell consumes the buy at 499.
                auto sell_match = book.add_order(Order{
                    .order_id = id + 3,
                    .price = 499,
                    .quantity = 1,
                    .side = Side::Sell
                });
                KeepResult(sell_match);
            }
            benchmark::ClobberMemory();
            state.PauseTiming();
        }
        state.ResumeTiming();
    }

    const auto batches = static_cast<double>(state.iterations());
    const auto cycles = static_cast<double>(batch_size);

    // One cycle consists of four incoming API calls.
    state.SetItemsProcessed(
        state.iterations() *
        static_cast<std::int64_t>(batch_size) * 4);

    // Two trades are generated per cycle.
    state.counters["trades_per_second"] = benchmark::Counter(
        batches * cycles * 2.0,
        benchmark::Counter::kIsRate);
}





BENCHMARK(BM_SingleLevel_BuyAgainstAsk)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096);

BENCHMARK(BM_SingleLevel_SellAgainstBid)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096);

BENCHMARK(BM_NonCrossingAsks)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096);

BENCHMARK(BM_NonCrossingBids)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096);

BENCHMARK(BM_PartialBuyAgainstAsk)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096);

BENCHMARK(BM_PartialSellAgainstBid)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096);

BENCHMARK(BM_BookLifecycle)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000);

BENCHMARK(BM_MultiLevel_BuyAgainstAsks)
    ->Args({16, 16})
    ->Args({64, 16})
    ->Args({256, 16})
    ->Args({16, 64})
    ->Args({64, 64})
    ->Args({16, 256})
    ->MinTime(0.5);

BENCHMARK(BM_SparseTraversal_BuyAgainstAsks)
    ->Args({64, 1})
    ->Args({64, 16})
    ->Args({64, 64})
    ->Args({64, 256})
    ->MinTime(0.5);

BENCHMARK(BM_MixedWorkload)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096);

BENCHMARK(BM_SeededMixedReplay)
    // Balanced: 45% insert, 25% single-level match, 20% sweep, 10% sparse.
    ->Args({4000, 24301, 0})      // Balanced, seed 0x5EED.
    ->Args({4000, 12648430, 0})   // Balanced, alternate seed 0xC0FFEE.
    // Hold size and seed constant while shifting operation mix.
    ->Args({4000, 24301, 1})      // Insertion-heavy: 60/25/10/5.
    ->Args({4000, 24301, 2})      // Matching-heavy: 25/40/25/10.
    // Scaling check; book depth changes with operation count in current fixture.
    ->Args({8000, 24301, 0})
    ->MinTime(0.5);

BENCHMARK_MAIN();
