#include <gtest/gtest.h>

#include "matching_engine/order.hpp"

using matching_engine::Order;
using matching_engine::Side;

TEST(OrderTest, CanConstructBuyOrder)
{
    const Order order{
        .order_id = 1,
        .price = 10025,
        .quantity = 500,
        .side = Side::Buy
    };

    EXPECT_EQ(order.order_id, 1);
    EXPECT_EQ(order.price, 10025);
    EXPECT_EQ(order.quantity, 500);
    EXPECT_EQ(order.side, Side::Buy);
}