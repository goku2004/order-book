#pragma once

#include "matching_engine/order_book.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace seeded_replay {

// The primary ladder is centered at 20,000. The supported replay sizes are
// chosen so two times the operation count of liquidity fits on each side.
inline constexpr int64_t kPrimaryBookMaxPrice = 40'000;
inline constexpr int64_t kPrimaryAnchorPrice = 20'000;
inline constexpr std::size_t kMaxOperationCount = 8'000;
inline constexpr std::size_t kCategoryCount = 4;

enum class OperationKind : uint8_t {
    PassiveInsert = 0,
    SingleLevelMatch = 1,
    MultiLevelSweep = 2,
    SparseLevel = 3
};

enum class WorkloadProfile : uint8_t {
    Balanced = 0,       // 45% passive, 25% single-level, 20% sweeps, 10% sparse
    InsertionHeavy = 1, // 60% passive, 25% single-level, 10% sweeps, 5% sparse
    MatchingHeavy = 2   // 25% passive, 40% single-level, 25% sweeps, 10% sparse
};

struct OperationSpec {
    OperationKind kind{};
    matching_engine::Side side{matching_engine::Side::Buy};

    // Passive insertion price or expected sparse ask price. For top-of-book
    // matches and sweeps, the limit price is derived from the live book state.
    int64_t price{};

    uint32_t quantity{1};
    uint32_t level_count{1};
    uint32_t gap{1};
};

struct Workload {
    uint32_t seed{};
    WorkloadProfile profile{WorkloadProfile::Balanced};
    std::vector<OperationSpec> operations;

    // The separate sparse book holds one ask for each sparse operation and
    // one additional sentinel ask after the final measured sparse operation.
    int64_t sparse_ask_sentinel{};
    int64_t sparse_book_max_price{};
};

struct CategoryMetrics {
    uint64_t operation_count{};
    uint64_t trade_events{};
    uint64_t executed_quantity{};
};

struct ReplayMetrics {
    std::array<CategoryMetrics, kCategoryCount> categories{};
    uint64_t total_trade_events{};
    uint64_t total_executed_quantity{};

    // Nonzero values indicate that the generated workload did not behave as
    // intended. Passive insertion must not cross; other operation classes
    // have known expected execution counts for this seeded fixture.
    uint64_t trade_count_mismatches{};
    uint64_t passive_insert_crossings{};
};

constexpr std::size_t category_index(OperationKind kind) noexcept
{
    return static_cast<std::size_t>(kind);
}

// Creates exact proportions for the selected profile. operation_count must be a
// positive multiple of 20 and no greater than kMaxOperationCount. The fixed seed
// determines operation order, side choices, quantities, sweep sizes, and sparse gaps.
Workload make_workload(
    std::size_t operation_count,
    uint32_t seed,
    WorkloadProfile profile = WorkloadProfile::Balanced);

// Seeds a dense primary book and a separate sparse book. Call before timing.
void seed_order_books(
    matching_engine::OrderBook& primary,
    matching_engine::OrderBook& sparse,
    const Workload& workload);

// Executes one add_order call per operation. The existing vector<Trade> API is
// intentionally used for this baseline. Trade results are observed and summarized.
ReplayMetrics run_workload(
    matching_engine::OrderBook& primary,
    matching_engine::OrderBook& sparse,
    const Workload& workload);

} // namespace seeded_replay
