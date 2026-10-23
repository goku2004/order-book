#pragma once

#include "matching_engine/order.hpp"

#include <cstdint>
#include <optional>
#include <queue>
#include <vector>

namespace matching_engine {

// One record represents one execution between a buyer and a seller.
// Keeping executions as data makes them testable without console I/O.
struct Trade {
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    int64_t price;
    uint32_t quantity;
};

class OrderBook {
public:
    // max_price defines the inclusive upper bound of the price ladder.
    explicit OrderBook(int64_t max_price);

    // The book receives its own order copy and returns executions
    // generated while processing that order.
    std::vector<Trade> add_order(Order order);

    // Expose best-price state without exposing internal price levels.
    std::optional<int64_t> best_bid() const noexcept;
    std::optional<int64_t> best_ask() const noexcept;

private:
    // Dense price ladder: price tick -> FIFO queue of resting orders.
    std::vector<std::queue<Order>> buy_levels_;
    std::vector<std::queue<Order>> sell_levels_;

    int64_t max_price_;

    // Empty means there are no resting orders on that side.
    std::optional<int64_t> best_bid_;
    std::optional<int64_t> best_ask_;

    // Modifies the incoming order owned by add_order().
    // Appends executions instead of printing them.
    void match(Order& incoming, std::vector<Trade>& trades);
};

} // namespace matching_engine
