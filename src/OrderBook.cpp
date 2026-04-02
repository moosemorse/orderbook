#include "OrderBook.hpp"

#include <chrono>
#include <ctime>
#include <numeric>

// cancel any orders that are expired i.e. at end of trading day
void OrderBook::PruneGoodForDayOrders()
{
  using namespace std::chrono;
  const auto end = hours(16);

  while (true)
  {
    const auto now = system_clock::now();
    const auto now_c = system_clock::to_time_t(now);
    std::tm now_parts;
    localtime_r(&now_c, &now_parts);

    if (now_parts.tm_hour >= end.count())
      now_parts.tm_mday += 1;

    now_parts.tm_hour = static_cast<int>(end.count());
    now_parts.tm_min = 0;
    now_parts.tm_sec = 0;

    auto next = system_clock::from_time_t(mktime(&now_parts));
    auto till = next - now + milliseconds(100);

    {
      std::unique_lock ordersLock{ordersMutex_};

      if (shutdown_.load(std::memory_order_acquire) ||
          shutdownConditionVariable_.wait_for(ordersLock, till) == std::cv_status::no_timeout)
        return;
    }

    OrderIds orderIds;

    {
      std::scoped_lock ordersLock{ordersMutex_};
      for (const auto &[_, entry] : orders_)
      {
        const auto &order = entry.order_;

        if (order->GetOrderType() != OrderType::GoodForDay)
          continue;

        orderIds.push_back(order->GetOrderId());
      }
    }

    CancelOrders(orderIds);
  }
}

// helper to cancel a vector of orders
void OrderBook::CancelOrders(OrderIds orderIds)
{
  std::scoped_lock ordersLock{ordersMutex_};

  for (const auto &orderId : orderIds)
    CancelOrderInternal(orderId);
}

// cancel order by
// 1. removing from global orders table by erasing it
// 2. find which table has order and erase it there too
// 3. if price level at that table has no more orders erase entire price level
void OrderBook::CancelOrderInternal(OrderId orderId)
{
  if (!orders_.contains(orderId))
    return;

  const auto &[order, it] = orders_.at(orderId);
  orders_.erase(orderId);

  // I hate this, asks and bids have different signatures due to comparators
  // so we can abstract this into lambda and cant use ternary

  auto eraseOrderFromPriceLevel = [&](auto &orders, Price price)
  {
    auto &level = orders.at(price);
    level.erase(it);
    if (level.empty())
      orders.erase(price);
  };

  if (order->GetSide() == Side::Sell)
    eraseOrderFromPriceLevel(asks_, order->GetPrice());
  else
    eraseOrderFromPriceLevel(bids_, order->GetPrice());
}

// if order cancelled, quantity from that order removed at price level
void OrderBook::OnOrderCancelled(OrderPointer order)
{
  UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
}

// if order added, quantity of new order added at that price level
void OrderBook::OnOrderAdded(OrderPointer order)
{
  UpdateLevelData(order->GetPrice(), order->GetInitialQuantity(), LevelData::Action::Add);
}

// remove quantity at that price level based on matched order
// if order matched, we dont change ref count at that price level
void OrderBook::OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled)
{
  UpdateLevelData(price, quantity, isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
}

// update price level data table by updating ref count and quantity
// if no more references at the price level erase entry
void OrderBook::UpdateLevelData(Price price, Quantity quantity, LevelData::Action action)
{
  auto &data = data_[price];

  data.count_ += action == LevelData::Action::Remove ? -1 : action == LevelData::Action::Add ? 1
                                                                                             : 0;
  if (action == LevelData::Action::Remove || action == LevelData::Action::Match)
  {
    data.quantity_ -= quantity;
  }
  else
  {
    data.quantity_ += quantity;
  }

  if (data.count_ == 0)
    data_.erase(price);
}

Trades OrderBook::AddOrder(OrderPointer order)
{
  std::scoped_lock ordersLock{ordersMutex_};

  // c++20: contains
  if (orders_.contains(order->GetOrderId()))
    return {};
  if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
    return {};
  if (order->GetOrderType() == OrderType::FillOrKill && !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity()))
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

  OnOrderAdded(order);

  // finally, try match orders as result of adding new order
  return MatchOrders();
}

void OrderBook::CancelOrder(OrderId orderId)
{
  std::scoped_lock ordersLock{ordersMutex_};

  CancelOrderInternal(orderId);
}

// public api to match order on modified order
// check that this modified is in order book
// and then cancel old order and match new modified order
Trades OrderBook::ModifyOrder(OrderModify order)
{
  OrderType orderType;
  {
    std::scoped_lock ordersLock{ordersMutex_};

    if (!orders_.contains(order.GetOrderId()))
      return {};

    const auto &[existingOrder, _] = orders_.at(order.GetOrderId());
    orderType = existingOrder->GetOrderType();
  }

  CancelOrder(order.GetOrderId());
  return AddOrder(order.ToOrderPointer(orderType));
}

std::size_t OrderBook::Size() const
{
  std::scoped_lock ordersLock{ordersMutex_};
  return orders_.size();
}

// maybe useful for debugging/info later
OrderBookLevelInfos OrderBook::GetOrderInfos() const
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

bool OrderBook::CanFullyFill(Side side, Price price, Quantity quantity) const
{
  if (!CanMatch(side, price))
    return false;

  std::optional<Price> threshold;

  if (side == Side::Buy)
  {
    const auto [askPrice, _] = *asks_.begin();
    threshold = askPrice;
  }
  else
  {
    const auto [bidPrice, _] = *bids_.begin();
    threshold = bidPrice;
  }

  for (const auto &[levelPrice, levelData] : data_)
  {
    if (threshold.has_value() &&
        ((side == Side::Buy && threshold.value() > levelPrice) ||
         (side == Side::Sell && threshold.value() < levelPrice)))
      continue;

    if ((side == Side::Buy && levelPrice > price) ||
        (side == Side::Sell && levelPrice < price))
      continue;

    if (quantity <= levelData.quantity_)
      return true;

    quantity -= levelData.quantity_;
  }

  return false;
}

// try to match a side with given price to other side
// e.g. a buy order at some price is valid if we can find that the best
// ask is less than what they want
// TODO? Refactor to pick map and condition based on side
bool OrderBook::CanMatch(Side side, Price price) const
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

Trades OrderBook::MatchOrders()
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
      // make sure to take copy of shared_ptr (increments reference count)
      // so even after pop_front destroys lists copy, our copies are saved
      auto bid = bids.front();
      auto ask = asks.front();

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
      {
        bids_.erase(bidPrice);
        data_.erase(bidPrice);
      }

      if (asks.empty())
      {
        asks_.erase(askPrice);
        data_.erase(askPrice);
      }

      trades.push_back(Trade{
          TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity},
          TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity}});

      OnOrderMatched(bid->GetPrice(), quantity, bid->IsFilled());
      OnOrderMatched(ask->GetPrice(), quantity, ask->IsFilled());
    }
  }
  if (!bids_.empty())
  {
    auto &[_, bids] = *bids_.begin();
    auto &order = bids.front();
    if (order->GetOrderType() == OrderType::FillAndKill)
      CancelOrder(order->GetOrderId());
  }
  if (!asks_.empty())
  {
    auto &[_, asks] = *asks_.begin();
    auto &order = asks.front();
    if (order->GetOrderType() == OrderType::FillAndKill)
      CancelOrder(order->GetOrderId());
  }
  return trades;
}

OrderBook::OrderBook()
    : ordersPruneThread_{[this]
                         { PruneGoodForDayOrders(); }} {};

OrderBook::~OrderBook()
{
  shutdown_.store(true, std::memory_order_release);
  shutdownConditionVariable_.notify_one();
  ordersPruneThread_.join();
}