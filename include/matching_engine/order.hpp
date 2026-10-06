#pragma once

#include <cstdint>

namespace matching_engine {

enum class Side : uint8_t {
    Buy,
    Sell
};

struct Order {
    uint64_t order_id;
    int64_t price;
    uint32_t quantity;
    Side side;
};

} // namespace matching_engine