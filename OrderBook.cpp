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

enum class OrderType
{
  GoodTillCancel,
  FillAndKill
};

enum class Side
{
  Buy,
  Sell
};

// some aliases for better readability
using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

/* LevelInfo useful for public APIs to interact with state of orderbook. */
struct LevelInfo
{
  Price price_;
  Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

// we want to encapsulate level infos
class OrderBookLevelInfos
{
public:
  OrderBookLevelInfos(const LevelInfos &bids, const LevelInfos &asks)
      : bids_{bids}, asks_{asks}
  {
  }

  const LevelInfos &GetBids() const
  {
    return bids_;
  }
  const LevelInfos &GetAsks() const
  {
    return asks_;
  }

private:
  LevelInfos bids_;
  LevelInfos asks_;
};

/* Now we need to describe the things we add to an order book which are order objects. */
class Order
{
public:
  // note about quantity: when we talk about an order we really have 3 quantities
  // initial quantity, remaining quantity, and amount filled
  Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
      : orderType_{orderType}, orderId_{orderId}, side_{side}, price_{price}, initialQuantity_{quantity}, remainingQuantity_{quantity}
  {
  }

  // note: const just before func body means that the function is part of a class
  // and cant change any members of that class AND only const objects can call these functions
  OrderId GetOrderId() const
  {
    return orderId_;
  }

  Side GetSide() const
  {
    return side_;
  }

  Price GetPrice() const
  {
    return price_;
  }

  OrderType GetOrderType() const
  {
    return orderType_;
  }

  // might not be important but mentioned in some documentation
  // to be honest, we care more about remaining quantity
  Quantity GetInitialQuantity() const
  {
    return initialQuantity_;
  }

  Quantity GetRemainingQuantity() const
  {
    return remainingQuantity_;
  }

  Quantity GetFilledQuantity() const
  {
    return GetInitialQuantity() - GetRemainingQuantity();
  }

  bool IsFilled() const { return GetRemainingQuantity() == 0; }

  void Fill(Quantity quantity)
  {
    if (quantity > GetRemainingQuantity()) // note, std::format works by {} being replaced by *args
      throw std::logic_error(std::format("Order ({}) cannot be filled for more than its remaining quantity.", GetOrderId()));

    remainingQuantity_ -= quantity;
  }

private:
  OrderType orderType_;
  OrderId orderId_;
  Side side_;
  Price price_;
  Quantity initialQuantity_;
  Quantity remainingQuantity_;
};

// because we are storing one Order in multiple data structures in our order book
// we're going to want to use reference semantics (orders dict and ask/bids dict)
using OrderPointer = std::shared_ptr<Order>;

// list for order data structure, because a list gives an iterator which cannot
// be invalidated no matter how large it grows (alternative is vector, TODO?)
using OrderPointers = std::list<OrderPointer>;

// common apis: add, modify, cancel
// add and cancel are simple, modify requires the ability to modify different
// attributes like remaining price or side (side isn't necessary)

class OrderModify
{
public:
  OrderModify(OrderId orderId, Side side, Price price, Quantity quantity)
      : orderId_{orderId}, price_{price}, side_{side}, quantity_{quantity} {}

  OrderId GetOrderId() const { return orderId_; }
  Price GetPrice() const { return price_; }
  Side GetSide() const { return side_; }
  Quantity GetQuantity() const { return quantity_; }

  // current: only can modify good till cancel, but can be extended
  OrderPointer ToOrderPointer(OrderType type) const
  {
    return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(), GetQuantity());
  }

private:
  OrderId orderId_;
  Price price_;
  Side side_;
  Quantity quantity_;
};

struct TradeInfo
{
  OrderId orderId_;
  Price price_;
  Quantity quantity_;
};

// an order matched is the aggregation of two trade info objects (for ask and bid)
class Trade
{
public:
  Trade(const TradeInfo &bidTrade, const TradeInfo &askTrade)
      : bidTrade_{bidTrade}, askTrade_{askTrade}
  {
  }

  const TradeInfo &GetBidTrade() const { return bidTrade_; }
  const TradeInfo &GetAskTrade() const
  {
    return askTrade_;
  }

private:
  TradeInfo bidTrade_;
  TradeInfo askTrade_;
};

// note: one order can sweep many orders
using Trades = std::vector<Trade>;

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
      iterator = std::next(orders.begin(), orders.size() - 1);
    }
    else
    {
      auto &orders = asks_[order->GetPrice()];
      orders.push_back(order);
      iterator = std::next(orders.begin(), orders.size() - 1);
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
                                              [](std::size_t runningSum, const OrderPointer &order)
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

int main()
{
  return 0;
}
