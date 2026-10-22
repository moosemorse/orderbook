#pragma once

#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "OrderBook.hpp"

using Levels = std::vector<std::pair<Price, Quantity>>;

inline OrderPointer MakeOrder(const OrderType type, const OrderId id, const Side side, const Price price, const Quantity quantity)
{
  return std::make_shared<Order>(type, id, side, price, quantity);
}

inline OrderPointer Gtc(const OrderId id, const Side side, const Price price, const Quantity quantity)
{
  return MakeOrder(OrderType::GoodTillCancel, id, side, price, quantity);
}

// pairs print readably in gtest failure messages, LevelInfo doesn't
inline Levels ToLevels(const LevelInfos& infos)
{
  Levels levels;
  for (const auto& [price, quantity] : infos)
    levels.emplace_back(price, quantity);
  return levels;
}

inline Levels Bids(const OrderBook& book) { return ToLevels(book.GetOrderInfos().GetBids()); }
inline Levels Asks(const OrderBook& book) { return ToLevels(book.GetOrderInfos().GetAsks()); }

inline void ExpectTrade(const Trade& trade, const OrderId bidId, const OrderId askId, const Quantity quantity)
{
  EXPECT_EQ(trade.GetBidTrade().orderId_, bidId);
  EXPECT_EQ(trade.GetAskTrade().orderId_, askId);
  EXPECT_EQ(trade.GetBidTrade().quantity_, quantity);
  EXPECT_EQ(trade.GetAskTrade().quantity_, quantity);
}
