#pragma once

#include <condition_variable>
#include <iosfwd>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "Aliases.hpp"
#include "Order.hpp"
#include "OrderBookLevelInfos.hpp"
#include "OrderModify.hpp"
#include "Trade.hpp"

class OrderBook
{
  // iterator into the level's list gives O(1) cancel
  struct OrderEntry
  {
    OrderPointer order_{nullptr};
    OrderPointers::iterator location_;
  };

  struct LevelData
  {
    Quantity quantity_{};

    enum class Action
    {
      Add,
      Remove,
    };
  };

  // begin() is the best price on both sides
  std::map<Price, OrderPointers, std::greater<Price>> bids_;
  std::map<Price, OrderPointers, std::less<Price>> asks_;
  std::unordered_map<OrderId, OrderEntry> orders_;
  // quantity per price, for CanFullyFill; per side because a bid and an ask can share a price
  std::unordered_map<Price, LevelData> bidData_;
  std::unordered_map<Price, LevelData> askData_;
  mutable std::mutex ordersMutex_;
  std::condition_variable shutdownConditionVariable_;
  bool shutdown_{false}; // guarded by ordersMutex_
  // declared last: members initialise in order, and the thread uses all of the above
  std::thread ordersPruneThread_;

  void PruneGoodForDayOrders();

  void CancelOrders(const OrderIds& orderIds);
  void CancelOrderInternal(OrderId orderId);

  void OnOrderCancelled(const OrderPointer& order);
  void OnOrderAdded(const OrderPointer& order);
  void OnOrderMatched(Side side, Price price, Quantity quantity);
  void UpdateLevelData(Side side, Price price, Quantity quantity, LevelData::Action action);

  bool CanFullyFill(Side side, Price price, Quantity quantity) const;
  bool CanMatch(Side side, Price price) const;

  Trades MatchOrders();

public:
  OrderBook();
  OrderBook(const OrderBook &) = delete;
  void operator=(const OrderBook &) = delete;
  OrderBook(OrderBook &&) = delete;
  void operator=(OrderBook &&) = delete;
  ~OrderBook();

  Trades AddOrder(const OrderPointer& order);
  void CancelOrder(OrderId orderId);
  Trades ModifyOrder(const OrderModify &order);

  std::size_t Size() const;
  bool Contains(OrderId orderId) const;
  OrderBookLevelInfos GetOrderInfos() const;
  void Print(std::ostream& out) const;
};