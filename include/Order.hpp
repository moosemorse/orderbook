#pragma once

#include <format>
#include <list>
#include <memory>
#include <stdexcept>

#include "Aliases.hpp"
#include "Constants.hpp"
#include "OrderType.hpp"
#include "Side.hpp"

class Order
{
public:
  Order(const OrderType orderType, const OrderId orderId, const Side side, const Price price, const Quantity quantity)
      : orderType_{orderType}, orderId_{orderId}, side_{side}, price_{price}, initialQuantity_{quantity}, remainingQuantity_{quantity}
  {
  }

  // market order: price is set later by ToGoodTillCancel
  Order(const OrderId orderId, const Side side, const Quantity quantity)
      : Order(OrderType::Market, orderId, side, Constants::InvalidPrice, quantity) {}

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
    if (quantity > GetRemainingQuantity())
      throw std::logic_error(std::format("Order ({}) cannot be filled for more than its remaining quantity.", GetOrderId()));

    remainingQuantity_ -= quantity;
  }

  // market orders only
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

// shared: an order lives in both orders_ and its price level
using OrderPointer = std::shared_ptr<Order>;

// list iterators survive inserts/erases elsewhere, so OrderEntry can store one
using OrderPointers = std::list<OrderPointer>;