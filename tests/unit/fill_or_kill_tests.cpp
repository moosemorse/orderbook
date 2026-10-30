#include <gtest/gtest.h>

#include "test_helpers.hpp"

TEST(FillOrKill, FillsAcrossLevelsWhenEnoughLiquidity)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));
  book.AddOrder(Gtc(2, Side::Buy, 99, 10));

  const auto trades = book.AddOrder(MakeOrder(OrderType::FillOrKill, 3, Side::Sell, 99, 15));

  ASSERT_EQ(trades.size(), 2u);
  ExpectTrade(trades[0], 1, 3, 10);
  ExpectTrade(trades[1], 2, 3, 5);
  EXPECT_FALSE(book.Contains(3));
  EXPECT_EQ(Bids(book), (Levels{{99, 5}}));
}

TEST(FillOrKill, RejectedWhenNotEnoughLiquidity)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));
  book.AddOrder(Gtc(2, Side::Buy, 99, 10));

  EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillOrKill, 3, Side::Sell, 99, 25)).empty());
  EXPECT_FALSE(book.Contains(3));
  EXPECT_EQ(Bids(book), (Levels{{100, 10}, {99, 10}}));
}

TEST(FillOrKill, IgnoresLiquidityBeyondItsLimit)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 5));
  book.AddOrder(Gtc(2, Side::Sell, 101, 10));

  EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillOrKill, 3, Side::Buy, 100, 10)).empty());
  EXPECT_EQ(Asks(book), (Levels{{100, 5}, {101, 10}}));
}

TEST(FillOrKill, IgnoresItsOwnSide)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 5));
  book.AddOrder(Gtc(2, Side::Buy, 99, 100));

  EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillOrKill, 3, Side::Buy, 100, 10)).empty());
  EXPECT_EQ(Asks(book), (Levels{{100, 5}}));
}

TEST(FillOrKill, RejectedOnEmptyBook)
{
  OrderBook book;
  EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillOrKill, 1, Side::Buy, 100, 5)).empty());
  EXPECT_EQ(book.Size(), 0u);
}

// a bid and an ask at the same price must not share level quantity
TEST(FillOrKill, SeesBidLeftAfterAskAtSamePriceFills)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));
  book.AddOrder(Gtc(2, Side::Sell, 100, 4));
  ASSERT_EQ(Bids(book), (Levels{{100, 6}}));

  const auto trades = book.AddOrder(MakeOrder(OrderType::FillOrKill, 3, Side::Sell, 100, 6));

  ASSERT_EQ(trades.size(), 1u);
  ExpectTrade(trades[0], 1, 3, 6);
  EXPECT_EQ(book.Size(), 0u);
}

TEST(FillOrKill, RejectedAfterRepeatedPartialFillsOfALevel)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 4));
  book.AddOrder(Gtc(2, Side::Buy, 100, 10));
  book.AddOrder(Gtc(3, Side::Sell, 99, 2));
  ASSERT_EQ(Bids(book), (Levels{{100, 4}}));

  EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillOrKill, 4, Side::Sell, 100, 100)).empty());
  EXPECT_FALSE(book.Contains(4));
  EXPECT_TRUE(Asks(book).empty());
}

TEST(FillOrKill, IgnoresCancelledQuantity)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 5));
  book.AddOrder(Gtc(2, Side::Buy, 100, 5));
  book.CancelOrder(2);

  EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillOrKill, 3, Side::Sell, 100, 10)).empty());
  EXPECT_FALSE(book.Contains(3));
  EXPECT_TRUE(book.Contains(1));
}
