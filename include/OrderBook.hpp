#pragma once

#include <atomic>
#include <condition_variable>
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
  std::unordered_map<Price, LevelData> data_; // quantity per price, for CanFullyFill
  mutable std::mutex ordersMutex_;
  std::thread ordersPruneThread_;
  std::condition_variable shutdownConditionVariable_;
  std::atomic<bool> shutdown_{false};

  void PruneGoodForDayOrders();

  void CancelOrders(const OrderIds& orderIds);
  void CancelOrderInternal(OrderId orderId);

  void OnOrderCancelled(const OrderPointer& order);
  void OnOrderAdded(const OrderPointer& order);
  void OnOrderMatched(Price price, Quantity quantity);
  void UpdateLevelData(Price price, Quantity quantity, LevelData::Action action);

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
};