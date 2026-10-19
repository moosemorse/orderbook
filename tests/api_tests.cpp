#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "OrderBook.hpp"

namespace
{
using Levels = std::vector<std::pair<Price, Quantity>>;

OrderPointer MakeOrder(const OrderType type, const OrderId id, const Side side, const Price price, const Quantity quantity)
{
  return std::make_shared<Order>(type, id, side, price, quantity);
}

OrderPointer Gtc(const OrderId id, const Side side, const Price price, const Quantity quantity)
{
  return MakeOrder(OrderType::GoodTillCancel, id, side, price, quantity);
}

// pairs print readably in gtest failure messages, LevelInfo doesn't
Levels ToLevels(const LevelInfos& infos)
{
  Levels levels;
  for (const auto& [price, quantity] : infos)
    levels.emplace_back(price, quantity);
  return levels;
}

Levels Bids(const OrderBook& book) { return ToLevels(book.GetOrderInfos().GetBids()); }
Levels Asks(const OrderBook& book) { return ToLevels(book.GetOrderInfos().GetAsks()); }

void ExpectTrade(const Trade& trade, const OrderId bidId, const OrderId askId, const Quantity quantity)
{
  EXPECT_EQ(trade.GetBidTrade().orderId_, bidId);
  EXPECT_EQ(trade.GetAskTrade().orderId_, askId);
  EXPECT_EQ(trade.GetBidTrade().quantity_, quantity);
  EXPECT_EQ(trade.GetAskTrade().quantity_, quantity);
}
} // namespace

// ---- Size / Contains / GetOrderInfos ----

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

// ---- AddOrder: GoodTillCancel ----

TEST(AddOrder, NonCrossingOrdersRest)
{
  OrderBook book;
  EXPECT_TRUE(book.AddOrder(Gtc(1, Side::Buy, 99, 10)).empty());
  EXPECT_TRUE(book.AddOrder(Gtc(2, Side::Sell, 100, 10)).empty());

  EXPECT_EQ(book.Size(), 2u);
  EXPECT_TRUE(book.Contains(1));
  EXPECT_TRUE(book.Contains(2));
}

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

// ---- AddOrder: FillAndKill ----

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

// ---- AddOrder: FillOrKill ----

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

// ---- AddOrder: GoodForDay ----

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

// ---- AddOrder: Market ----

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

// ---- CancelOrder ----

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

// ---- ModifyOrder ----

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

// ---- Lifetime ----

// destroying a book must wake and join the prune thread straight away, not at the 16:00 cutoff
TEST(Lifetime, DestructorReturnsPromptly)
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
