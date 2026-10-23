#include <cstddef>
#include <benchmark/benchmark.h>

#include "matching_engine/order.hpp"
#include "matching_engine/order_book.hpp"

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

BENCHMARK_MAIN();
