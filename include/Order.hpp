#pragma once

#include <exception>
#include <format>
#include <list>
#include <memory>

#include "Aliases.hpp"
#include "Constants.hpp"
#include "OrderType.hpp"
#include "Side.hpp"

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

  // support market order types, price is not important -> rename to classic factory style?
  Order(OrderId orderId, Side side, Quantity quantity)
      : Order(OrderType::Market, orderId, side, Constants::InvalidPrice, quantity) {}

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

  // change ordertype to goodTillCancel and price of order to price passed in
  // PRE: used to only convert market orders
  void ToGoodTillCancel(Price price)
  {
    if (GetOrderType() != OrderType::Market)
      throw std::logic_error(std::format("Order ({}) cannot have its price adjusted, only market orders can.", GetOrderId()));

    price_ = price;
    orderType_ = OrderType::GoodTillCancel;
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