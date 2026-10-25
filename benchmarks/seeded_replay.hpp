#pragma once

#include "matching_engine/order_book.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace seeded_replay {

inline constexpr int64_t kPrimaryBookMaxPrice = 200'000;
inline constexpr std::size_t kCategoryCount = 4;

enum class OperationKind : uint8_t {
    PassiveInsert = 0,
    TopOfBookMatch = 1,
    MultiLevelSweep = 2,
    SparseLevel = 3
};

struct OperationSpec {
    OperationKind kind{};
    matching_engine::Side side{matching_engine::Side::Buy};

    // Passive insertion price or the target sparse-level price. For top-of-book
    // and sweep events, the relevant limit price is derived from the live book.
    int64_t price{};

    uint32_t quantity{1};
    uint32_t level_count{1};
    uint32_t gap{1};
};

struct Workload {
    uint32_t seed{};
    std::vector<OperationSpec> operations;

    // Sparse-book sentinel levels are left after all corresponding sparse
    // operations have consumed their preloaded level.
    int64_t sparse_ask_sentinel{};
    int64_t sparse_bid_sentinel{};
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

    // These must both remain zero for the generated workload.
    uint64_t missing_expected_trades{};
    uint64_t passive_orders_that_traded{};
};

constexpr std::size_t category_index(OperationKind kind) noexcept
{
    return static_cast<std::size_t>(kind);
}

// Creates exact 45/25/20/10 category proportions for operation counts
// divisible by 20. For other counts, rounding remainder is assigned to SparseLevel.
Workload make_workload(std::size_t operation_count, uint32_t seed);

// Seeds a dense primary book and a separate sparse book outside the timed region.
void seed_order_books(
    matching_engine::OrderBook& primary,
    matching_engine::OrderBook& sparse,
    const Workload& workload);

// Executes exactly one add_order call per workload specification. The caller
// decides which region is timed. This currently consumes the vector<Trade>
// returned by the existing public API.
ReplayMetrics run_workload(
    matching_engine::OrderBook& primary,
    matching_engine::OrderBook& sparse,
    const Workload& workload);

} // namespace seeded_replay
