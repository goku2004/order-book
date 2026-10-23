#include "matching_engine/order_book.hpp"
#include "matching_engine/order.hpp"
#include <vector>
#include <queue>
#include <algorithm>
#include <optional>
#include <cstdint>
#include <stdexcept>
#include <limits>
namespace matching_engine {

// Domain-level order functionality will be added here
// as the model becomes more sophisticated.

inline std::size_t checked_level_count(std::int64_t max_price)
{
    if (max_price < 0) {
        throw std::invalid_argument("max_price must be non-negative");
    }

    // Check that the requested number of levels is representable.
    const auto price = static_cast<std::uint64_t>(max_price);

    if (price >= std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("max_price is too large");
    }

    const auto count = static_cast<std::size_t>(price + 1);

    // Reject counts that the vector implementation cannot support.
    if (count > std::vector<std::queue<Order>>{}.max_size()) {
        throw std::length_error("too many price levels");
    }

    return count;
}


OrderBook::OrderBook(int64_t max_price)
    : max_price_(max_price), 
      buy_levels_(checked_level_count(max_price)), 
      sell_levels_(checked_level_count(max_price)) {
}

std::optional<int64_t> OrderBook :: best_bid() const noexcept{
    return best_bid_;
}
std::optional<int64_t> OrderBook :: best_ask() const noexcept{
    return best_ask_;
}
//ORDER ADDING ALGO
// ---------------------------------------//
std::vector<Trade> OrderBook :: add_order(Order order){
    if(0 > order.price || order.price > max_price_){
        throw std::invalid_argument("Invalid_order price");
    }
    if(0 == order.quantity){
        throw std::invalid_argument("Invalid order quantity");
    }
    std::vector<Trade>trades;
    match(order, trades);
    if(order.quantity > 0){
        if(order.side == Side::Buy){
            buy_levels_[order.price].push(order);
            if(best_bid_){
                best_bid_ = std::max(*best_bid_, order.price);
            }
            else{
                best_bid_ = order.price;
            }
        }
        else{
            sell_levels_[order.price].push(order);
            if(best_ask_){
                best_ask_ = std::min(*best_ask_, order.price);
            }
            else{
                best_ask_ = order.price;
            }
        }
    }
    return trades;
}
//MATCHING ALGO
// -------------------------------------------------------//
void OrderBook :: match(Order& order, std::vector<Trade>& trades){
    if(order.side == Side::Buy){
        while(best_ask_ && *best_ask_ <= order.price && order.quantity > 0){
            while(!sell_levels_[*best_ask_].empty() && order.quantity > 0){
                Order& ask = sell_levels_[*best_ask_].front();
                uint32_t traded = std::min(order.quantity, ask.quantity);
                trades.push_back(Trade{order.order_id, ask.order_id, ask.price, traded});
                order.quantity -= traded;
                ask.quantity -= traded;
                if(ask.quantity == 0){
                    sell_levels_[*best_ask_].pop();
                }
            }
            int64_t curr = *best_ask_;
            while(curr <= max_price_ && sell_levels_[curr].empty()){
                curr++;
            }
            if(curr > max_price_){
                best_ask_ = std::nullopt;
            }
            else{
                best_ask_ = curr;
            }
        } 
    }
    else{
        while(best_bid_ && *best_bid_ >= order.price && order.quantity > 0){
            while(!buy_levels_[*best_bid_].empty() && order.quantity > 0){
                Order& bid = buy_levels_[*best_bid_].front();
                uint32_t traded = std::min(order.quantity, bid.quantity);
                trades.push_back(Trade{bid.order_id, order.order_id, bid.price, traded});
                order.quantity -= traded;
                bid.quantity -= traded;
                if(bid.quantity == 0){
                    buy_levels_[*best_bid_].pop();
                }
            }
            int64_t curr = *best_bid_;
            while(curr >= 0 && buy_levels_[curr].empty()){
                curr--;
            }
            if(curr < 0){
                best_bid_ = std::nullopt;
            }
            else{
                best_bid_ = curr;
            }
        } 
    }
}
} // namespace matching_engine