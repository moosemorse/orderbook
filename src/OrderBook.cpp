#include "OrderBook.hpp"

#include <chrono>
#include <ctime>
#include <numeric>
#include <ranges>

// cancel any orders that are expired i.e. at end of trading day
void OrderBook::PruneGoodForDayOrders()
{
  using namespace std::chrono;
  const auto end = hours(16); // todo, magic number, 4pm end of trading day

  while (true)
  {
    const auto now = system_clock::now();
    const auto now_c = system_clock::to_time_t(now);
    std::tm now_parts;
    localtime_r(&now_c, &now_parts);

    // already past the cutoff, so the next one is tomorrow
    if (now_parts.tm_hour >= end.count())
      now_parts.tm_mday += 1;

    now_parts.tm_hour = static_cast<int>(end.count());
    now_parts.tm_min = 0;
    now_parts.tm_sec = 0;

    auto next = system_clock::from_time_t(mktime(&now_parts));
    auto till = next - now + milliseconds(100);

    {
      std::unique_lock ordersLock{ordersMutex_};
      // finish if orderbook has shutdown or shutdown signalled while waiting
      if (shutdown_.load(std::memory_order_acquire) ||
          shutdownConditionVariable_.wait_for(ordersLock, till) == std::cv_status::no_timeout)
        return;
    }

    OrderIds orderIds;

    {
      std::scoped_lock ordersLock{ordersMutex_};
      for (const auto &[order_, location_]: orders_ | std::views::values)
      {
        const auto& order = order_;

        if (order->GetOrderType() != OrderType::GoodForDay)
          continue;

        orderIds.push_back(order->GetOrderId());
      }
    }

    CancelOrders(orderIds);
  }
}

// takes the lock once for the whole batch
void OrderBook::CancelOrders(const OrderIds& orderIds)
{
  std::scoped_lock ordersLock{ordersMutex_};

  for (const auto& orderId : orderIds)
    CancelOrderInternal(orderId);
}

// O(1): the stored iterator erases straight from the level's list
void OrderBook::CancelOrderInternal(const OrderId orderId)
{
  if (!orders_.contains(orderId))
    return;

  const auto [order, it] = orders_.at(orderId);
  orders_.erase(orderId);

  // bids_ and asks_ are different types (different comparators), hence the generic lambda

  auto eraseOrderFromPriceLevel = [&](auto& orders, Price price)
  {
    auto& level = orders.at(price);
    level.erase(it);
    if (level.empty())
      orders.erase(price);
  };

  if (order->GetSide() == Side::Sell)
    eraseOrderFromPriceLevel(asks_, order->GetPrice());
  else
    eraseOrderFromPriceLevel(bids_, order->GetPrice());
}

// level data bookkeeping - caller must hold ordersMutex_
void OrderBook::OnOrderCancelled(const OrderPointer& order)
{
  UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
}

void OrderBook::OnOrderAdded(const OrderPointer& order)
{
  UpdateLevelData(order->GetPrice(), order->GetInitialQuantity(), LevelData::Action::Add);
}

void OrderBook::OnOrderMatched(const Price price, const Quantity quantity)
{
  UpdateLevelData(price, quantity, LevelData::Action::Remove);
}

void OrderBook::UpdateLevelData(const Price price, const Quantity quantity, const LevelData::Action action)
{
  auto&[quantity_] = data_[price];

  if (action == LevelData::Action::Remove)
  {
    quantity_ -= quantity;
  }
  else
  {
    quantity_ += quantity;
  }

  if (quantity_ == 0)
    data_.erase(price);
}

Trades OrderBook::AddOrder(const OrderPointer& order)
{
  std::scoped_lock ordersLock{ordersMutex_};

  if (orders_.contains(order->GetOrderId()))
    return {};

  // market order: reprice to the worst opposite level so it sweeps the book
  if (order->GetOrderType() == OrderType::Market)
  {
    if (order->GetSide() == Side::Buy && !asks_.empty())
    {
      const auto& [worstAsk, _] = *asks_.rbegin();
      order->ToGoodTillCancel(worstAsk);
    }
    else if (order->GetSide() == Side::Sell && !bids_.empty())
    {
      const auto& [worstBid, _] = *bids_.rbegin();
      order->ToGoodTillCancel(worstBid);
    }
    else
      return {}; // should we return error instead here?
  }

  if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
    return {};
  if (order->GetOrderType() == OrderType::FillOrKill &&
      !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity()))
    return {};

  OrderPointers::iterator iterator;

  if (order->GetSide() == Side::Buy)
  {
    auto& orders = bids_[order->GetPrice()];
    orders.push_back(order);
    iterator = std::prev(orders.end());
  }
  else
  {
    auto& orders = asks_[order->GetPrice()];
    orders.push_back(order);
    iterator = std::prev(orders.end());
  }

  orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});

  OnOrderAdded(order);

  return MatchOrders();
}

void OrderBook::CancelOrder(const OrderId orderId)
{
  std::scoped_lock ordersLock{ordersMutex_};

  CancelOrderInternal(orderId);
}

// modify = cancel + re-add, so the order loses its time priority
Trades OrderBook::ModifyOrder(const OrderModify &order)
{
  OrderType orderType;
  {
    std::scoped_lock ordersLock{ordersMutex_};

    if (!orders_.contains(order.GetOrderId()))
      return {};

    const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
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

bool OrderBook::Contains(const OrderId orderId) const
{
  std::scoped_lock ordersLock{ordersMutex_};
  return orders_.contains(orderId);
}

OrderBookLevelInfos OrderBook::GetOrderInfos() const
{
  LevelInfos bidInfos, askInfos;
  bidInfos.reserve(orders_.size());
  askInfos.reserve(orders_.size());

  auto CreateLevelInfos = [](const Price price, const OrderPointers& orders)
  {
    return LevelInfo{price, std::accumulate(orders.begin(), orders.end(), static_cast<Quantity>(0),
                                            [](const Quantity runningSum, const OrderPointer& order)
                                            { return runningSum + order->GetRemainingQuantity(); })};
  };

  for (const auto& [price, orders] : bids_)
    bidInfos.push_back(CreateLevelInfos(price, orders));

  for (const auto& [price, orders] : asks_)
    askInfos.push_back(CreateLevelInfos(price, orders));

  return OrderBookLevelInfos{bidInfos, askInfos};
}

bool OrderBook::CanFullyFill(const Side side, const Price price, Quantity quantity) const
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

  for (const auto& [levelPrice, levelData] : data_)
  {
    if (threshold.has_value() && ((side == Side::Buy && threshold.value() > levelPrice) ||
                                  (side == Side::Sell && threshold.value() < levelPrice)))
      continue;

    if ((side == Side::Buy && levelPrice > price) || (side == Side::Sell && levelPrice < price))
      continue;

    if (quantity <= levelData.quantity_)
      return true;

    quantity -= levelData.quantity_;
  }

  return false;
}

// would an order at this price cross the best opposite level?
bool OrderBook::CanMatch(const Side side, const Price price) const
{
  if (side == Side::Buy)
  {
    if (asks_.empty())
      return false;

    const auto& [bestAsk, _] = *asks_.begin();
    return price >= bestAsk;
  }
  else
  {
    if (bids_.empty())
      return false;

    const auto& [bestBid, _] = *bids_.begin();
    return price <= bestBid;
  }
}

Trades OrderBook::MatchOrders()
{
  Trades trades;
  trades.reserve(orders_.size()); // over-reserves, see TODO.md perf step 3
  while (true)
  {
    if (bids_.empty() || asks_.empty())
      break;
    auto& [bidPrice, bids] = *bids_.begin();
    auto& [askPrice, asks] = *asks_.begin();
    if (bidPrice < askPrice)
      break;
    while (bids.size() && asks.size())
    {
      // copies keep the orders alive after pop_front
      auto bid = bids.front();
      auto ask = asks.front();

      Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

      bid->Fill(quantity);
      ask->Fill(quantity);

      // TODO: duplicated cleanup, share with CancelOrderInternal
      if (bid->IsFilled())
      {
        bids.pop_front();
        orders_.erase(bid->GetOrderId());
      }

      if (ask->IsFilled())
      {
        asks.pop_front();
        orders_.erase(ask->GetOrderId());
      }

      trades.push_back(Trade{TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity},
                             TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity}});

      OnOrderMatched(bid->GetPrice(), quantity);
      OnOrderMatched(ask->GetPrice(), quantity);
    }
    // bidPrice/askPrice reference the map node's key - copy before erase(), which frees it
    if (bids.empty())
    {
      const Price bidPriceToErase = bidPrice;
      bids_.erase(bidPriceToErase);
      data_.erase(bidPriceToErase);
    }

    if (asks.empty())
    {
      const Price askPriceToErase = askPrice;
      asks_.erase(askPriceToErase);
      data_.erase(askPriceToErase);
    }
  }
  if (!bids_.empty())
  {
    auto& [_, bids] = *bids_.begin();
    if (const auto& order = bids.front(); order->GetOrderType() == OrderType::FillAndKill)
      CancelOrderInternal(order->GetOrderId());
  }
  if (!asks_.empty())
  {
    auto& [_, asks] = *asks_.begin();
    if (const auto& order = asks.front(); order->GetOrderType() == OrderType::FillAndKill)
      CancelOrderInternal(order->GetOrderId());
  }
  return trades;
}

// start thread that cancels goodForDay orders at end of day
OrderBook::OrderBook() : ordersPruneThread_{[this] { PruneGoodForDayOrders(); }} {};

OrderBook::~OrderBook()
{
  shutdown_.store(true, std::memory_order_release);
  shutdownConditionVariable_.notify_one();
  ordersPruneThread_.join();
}
