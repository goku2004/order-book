#include <gtest/gtest.h>

#include "matching_engine/order_book.hpp"
#include <stdexcept>

using matching_engine::Order;
using matching_engine::OrderBook;
using matching_engine::Side;

TEST(OrderBookTest, FirstBuyInitializesBestBid)
{
    OrderBook book{1000};

    const auto trades = book.add_order(Order{
        .order_id = 1,
        .price = 100,
        .quantity = 10,
        .side = Side::Buy
    });

    EXPECT_TRUE(trades.empty());
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), 100);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookTest, NonCrossingOrdersRemainOnBothSides)
{
    OrderBook book{1000};

    const auto buy_trades = book.add_order(Order{
        .order_id = 1,
        .price = 100,
        .quantity = 10,
        .side = Side::Buy
    });

    const auto sell_trades = book.add_order(Order{
        .order_id = 2,
        .price = 105,
        .quantity = 7,
        .side = Side::Sell
    });

    EXPECT_TRUE(buy_trades.empty());
    EXPECT_TRUE(sell_trades.empty());

    ASSERT_TRUE(book.best_bid().has_value());
    ASSERT_TRUE(book.best_ask().has_value());

    EXPECT_EQ(*book.best_bid(), 100);
    EXPECT_EQ(*book.best_ask(), 105);
}

TEST(OrderBookTest, CrossingBuyExecutesAtRestingAskPrice)
{
    OrderBook book{1000};

    book.add_order(Order{
        .order_id = 10,
        .price = 100,
        .quantity = 5,
        .side = Side::Sell
    });

    const auto trades = book.add_order(Order{
        .order_id = 20,
        .price = 105,
        .quantity = 5,
        .side = Side::Buy
    });

    ASSERT_EQ(trades.size(), 1);

    EXPECT_EQ(trades[0].buy_order_id, 20);
    EXPECT_EQ(trades[0].sell_order_id, 10);
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].quantity, 5);

    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}
TEST(OrderBookTest, TradesExecutingByFIFOAndPartialOrdersRemain)
{
    OrderBook book{1000};

    book.add_order(Order{
        .order_id = 10,
        .price = 12,
        .quantity = 2,
        .side = Side::Sell
    });
    book.add_order(Order{
        .order_id = 11,
        .price = 12,
        .quantity = 2,
        .side = Side::Sell
    });
    
    const auto trades = book.add_order(Order{
        .order_id = 20,
        .price = 20,
        .quantity = 3,
        .side = Side::Buy
    });

    const auto partial_trade = book.add_order(Order{
        .order_id = 30,
        .price = 20,
        .quantity = 4,
        .side = Side::Buy
    });

    ASSERT_EQ(trades.size(), 2);

    EXPECT_EQ(trades[0].buy_order_id, 20);
    EXPECT_EQ(trades[0].sell_order_id, 10);
    EXPECT_EQ(trades[0].price, 12);
    EXPECT_EQ(trades[0].quantity, 2);

    EXPECT_EQ(trades[1].buy_order_id, 20);
    EXPECT_EQ(trades[1].sell_order_id, 11);
    EXPECT_EQ(trades[1].price, 12);
    EXPECT_EQ(trades[1].quantity, 1);

    ASSERT_EQ(partial_trade.size(), 1);
    EXPECT_EQ(partial_trade[0].buy_order_id, 30);
    EXPECT_EQ(partial_trade[0].sell_order_id, 11);
    EXPECT_EQ(partial_trade[0].price, 12);
    EXPECT_EQ(partial_trade[0].quantity, 1);
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), 20);

    EXPECT_FALSE(book.best_ask().has_value());  
}

TEST(OrderBookTest, MultiPriceLevelMatching)
{
    OrderBook book{1000};

    book.add_order(Order{
        .order_id = 10,
        .price = 101,
        .quantity = 2,
        .side = Side::Sell
    });

    book.add_order(Order{
        .order_id = 11,
        .price = 102,
        .quantity = 3,
        .side = Side::Sell
    });

    const auto trades = book.add_order(Order{
        .order_id = 20,
        .price = 105,
        .quantity = 4,
        .side = Side::Buy
    }); 

    ASSERT_EQ(trades.size(), 2);

    EXPECT_EQ(trades[0].buy_order_id, 20);
    EXPECT_EQ(trades[0].sell_order_id, 10);
    EXPECT_EQ(trades[0].price, 101);
    EXPECT_EQ(trades[0].quantity, 2);
    EXPECT_EQ(trades[1].buy_order_id, 20);
    EXPECT_EQ(trades[1].sell_order_id, 11);
    EXPECT_EQ(trades[1].price, 102);
    EXPECT_EQ(trades[1].quantity, 2);
    EXPECT_FALSE(book.best_bid().has_value());
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), 102);
}


TEST(OrderBookTest, BestAskRecomputedAfterBestAskIsExhausted)
{
    OrderBook book{1000};

    book.add_order(Order{
        .order_id = 10,
        .price = 101,
        .quantity = 2,
        .side = Side::Sell
    });

    book.add_order(Order{
        .order_id = 11,
        .price = 105,
        .quantity = 3,
        .side = Side::Sell
    });

    const auto trades = book.add_order(Order{
        .order_id = 20,
        .price = 101,
        .quantity = 2,
        .side = Side::Buy
    });

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].sell_order_id, 10);
    EXPECT_EQ(trades[0].quantity, 2);

    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), 105);

    EXPECT_FALSE(book.best_bid().has_value());
}


TEST(OrderBookTest, BestBidRecomputedAfterBestBidIsExhausted)
{
    OrderBook book{1000};

    book.add_order(Order{
        .order_id = 10,
        .price = 105,
        .quantity = 2,
        .side = Side::Buy
    });

    book.add_order(Order{
        .order_id = 11,
        .price = 100,
        .quantity = 3,
        .side = Side::Buy
    });

    const auto trades = book.add_order(Order{
        .order_id = 20,
        .price = 105,
        .quantity = 2,
        .side = Side::Sell
    });

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 10);
    EXPECT_EQ(trades[0].quantity, 2);

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), 100);

    EXPECT_FALSE(book.best_ask().has_value());
}


TEST(OrderBookTest, RejectsNegativeMaximumPrice)
{
    EXPECT_THROW(
        { OrderBook book{-1}; },
        std::invalid_argument
    );
}

TEST(OrderBookTest, RejectsPriceAboveMaximum)
{
    OrderBook book{1000};

    EXPECT_THROW(
        book.add_order(Order{
            .order_id = 1,
            .price = 1001,
            .quantity = 1,
            .side = Side::Buy
        }),
        std::invalid_argument
    );
}

TEST(OrderBookTest, RejectsNegativeOrderPrice)
{
    OrderBook book{1000};

    EXPECT_THROW(
        book.add_order(Order{
            .order_id = 1,
            .price = -1,
            .quantity = 1,
            .side = Side::Buy
        }),
        std::invalid_argument
    );
}

TEST(OrderBookTest, RejectsZeroQuantity)
{
    OrderBook book{1000};

    EXPECT_THROW(
        book.add_order(Order{
            .order_id = 1,
            .price = 100,
            .quantity = 0,
            .side = Side::Buy
        }),
        std::invalid_argument
    );
}


TEST(OrderBookTest, AcceptsMinimumPrice)
{
    OrderBook book{1000};

    const auto trades = book.add_order(Order{
        .order_id = 1,
        .price = 0,
        .quantity = 1,
        .side = Side::Buy
    });

    EXPECT_TRUE(trades.empty());
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), 0);
}

TEST(OrderBookTest, AcceptsMaximumPrice)
{
    OrderBook book{1000};

    const auto trades = book.add_order(Order{
        .order_id = 1,
        .price = 1000,
        .quantity = 1,
        .side = Side::Sell
    });

    EXPECT_TRUE(trades.empty());
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), 1000);
}

TEST(OrderBookTest, ReinitializesBestAskAfterSideBecomesEmpty)
{
    OrderBook book{1000};

    book.add_order(Order{
        .order_id = 1,
        .price = 100,
        .quantity = 2,
        .side = Side::Sell
    });

    // Consume the entire sell side.
    const auto trades = book.add_order(Order{
        .order_id = 2,
        .price = 100,
        .quantity = 2,
        .side = Side::Buy
    });

    ASSERT_EQ(trades.size(), 1);
    EXPECT_FALSE(book.best_ask().has_value());

    // A later sell must initialize the best ask again.
    const auto new_trades = book.add_order(Order{
        .order_id = 3,
        .price = 120,
        .quantity = 1,
        .side = Side::Sell
    });

    EXPECT_TRUE(new_trades.empty());
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), 120);
}

TEST(OrderBookTest, ReinitializesBestBidAfterSideBecomesEmpty)
{
    OrderBook book{1000};

    book.add_order(Order{
        .order_id = 1,
        .price = 100,
        .quantity = 2,
        .side = Side::Buy
    });

    // Consume the entire buy side.
    const auto trades = book.add_order(Order{
        .order_id = 2,
        .price = 100,
        .quantity = 2,
        .side = Side::Sell
    });

    ASSERT_EQ(trades.size(), 1);
    EXPECT_FALSE(book.best_bid().has_value());

    // A later buy must initialize the best bid again.
    const auto new_trades = book.add_order(Order{
        .order_id = 3,
        .price = 120,
        .quantity = 1,
        .side = Side::Buy
    });

    EXPECT_TRUE(new_trades.empty());
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), 120);
}





