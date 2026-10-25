#include <gtest/gtest.h>

#include "seeded_replay.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

using matching_engine::OrderBook;
using seeded_replay::OperationKind;
using seeded_replay::Workload;

namespace {

std::array<std::size_t, seeded_replay::kCategoryCount> count_categories(
    const Workload& workload)
{
    std::array<std::size_t, seeded_replay::kCategoryCount> counts{};
    for (const auto& operation : workload.operations) {
        ++counts[seeded_replay::category_index(operation.kind)];
    }
    return counts;
}

TEST(SeededReplayTest, RejectsInvalidOperationCounts)
{
    EXPECT_THROW(seeded_replay::make_workload(0, 1), std::invalid_argument);
    EXPECT_THROW(seeded_replay::make_workload(101, 1), std::invalid_argument);
    EXPECT_THROW(
        seeded_replay::make_workload(
            seeded_replay::kMaxOperationCount + 20, 1),
        std::invalid_argument);
}

TEST(SeededReplayTest, PreservesExactOperationDistribution)
{
    constexpr std::size_t operation_count = 4'000;
    const auto workload = seeded_replay::make_workload(operation_count, 0x5EEDu);
    const auto counts = count_categories(workload);

    EXPECT_EQ(counts[seeded_replay::category_index(OperationKind::PassiveInsert)], 1'800);
    EXPECT_EQ(counts[seeded_replay::category_index(OperationKind::TopOfBookMatch)], 1'000);
    EXPECT_EQ(counts[seeded_replay::category_index(OperationKind::MultiLevelSweep)], 800);
    EXPECT_EQ(counts[seeded_replay::category_index(OperationKind::SparseLevel)], 400);
}

TEST(SeededReplayTest, SameSeedProducesSameOperations)
{
    const auto first = seeded_replay::make_workload(4'000, 0xC0FFEEu);
    const auto second = seeded_replay::make_workload(4'000, 0xC0FFEEu);

    ASSERT_EQ(first.operations.size(), second.operations.size());
    EXPECT_EQ(first.sparse_ask_sentinel, second.sparse_ask_sentinel);
    EXPECT_EQ(first.sparse_book_max_price, second.sparse_book_max_price);

    for (std::size_t i = 0; i < first.operations.size(); ++i) {
        EXPECT_EQ(first.operations[i].kind, second.operations[i].kind);
        EXPECT_EQ(first.operations[i].side, second.operations[i].side);
        EXPECT_EQ(first.operations[i].price, second.operations[i].price);
        EXPECT_EQ(first.operations[i].quantity, second.operations[i].quantity);
        EXPECT_EQ(first.operations[i].level_count, second.operations[i].level_count);
        EXPECT_EQ(first.operations[i].gap, second.operations[i].gap);
    }
}

TEST(SeededReplayTest, ReplayMaintainsWorkloadAssumptions)
{
    constexpr std::size_t operation_count = 4'000;
    const auto workload = seeded_replay::make_workload(operation_count, 0x5EEDu);

    OrderBook primary{seeded_replay::kPrimaryBookMaxPrice};
    OrderBook sparse{workload.sparse_book_max_price};
    seeded_replay::seed_order_books(primary, sparse, workload);

    const auto metrics = seeded_replay::run_workload(primary, sparse, workload);

    EXPECT_EQ(
        metrics.categories[seeded_replay::category_index(OperationKind::PassiveInsert)]
            .operation_count,
        1'800u);
    EXPECT_EQ(
        metrics.categories[seeded_replay::category_index(OperationKind::TopOfBookMatch)]
            .operation_count,
        1'000u);
    EXPECT_EQ(
        metrics.categories[seeded_replay::category_index(OperationKind::MultiLevelSweep)]
            .operation_count,
        800u);
    EXPECT_EQ(
        metrics.categories[seeded_replay::category_index(OperationKind::SparseLevel)]
            .operation_count,
        400u);

    EXPECT_EQ(metrics.trade_count_mismatches, 0u);
    EXPECT_EQ(metrics.passive_insert_crossings, 0u);
    EXPECT_GT(metrics.total_trade_events, 0u);
    EXPECT_GT(metrics.total_executed_quantity, 0u);

    // The sparse category consumes exactly one seeded ask for every operation.
    EXPECT_EQ(
        metrics.categories[seeded_replay::category_index(OperationKind::SparseLevel)]
            .trade_events,
        400u);
}

} // namespace
