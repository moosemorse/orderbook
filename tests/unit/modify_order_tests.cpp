#include <gtest/gtest.h>

#include "test_helpers.hpp"

TEST(ModifyOrder, UnknownIdIsNoOp)
{
  OrderBook book;
  EXPECT_TRUE(book.ModifyOrder(OrderModify{1, Side::Buy, 100, 10}).empty());
  EXPECT_EQ(book.Size(), 0u);
}

TEST(ModifyOrder, ChangesPriceAndQuantity)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));
  book.ModifyOrder(OrderModify{1, Side::Buy, 98, 4});

  EXPECT_EQ(book.Size(), 1u);
  EXPECT_EQ(Bids(book), (Levels{{98, 4}}));
}

TEST(ModifyOrder, LosesTimePriority)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 5));
  book.AddOrder(Gtc(2, Side::Buy, 100, 5));
  book.ModifyOrder(OrderModify{1, Side::Buy, 100, 5});

  const auto trades = book.AddOrder(Gtc(3, Side::Sell, 100, 5));
  ASSERT_EQ(trades.size(), 1u);
  ExpectTrade(trades[0], 2, 3, 5);
}

TEST(ModifyOrder, CanCrossAndMatch)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 101, 5));
  book.AddOrder(Gtc(2, Side::Buy, 100, 5));

  const auto trades = book.ModifyOrder(OrderModify{2, Side::Buy, 101, 5});

  ASSERT_EQ(trades.size(), 1u);
  ExpectTrade(trades[0], 2, 1, 5);
  EXPECT_EQ(book.Size(), 0u);
}
