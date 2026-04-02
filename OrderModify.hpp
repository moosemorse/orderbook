#pragma once

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