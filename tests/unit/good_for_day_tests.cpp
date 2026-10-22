#include <gtest/gtest.h>

#include "test_helpers.hpp"

TEST(GoodForDay, RestsAndMatchesLikeGoodTillCancel)
{
  OrderBook book;
  EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::GoodForDay, 1, Side::Buy, 100, 10)).empty());
  EXPECT_TRUE(book.Contains(1));

  const auto trades = book.AddOrder(Gtc(2, Side::Sell, 100, 4));
  ASSERT_EQ(trades.size(), 1u);
  ExpectTrade(trades[0], 1, 2, 4);
  EXPECT_EQ(Bids(book), (Levels{{100, 6}}));
}
