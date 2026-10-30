#include <gtest/gtest.h>

#include "test_helpers.hpp"

TEST(FillAndKill, RejectedWhenItCannotCross)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 101, 5));

  EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillAndKill, 2, Side::Buy, 100, 5)).empty());
  EXPECT_FALSE(book.Contains(2));
  EXPECT_EQ(book.Size(), 1u);
}

TEST(FillAndKill, RejectedOnEmptyBook)
{
  OrderBook book;
  EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillAndKill, 1, Side::Sell, 100, 5)).empty());
  EXPECT_EQ(book.Size(), 0u);
}

TEST(FillAndKill, FillsWhatItCanAndCancelsTheRest)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 4));

  const auto trades = book.AddOrder(MakeOrder(OrderType::FillAndKill, 2, Side::Buy, 100, 10));

  ASSERT_EQ(trades.size(), 1u);
  ExpectTrade(trades[0], 2, 1, 4);
  EXPECT_FALSE(book.Contains(2));
  EXPECT_EQ(book.Size(), 0u);
  EXPECT_TRUE(Bids(book).empty());
}

TEST(FillAndKill, DoesNotTradeThroughItsLimit)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 4));
  book.AddOrder(Gtc(2, Side::Sell, 101, 4));

  const auto trades = book.AddOrder(MakeOrder(OrderType::FillAndKill, 3, Side::Buy, 100, 10));

  ASSERT_EQ(trades.size(), 1u);
  ExpectTrade(trades[0], 3, 1, 4);
  EXPECT_FALSE(book.Contains(3));
  EXPECT_EQ(Asks(book), (Levels{{101, 4}}));
}
