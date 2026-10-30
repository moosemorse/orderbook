#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "test_helpers.hpp"

TEST(EmptyBook, HasNothing)
{
  OrderBook book;
  EXPECT_EQ(book.Size(), 0u);
  EXPECT_FALSE(book.Contains(1));
  EXPECT_TRUE(Bids(book).empty());
  EXPECT_TRUE(Asks(book).empty());
}

TEST(GetOrderInfos, BidsBestFirstDescending)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 99, 5));
  book.AddOrder(Gtc(2, Side::Buy, 101, 7));
  book.AddOrder(Gtc(3, Side::Buy, 100, 3));

  EXPECT_EQ(Bids(book), (Levels{{101, 7}, {100, 3}, {99, 5}}));
}

TEST(GetOrderInfos, AsksBestFirstAscending)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 102, 5));
  book.AddOrder(Gtc(2, Side::Sell, 100, 7));
  book.AddOrder(Gtc(3, Side::Sell, 101, 3));

  EXPECT_EQ(Asks(book), (Levels{{100, 7}, {101, 3}, {102, 5}}));
}

TEST(GetOrderInfos, SumsQuantityAtALevel)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 5));
  book.AddOrder(Gtc(2, Side::Buy, 100, 7));

  EXPECT_EQ(Bids(book), (Levels{{100, 12}}));
  EXPECT_EQ(book.Size(), 2u);
}

TEST(GetOrderInfos, ReportsRemainingNotInitialQuantity)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));
  book.AddOrder(Gtc(2, Side::Sell, 100, 4));

  EXPECT_EQ(Bids(book), (Levels{{100, 6}}));
}

// most reliable under -fsanitize=thread
TEST(GetOrderInfos, SafeWithConcurrentWriters)
{
  OrderBook book;
  std::atomic<bool> stop{false};

  std::thread writer{[&]
                     {
                       for (OrderId id = 1; !stop.load(std::memory_order_relaxed); ++id)
                       {
                         book.AddOrder(Gtc(id, Side::Buy, static_cast<Price>(100 + id % 50), 1));
                         book.CancelOrder(id);
                       }
                     }};

  for (int i = 0; i < 200'000; ++i)
    book.GetOrderInfos();

  stop = true;
  writer.join();
}
