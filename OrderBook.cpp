#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <deque>
#include <format>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

class Orderbook
{
private:
  struct OrderEntry
  {
    OrderPointer order_{nullptr};
    OrderPointers::iterator location_;
  };

  // map to represent bids and asks
  // bids sorted in descending order from best bid
  // asks sorted in ascending order from best ask
  // e.g. the level with the lowest price is our best ask <- prefer to match
  std::map<Price, OrderPointers, std::greater<Price>> bids_;
  std::map<Price, OrderPointers, std::less<Price>> asks_;
  std::unordered_map<OrderId, OrderEntry> orders_;

  // try to match a side with given price to other side
  // e.g. a buy order at some price is valid if we can find that the best
  // ask is less than what they want
  // TODO? Refactor to pick map and condition based on side
  bool CanMatch(Side side, Price price) const
  {
    if (side == Side::Buy)
    {
      if (asks_.empty())
        return false;

      const auto &[bestAsk, _] = *asks_.begin();
      return price >= bestAsk;
    }
    else
    {
      if (bids_.empty())
        return false;

      const auto &[bestBid, _] = *bids_.begin();
      return price <= bestBid;
    }
  }

  Trades MatchOrders()
  {
    Trades trades;
    trades.reserve(orders_.size()); // optimistic, orders_.size predicts we have that many trades

    while (true)
    {
      if (bids_.empty() || asks_.empty())
        break;

      // iterator from ordered maps
      auto &[bidPrice, bids] = *bids_.begin();
      auto &[askPrice, asks] = *asks_.begin();

      // preemptive break
      if (bidPrice < askPrice)
        break;

      // try match orders
      while (bids.size() && asks.size())
      {
        auto &bid = bids.front();
        auto &ask = asks.front();

        Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

        bid->Fill(quantity);
        ask->Fill(quantity);

        // cleanup - TODO, duplicate code, abstract into erase order
        if (bid->IsFilled())
        {
          bids.pop_front();
          orders_.erase(bid->GetOrderId());
        }

        // cleanup
        if (ask->IsFilled())
        {
          asks.pop_front();
          orders_.erase(ask->GetOrderId());
        }

        // further clean up for maps
        if (bids.empty())
          bids_.erase(bidPrice);

        if (asks.empty())
          asks_.erase(askPrice);

        trades.push_back(Trade{TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity},
                               TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity}});
      }
    }

    if (!bids_.empty())
    {
      auto &[_, bids] = *bids_.begin();
      auto &order = bids.front();
      // if (order->GetOrderType() == OrderType::FillAndKill)
      //    CancelOrder(order->GetOrderId()); // TODO implement
    }

    if (!asks_.empty())
    {
      auto &[_, asks] = *asks_.begin();
      auto &order = asks.front();
      // if (order->GetOrderType() == OrderType::FillAndKill)
      //   CancelOrder(order->GetOrderId()); // TODO implement
    }

    return trades;
  }

public:
  Orderbook()
      : bids_{}, asks_{}, orders_{} {};
  Trades AddOrder(OrderPointer order)
  {
    // c++20: contains
    if (orders_.contains(order->GetOrderId()))
      return {};
    if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
      return {};

    OrderPointers::iterator iterator;

    // add order to respective map and select iterator for map (bids/asks)
    if (order->GetSide() == Side::Buy)
    {
      auto &orders = bids_[order->GetPrice()];
      orders.push_back(order);
      iterator = std::prev(orders.end());
    }
    else
    {
      auto &orders = asks_[order->GetPrice()];
      orders.push_back(order);
      iterator = std::prev(orders.end());
    }

    // update global order map with new order
    orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});

    // finally, try match orders as result of adding new order
    return MatchOrders();
  }

  void CancelOrder(OrderId orderId)
  {
    if (!orders_.contains(orderId))
      return;

    // get order by orderId
    const auto &[order, iterator] = orders_.at(orderId);
    orders_.erase(orderId); // clear from map

    // update respective bid/ask table
    // look at order at price level and remove cancelled order from
    // that level, if no more orders at price level we can erase
    // existing price level
    if (order->GetSide() == Side::Sell)
    {
      auto price = order->GetPrice();
      auto &orders = asks_.at(price);
      orders.erase(iterator);
      if (orders.empty())
        asks_.erase(price);
    }
    else
    {
      auto price = order->GetPrice();
      auto &orders = bids_.at(price);
      orders.erase(iterator);
      if (orders.empty())
        bids_.erase(price);
    }
  }

  // public api to match order on modified order
  // check that this modified is in order book
  // and then cancel old order and match new modified order
  Trades MatchOrder(OrderModify order)
  {
    if (!orders_.contains(order.GetOrderId()))
      return {};

    const auto &[existingOrder, _] = orders_.at(order.GetOrderId());
    CancelOrder(order.GetOrderId());
    return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
  }

  std::size_t Size() const { return orders_.size(); }

  // maybe useful for debugging/info later
  OrderBookLevelInfos GetOrderInfos() const
  {
    LevelInfos bidInfos, askInfos;
    bidInfos.reserve(orders_.size());
    askInfos.reserve(orders_.size());

    // A level info at some price, and we accumulate all quantities at the level
    // make sure to get remaining
    auto CreateLevelInfos = [](Price price, const OrderPointers &orders)
    {
      return LevelInfo{price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
                                              [](Quantity runningSum, const OrderPointer &order)
                                              { return runningSum + order->GetRemainingQuantity(); })};
    };

    // accumulate level infos for each bids and asks
    //
    for (const auto &[price, orders] : bids_)
      bidInfos.push_back(CreateLevelInfos(price, orders));

    for (const auto &[price, orders] : asks_)
      askInfos.push_back(CreateLevelInfos(price, orders));

    return OrderBookLevelInfos{bidInfos, askInfos};
  }
};