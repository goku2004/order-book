#include <benchmark/benchmark.h>

#include "matching_engine/order.hpp"

using matching_engine::Order;
using matching_engine::Side;

static void BM_CreateOrder(benchmark::State& state)
{
    for (auto _ : state) {
        Order order{
            .order_id = 1,
            .price = 10025,
            .quantity = 500,
            .side = Side::Buy
        };

        // Prevent the compiler from optimizing the object away.
        benchmark::DoNotOptimize(order);
    }
}

BENCHMARK(BM_CreateOrder);

BENCHMARK_MAIN();