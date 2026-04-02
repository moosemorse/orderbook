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
  Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
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

int
main()
{
  return 0;
}
