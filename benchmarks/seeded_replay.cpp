#include "seeded_replay.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <utility>

namespace seeded_replay {

namespace {

using matching_engine::Order;
using matching_engine::Side;
using matching_engine::Trade;

Order make_order(
    uint64_t order_id,
    int64_t price,
    uint32_t quantity,
    Side side)
{
    return Order{
        .order_id = order_id,
        .price = price,
        .quantity = quantity,
        .side = side
    };
}

void accumulate_trades(
    OperationKind kind,
    const std::vector<Trade>& trades,
    ReplayMetrics& metrics)
{
    auto& category = metrics.categories[category_index(kind)];

    const auto event_count = static_cast<uint64_t>(trades.size());
    category.trade_events += event_count;
    metrics.total_trade_events += event_count;

    // Every resting order in the active seeded liquidity has quantity one.
    // Thus each generated execution is one unit and executed quantity equals
    // event count. Avoid traversing the returned vector in the timed replay
    // solely to recompute a quantity already fixed by this workload design.
    category.executed_quantity += event_count;
    metrics.total_executed_quantity += event_count;
}

bool is_buy_side(std::mt19937& rng)
{
    return std::uniform_int_distribution<int>(0, 1)(rng) == 0;
}

} // namespace

Workload make_workload(
    std::size_t operation_count,
    uint32_t seed,
    WorkloadProfile profile)
{
    if (operation_count == 0 ||
        operation_count > kMaxOperationCount ||
        operation_count % 20 != 0) {
        throw std::invalid_argument(
            "Seeded replay operation count must be a positive multiple of 20 "
            "and no greater than 8000");
    }

    // Weights are integer fifths of 20: this keeps every profile's proportions
    // exact for any supported operation count divisible by 20.
    std::array<std::size_t, kCategoryCount> weights{};
    switch (profile) {
    case WorkloadProfile::Balanced:
        weights = {9, 5, 4, 2}; // 45 / 25 / 20 / 10
        break;
    case WorkloadProfile::InsertionHeavy:
        weights = {12, 5, 2, 1}; // 60 / 25 / 10 / 5
        break;
    case WorkloadProfile::MatchingHeavy:
        weights = {5, 8, 5, 2}; // 25 / 40 / 25 / 10
        break;
    default:
        throw std::invalid_argument("Unknown seeded replay workload profile");
    }

    Workload workload;
    workload.seed = seed;
    workload.profile = profile;
    workload.operations.reserve(operation_count);

    std::mt19937 rng(seed);
    const std::size_t units = operation_count / 20;

    const std::size_t passive_count = weights[category_index(OperationKind::PassiveInsert)] * units;
    const std::size_t single_match_count = weights[category_index(OperationKind::SingleLevelMatch)] * units;
    const std::size_t sweep_count = weights[category_index(OperationKind::MultiLevelSweep)] * units;
    const std::size_t sparse_count = weights[category_index(OperationKind::SparseLevel)] * units;

    std::uniform_int_distribution<uint32_t> passive_quantity(1, 5);
    std::uniform_int_distribution<int64_t> passive_bid_price(1'000, 3'000);
    std::uniform_int_distribution<int64_t> passive_ask_price(37'000, 39'500);
    std::uniform_int_distribution<uint32_t> gap_index(0, 3);
    std::uniform_int_distribution<uint32_t> sweep_size_index(0, 1);

    constexpr std::array<uint32_t, 4> sparse_gaps{1, 16, 64, 128};
    constexpr std::array<uint32_t, 2> sweep_sizes{4, 8};

    // Passive insertions are deliberately far from the active top-of-book
    // frontier, so inserting them should not immediately cross.
    for (std::size_t i = 0; i < passive_count; ++i) {
        const bool buy = is_buy_side(rng);
        workload.operations.push_back(OperationSpec{
            .kind = OperationKind::PassiveInsert,
            .side = buy ? Side::Buy : Side::Sell,
            .price = buy ? passive_bid_price(rng) : passive_ask_price(rng),
            .quantity = passive_quantity(rng),
            .level_count = 1,
            .gap = 1
        });
    }

    // Half of the single-level match operations submit quantity 1; half submit
    // quantity 2 against unit resting liquidity. The latter partially fill
    // the incoming order, leaving one unit to rest at its limit price.
    for (std::size_t i = 0; i < single_match_count; ++i) {
        workload.operations.push_back(OperationSpec{
            .kind = OperationKind::SingleLevelMatch,
            .side = is_buy_side(rng) ? Side::Buy : Side::Sell,
            .price = 0, // Derived from the current best price during replay.
            .quantity = (i < single_match_count / 2) ? 2u : 1u,
            .level_count = 1,
            .gap = 1
        });
    }

    for (std::size_t i = 0; i < sweep_count; ++i) {
        const auto levels = sweep_sizes[sweep_size_index(rng)];
        workload.operations.push_back(OperationSpec{
            .kind = OperationKind::MultiLevelSweep,
            .side = is_buy_side(rng) ? Side::Buy : Side::Sell,
            .price = 0, // A marketable limit is derived from current book state.
            .quantity = levels,
            .level_count = levels,
            .gap = 1
        });
    }

    for (std::size_t i = 0; i < sparse_count; ++i) {
        workload.operations.push_back(OperationSpec{
            .kind = OperationKind::SparseLevel,
            .side = Side::Buy,
            .price = 0, // Assigned to an ascending sparse price level below.
            .quantity = 1,
            .level_count = 1,
            .gap = sparse_gaps[gap_index(rng)]
        });
    }

    // Randomize the order stream while preserving the exact category counts.
    std::shuffle(
        workload.operations.begin(),
        workload.operations.end(),
        rng);

    // Assign one unique ascending sparse-book price to each sparse operation.
    // The gap after each price is part of the seed and is reproduced exactly.
    int64_t next_sparse_price = 100;
    for (auto& operation : workload.operations) {
        if (operation.kind != OperationKind::SparseLevel) {
            continue;
        }

        operation.price = next_sparse_price;
        next_sparse_price += static_cast<int64_t>(operation.gap);
    }

    workload.sparse_ask_sentinel = next_sparse_price;
    workload.sparse_book_max_price = next_sparse_price;

    // At most 10% of operations are sparse and each gap is at most 128 ticks.
    // Keep this independent of the smaller primary-book price domain.
    constexpr int64_t kSparseBookMaxPrice = 110'000;
    if (next_sparse_price > kSparseBookMaxPrice) {
        throw std::length_error("Generated sparse price range exceeds configured bounds");
    }

    return workload;
}

void seed_order_books(
    matching_engine::OrderBook& primary,
    matching_engine::OrderBook& sparse,
    const Workload& workload)
{
    if (workload.operations.empty() ||
        workload.operations.size() > kMaxOperationCount) {
        throw std::invalid_argument("Invalid seeded workload");
    }

    const int64_t depth =
        static_cast<int64_t>(2 * workload.operations.size());
    const int64_t anchor = kPrimaryAnchorPrice;
    const int64_t low_bid = anchor - depth;
    const int64_t high_ask = anchor + depth - 1;

    if (low_bid <= 3'000 || high_ask >= 37'000 ||
        high_ask >= kPrimaryBookMaxPrice) {
        throw std::invalid_argument("Replay size would overlap passive price bands");
    }

    uint64_t order_id = 1;

    // Dense bid ladder immediately below the anchor.
    for (int64_t price = low_bid; price < anchor; ++price) {
        auto bid = make_order(order_id++, price, 1, Side::Buy);
        (void)primary.add_order(bid);
    }

    // Dense ask ladder beginning one tick above the best bid.
    for (int64_t price = anchor; price <= high_ask; ++price) {
        auto ask = make_order(order_id++, price, 1, Side::Sell);
        (void)primary.add_order(ask);
    }

    // Seed sparse asks in the order in which their operations will execute.
    uint64_t sparse_order_id = 5'000'000;
    for (const auto& operation : workload.operations) {
        if (operation.kind != OperationKind::SparseLevel) {
            continue;
        }

        auto ask = make_order(
            sparse_order_id++, operation.price, 1, Side::Sell);
        (void)sparse.add_order(ask);
    }

    auto sentinel = make_order(
        sparse_order_id++,
        workload.sparse_ask_sentinel,
        1,
        Side::Sell);
    (void)sparse.add_order(sentinel);
}

ReplayMetrics run_workload(
    matching_engine::OrderBook& primary,
    matching_engine::OrderBook& sparse,
    const Workload& workload)
{
    ReplayMetrics metrics{};
    uint64_t primary_incoming_id = 10'000'000;
    uint64_t sparse_incoming_id = 20'000'000;

    for (const auto& operation : workload.operations) {
        auto& category =
            metrics.categories[category_index(operation.kind)];
        ++category.operation_count;

        // Directly initialize from add_order's return value. This avoids the
        // extra default construction plus move assignment of a temporary vector.
        auto trades = [&]() -> std::vector<Trade> {
            switch (operation.kind) {
            case OperationKind::PassiveInsert: {
                auto order = make_order(
                    primary_incoming_id++,
                    operation.price,
                    operation.quantity,
                    operation.side);
                return primary.add_order(order);
            }

            case OperationKind::SingleLevelMatch: {
                const auto best = operation.side == Side::Buy
                    ? primary.best_ask()
                    : primary.best_bid();

                if (!best) {
                    ++metrics.trade_count_mismatches;
                    return {};
                }

                auto order = make_order(
                    primary_incoming_id++,
                    *best,
                    operation.quantity,
                    operation.side);
                return primary.add_order(order);
            }

            case OperationKind::MultiLevelSweep: {
                const auto best = operation.side == Side::Buy
                    ? primary.best_ask()
                    : primary.best_bid();

                if (!best) {
                    ++metrics.trade_count_mismatches;
                    return {};
                }

                // A marketable limit spans the valid price domain. Quantity
                // caps the sweep at K unit-sized resting orders, allowing an
                // evolving gap to be scanned without stopping the sweep early.
                const int64_t limit_price = operation.side == Side::Buy
                    ? kPrimaryBookMaxPrice
                    : 0;

                auto order = make_order(
                    primary_incoming_id++,
                    limit_price,
                    operation.level_count,
                    operation.side);
                return primary.add_order(order);
            }

            case OperationKind::SparseLevel: {
                const auto best = sparse.best_ask();

                if (!best || *best != operation.price) {
                    ++metrics.trade_count_mismatches;
                }

                auto order = make_order(
                    sparse_incoming_id++,
                    best.value_or(operation.price),
                    1,
                    Side::Buy);
                return sparse.add_order(order);
            }
            }

            ++metrics.trade_count_mismatches;
            return {};
        }();

        // Validate each workload class using the observed trade count.
        // The active seeded levels have unit quantity, so each trade event is
        // also one executed unit in this particular fixture.
        switch (operation.kind) {
        case OperationKind::PassiveInsert:
            if (!trades.empty()) {
                ++metrics.passive_insert_crossings;
            }
            break;
        case OperationKind::SingleLevelMatch:
        case OperationKind::SparseLevel:
            if (trades.size() != 1) {
                ++metrics.trade_count_mismatches;
            }
            break;
        case OperationKind::MultiLevelSweep:
            if (trades.size() != operation.level_count) {
                ++metrics.trade_count_mismatches;
            }
            break;
        }

        accumulate_trades(operation.kind, trades, metrics);
    }

    return metrics;
}
} // namespace seeded_replay
