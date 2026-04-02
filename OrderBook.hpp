#pragma once

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
private:
  // ++ storing iterator in each entry leads to O(1) deletion
  struct OrderEntry
  {
    OrderPointer order_{nullptr};
    OrderPointers::iterator location_;
  };

  struct LevelData
  {
    Quantity quantity_{};
    Quantity count_{};

    enum class Action
    {
      Add,
      Remove,
      Match,
    };
  };

  // map to represent bids and asks
  // bids sorted in descending order from best bid
  // asks sorted in ascending order from best ask
  // e.g. the level with the lowest price is our best ask <- prefer to match
  std::map<Price, OrderPointers, std::greater<Price>> bids_;
  std::map<Price, OrderPointers, std::less<Price>> asks_;
  std::unordered_map<OrderId, OrderEntry> orders_;
  std::unordered_map<Price, LevelData> data_; // data at a price level
  mutable std::mutex ordersMutex_;
  std::thread ordersPruneThread_;
  std::condition_variable shutdownConditionVariable_;

  void PruneGoodForDayOrders();

  void CancelOrders(OrderIds orderIds);
  void CancelOrderInternal(OrderId orderId);

  void OnOrderCancelled(OrderPointer order);
  void OnOrderAdded(OrderPointer order);
  void OnOrderMatched(Price price, Quantity quantity, bool isFullFilled);
  void UpdateLevelData(Price price, Quantity quantity, LevelData::Action action);

  bool CanFullyFill(Side side, Price price, Quantity quantity) const;
  bool CanMatch(Side side, Price price) const;

  Trades MatchOrders();

public:
  // object creation semantic stuff (destructors, constructors etc.)
  OrderBook();
  OrderBook(const OrderBook &) = delete;
  void operator=(const OrderBook &) = delete;
  OrderBook(OrderBook &&) = delete;
  void operator=(OrderBook &&) = delete;
  ~OrderBook();

  // common order apis on orderbook
  Trades AddOrder(OrderPointer order);
  void CancelOrder(OrderId orderId);
  Trades MatchOrder(OrderModify order);

  // debugging/less useful feat
  std::size_t Size() const;
  OrderBookLevelInfos GetOrderInfos() const;
};