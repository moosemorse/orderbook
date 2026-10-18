#pragma once

#include "Order.hpp"

class OrderModify
{
public:
  OrderModify(const OrderId orderId, const Side side, const Price price, const Quantity quantity)
      : orderId_{orderId}, price_{price}, side_{side}, quantity_{quantity} {}

  OrderId GetOrderId() const { return orderId_; }
  Price GetPrice() const { return price_; }
  Side GetSide() const { return side_; }
  Quantity GetQuantity() const { return quantity_; }

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