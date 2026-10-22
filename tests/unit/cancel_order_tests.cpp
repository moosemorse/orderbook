#include <gtest/gtest.h>

#include "test_helpers.hpp"

TEST(CancelOrder, RemovesOrderAndEmptyLevel)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));
  book.CancelOrder(1);

  EXPECT_FALSE(book.Contains(1));
  EXPECT_EQ(book.Size(), 0u);
  EXPECT_TRUE(Bids(book).empty());
}

TEST(CancelOrder, KeepsOtherOrdersAtTheLevel)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 10));
  book.AddOrder(Gtc(2, Side::Sell, 100, 3));
  book.CancelOrder(1);

  EXPECT_TRUE(book.Contains(2));
  EXPECT_EQ(Asks(book), (Levels{{100, 3}}));
}

TEST(CancelOrder, UnknownIdIsNoOp)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));
  book.CancelOrder(42);

  EXPECT_EQ(book.Size(), 1u);
}

TEST(CancelOrder, CancelledOrderNoLongerMatches)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));
  book.CancelOrder(1);

  EXPECT_TRUE(book.AddOrder(Gtc(2, Side::Sell, 100, 10)).empty());
  EXPECT_EQ(Asks(book), (Levels{{100, 10}}));
}
