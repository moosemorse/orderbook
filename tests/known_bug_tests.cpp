#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "OrderBook.hpp"

// Each test here reproduces a bug listed under "Known bugs" in TODO.md and fails until it is fixed.

namespace
{
OrderPointer MakeOrder(const OrderType type, const OrderId id, const Side side, const Price price, const Quantity quantity)
{
  return std::make_shared<Order>(type, id, side, price, quantity);
}

OrderPointer Gtc(const OrderId id, const Side side, const Price price, const Quantity quantity)
{
  return MakeOrder(OrderType::GoodTillCancel, id, side, price, quantity);
}
} // namespace

// the ask level at 100 empties and takes the bid's data_[100] with it
TEST(KnownBug, LevelDataSharedBetweenSides_FillOrKillMissesRestingBid)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 10));
  book.AddOrder(Gtc(2, Side::Sell, 100, 4));
  ASSERT_EQ(book.GetOrderInfos().GetBids().at(0).quantity_, 6u);

  const auto trades = book.AddOrder(MakeOrder(OrderType::FillOrKill, 3, Side::Sell, 100, 6));

  ASSERT_EQ(trades.size(), 1u);
  EXPECT_EQ(trades[0].GetBidTrade().quantity_, 6u);
  EXPECT_EQ(book.Size(), 0u);
}

// once data_[100] is gone, a later fill against the bid wraps it to ~4e9 of phantom liquidity
TEST(KnownBug, LevelDataSharedBetweenSides_PhantomLiquidityAcceptsFillOrKill)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 4));
  book.AddOrder(Gtc(2, Side::Buy, 100, 10));
  book.AddOrder(Gtc(3, Side::Sell, 99, 2));
  ASSERT_EQ(book.GetOrderInfos().GetBids().at(0).quantity_, 4u);

  EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillOrKill, 4, Side::Sell, 100, 100)).empty());
  EXPECT_FALSE(book.Contains(4));
  EXPECT_TRUE(book.GetOrderInfos().GetAsks().empty());
}

// cancelled order 2's quantity stays in data_, so FillOrKill thinks 10 is available
TEST(KnownBug, CancelLeavesLevelData_FillOrKillWronglyAccepted)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Buy, 100, 5));
  book.AddOrder(Gtc(2, Side::Buy, 100, 5));
  book.CancelOrder(2);

  EXPECT_TRUE(book.AddOrder(MakeOrder(OrderType::FillOrKill, 3, Side::Sell, 100, 10)).empty());
  EXPECT_FALSE(book.Contains(3));
  EXPECT_TRUE(book.Contains(1));
}

TEST(KnownBug, MarketOrderRemainderRests)
{
  OrderBook book;
  book.AddOrder(Gtc(1, Side::Sell, 100, 5));

  const auto trades = book.AddOrder(std::make_shared<Order>(2, Side::Buy, 10));

  ASSERT_EQ(trades.size(), 1u);
  EXPECT_FALSE(book.Contains(2));
  EXPECT_EQ(book.Size(), 0u);
  EXPECT_TRUE(book.GetOrderInfos().GetBids().empty());
}

// 0 is a usable price for an integer tick, so it can't double as "no price"
TEST(KnownBug, InvalidPriceIsZero)
{
  EXPECT_NE(Constants::InvalidPrice, Price{0});
}

// GetOrderInfos reads bids_/asks_ without ordersMutex_; most reliable under -fsanitize=thread
TEST(KnownBug, GetOrderInfosRacesWithWriters)
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

// ~OrderBook stores shutdown_ and notifies without the lock; landing between the prune thread's
// shutdown_ check and its wait_for loses the wakeup and join() blocks until 16:00
TEST(KnownBug, DestructorCanMissPruneThreadWakeup)
{
  auto done = std::make_shared<std::promise<void>>();
  auto finished = done->get_future();

  // detached so a hung join() fails this test instead of hanging the run
  std::thread{[done]
              {
                for (int i = 0; i < 20'000; ++i)
                {
                  OrderBook book;
                  std::this_thread::sleep_for(std::chrono::microseconds{i % 50});
                }
                done->set_value();
              }}
      .detach();

  ASSERT_EQ(finished.wait_for(std::chrono::seconds{60}), std::future_status::ready)
      << "an OrderBook destructor never returned";
}
