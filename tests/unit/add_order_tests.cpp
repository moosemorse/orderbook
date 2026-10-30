#include <gtest/gtest.h>

#include "test_helpers.hpp"

TEST(AddOrder, NonCrossingOrdersRest)
{
  OrderBook book;
  EXPECT_TRUE(book.AddOrder(Gtc(1, Side::Buy, 99, 10)).empty());
  EXPECT_TRUE(book.AddOrder(Gtc(2, Side::Sell, 100, 10)).empty());

  EXPECT_EQ(book.Size(), 2u);
  EXPECT_TRUE(book.Contains(1));
  EXPECT_TRUE(book.Contains(2));
}

// TODO: we should probably throw here instead of ignoring
TEST(AddOrder, DuplicateIdIsIgnored)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));

  EXPECT_TRUE(book.AddOrder(Gtc(1, Side::Sell, 100, 10)).empty());
  EXPECT_EQ(book.Size(), 1u);
  EXPECT_EQ(Bids(book), (Levels{{100, 10}}));
  EXPECT_TRUE(Asks(book).empty());
}

TEST(AddOrder, FullMatchEmptiesBothSides)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));
  const auto trades = book.AddOrder(Gtc(2, Side::Sell, 100, 10));

  ASSERT_EQ(trades.size(), 1u);
  ExpectTrade(trades[0], 1, 2, 10);
  EXPECT_EQ(book.Size(), 0u);
  EXPECT_TRUE(Bids(book).empty());
  EXPECT_TRUE(Asks(book).empty());
}

TEST(AddOrder, IncomingRemainderRests)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 4));
  const auto trades = book.AddOrder(Gtc(2, Side::Buy, 100, 10));

  ASSERT_EQ(trades.size(), 1u);
  ExpectTrade(trades[0], 2, 1, 4);
  EXPECT_FALSE(book.Contains(1));
  EXPECT_TRUE(book.Contains(2));
  EXPECT_EQ(Bids(book), (Levels{{100, 6}}));
  EXPECT_TRUE(Asks(book).empty());
}

TEST(AddOrder, TradeCarriesEachOrdersOwnPrice)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 5));
  const auto trades = book.AddOrder(Gtc(2, Side::Buy, 105, 5));

  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].GetBidTrade().price_, 105);
  EXPECT_EQ(trades[0].GetAskTrade().price_, 100);
}

TEST(AddOrder, SweepsLevelsBestPriceFirst)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 102, 5));
  book.AddOrder(Gtc(2, Side::Sell, 100, 5));
  book.AddOrder(Gtc(3, Side::Sell, 101, 5));

  const auto trades = book.AddOrder(Gtc(4, Side::Buy, 101, 12));

  ASSERT_EQ(trades.size(), 2u);
  ExpectTrade(trades[0], 4, 2, 5);
  ExpectTrade(trades[1], 4, 3, 5);
  EXPECT_EQ(Bids(book), (Levels{{101, 2}}));
  EXPECT_EQ(Asks(book), (Levels{{102, 5}}));
}

TEST(AddOrder, TimePriorityWithinALevel)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 5));
  book.AddOrder(Gtc(2, Side::Buy, 100, 5));

  const auto trades = book.AddOrder(Gtc(3, Side::Sell, 100, 7));

  ASSERT_EQ(trades.size(), 2u);
  ExpectTrade(trades[0], 1, 3, 5);
  ExpectTrade(trades[1], 2, 3, 2);
  EXPECT_FALSE(book.Contains(1));
  EXPECT_EQ(Bids(book), (Levels{{100, 3}}));
}
