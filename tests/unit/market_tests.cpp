#include <gtest/gtest.h>

#include <memory>

#include "test_helpers.hpp"

TEST(Market, RejectedWhenOppositeSideEmpty)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));

  EXPECT_TRUE(book.AddOrder(std::make_shared<Order>(2, Side::Buy, 5)).empty());
  EXPECT_FALSE(book.Contains(2));
  EXPECT_EQ(book.Size(), 1u);
}

TEST(Market, BuySweepsAsksRegardlessOfPrice)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 5));
  book.AddOrder(Gtc(2, Side::Sell, 110, 5));

  const auto trades = book.AddOrder(std::make_shared<Order>(3, Side::Buy, 10));

  ASSERT_EQ(trades.size(), 2u);
  ExpectTrade(trades[0], 3, 1, 5);
  ExpectTrade(trades[1], 3, 2, 5);
  EXPECT_EQ(book.Size(), 0u);
}

TEST(Market, SellSweepsBidsRegardlessOfPrice)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 5));
  book.AddOrder(Gtc(2, Side::Buy, 90, 5));

  const auto trades = book.AddOrder(std::make_shared<Order>(3, Side::Sell, 8));

  ASSERT_EQ(trades.size(), 2u);
  ExpectTrade(trades[0], 1, 3, 5);
  ExpectTrade(trades[1], 2, 3, 3);
  EXPECT_EQ(Bids(book), (Levels{{90, 2}}));
}

TEST(Market, UnfilledRemainderIsCancelled)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 5));

  const auto trades = book.AddOrder(std::make_shared<Order>(2, Side::Buy, 10));

  ASSERT_EQ(trades.size(), 1u);
  EXPECT_FALSE(book.Contains(2));
  EXPECT_EQ(book.Size(), 0u);
  EXPECT_TRUE(Bids(book).empty());
}

// 0 is a usable price for an integer tick, so it can't double as "no price"
TEST(Market, InvalidPriceIsNotZero)
{
  EXPECT_NE(Constants::InvalidPrice, Price{0});
}
