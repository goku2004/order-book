#include "matching_engine/order.hpp"
#include <vector>
#include <queue>
#include <algorithm>
#include <optional>
namespace matching_engine {

// Domain-level order functionality will be added here
// as the model becomes more sophisticated.
class OrderBook{
    private:
    std::vector<std::queue<Order>>buy_levels;
    std::vector<std::queue<Order>>sell_levels;
    int64_t max_price;
    std::optional<int64_t> best_bid;
    std::optional<int64_t> best_ask;
    public:
    OrderBook(int64_t max_price): max_price(max_price), buy_levels(max_price + 1), sell_levels(max_price + 1){}
    void add_order(Order& order){
        match(order);
        if(order.quantity > 0){
            if(order.side == Side::Buy){
                buy_levels[order.price].push(order);
                best_bid = std::max(*best_bid, order.price);
            }
            else{
                sell_levels[order.price].push(order);
                best_ask = std::min(*best_ask, order.price);
            }
        }
        
    }
    void match(Order& order){
        if(order.side == Side::Buy){
            while(best_ask && *best_ask <= order.price && order.quantity > 0){
                while(!sell_levels[*best_ask].empty() && order.quantity > 0){
                    Order& ask = sell_levels[*best_ask].front();
                    uint32_t traded = std::min(order.quantity, ask.quantity);
                    order.quantity -= traded;
                    ask.quantity -= traded;
                    if(ask.quantity == 0){
                        sell_levels[*best_ask].pop();
                    }
                }
                int64_t curr = *best_ask;
                while(curr < max_price && sell_levels[curr].empty()){
                    curr++;
                }
                best_ask = curr;
            } 
        }
        else{
            while(best_bid && *best_bid >= order.price && order.quantity > 0){
                while(!buy_levels[*best_bid].empty() && order.quantity > 0){
                    Order& bid = buy_levels[*best_bid].front();
                    uint32_t traded = std::min(order.quantity, bid.quantity);
                    order.quantity -= traded;
                    bid.quantity -= traded;
                    if(bid.quantity == 0){
                        buy_levels[*best_bid].pop();
                    }
                }
                int64_t curr = *best_bid;
                while(curr >= 0 && buy_levels[curr].empty()){
                    curr--;
                }
                if(curr < 0){
                    best_bid = std::nullopt;
                }
                else{
                    best_bid = curr;
                }
            } 
        }
    }

};

} // namespace matching_engine