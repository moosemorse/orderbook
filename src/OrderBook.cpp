#include "OrderBook.hpp"

#include <chrono>
#include <ctime>
#include <numeric>
#include <ostream>
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
      // the predicate re-checks shutdown_, so a spurious wakeup goes back to waiting instead of exiting
      if (shutdownConditionVariable_.wait_for(ordersLock, till,
                                              [this] { return shutdown_.load(std::memory_order_acquire); }))
        return;
    }

    OrderIds orderIds;

    {
      std::scoped_lock ordersLock{ordersMutex_};
      for (const auto& [order, _] : orders_ | std::views::values)
        if (order->GetOrderType() == OrderType::GoodForDay)
          orderIds.push_back(order->GetOrderId());
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

  OnOrderCancelled(order);
}

// level data bookkeeping - caller must hold ordersMutex_
void OrderBook::OnOrderCancelled(const OrderPointer& order)
{
  UpdateLevelData(order->GetSide(), order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
}

void OrderBook::OnOrderAdded(const OrderPointer& order)
{
  UpdateLevelData(order->GetSide(), order->GetPrice(), order->GetInitialQuantity(), LevelData::Action::Add);
}

void OrderBook::OnOrderMatched(const Side side, const Price price, const Quantity quantity)
{
  UpdateLevelData(side, price, quantity, LevelData::Action::Remove);
}

void OrderBook::UpdateLevelData(const Side side, const Price price, const Quantity quantity, const LevelData::Action action)
{
  auto& data = side == Side::Buy ? bidData_ : askData_;
  auto&[quantity_] = data[price];

  if (action == LevelData::Action::Remove)
  {
    quantity_ -= quantity;
  }
  else
  {
    quantity_ += quantity;
  }

  if (quantity_ == 0)
    data.erase(price);
}

Trades OrderBook::AddOrder(const OrderPointer& order)
{
  std::scoped_lock ordersLock{ordersMutex_};

  if (orders_.contains(order->GetOrderId()))
    return {};

  // market order: reprice to the worst opposite level so it sweeps the book
  const bool isMarket = order->GetOrderType() == OrderType::Market;
  if (isMarket)
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

  Trades trades = MatchOrders();

  // FillAndKill and market orders never rest: cancel whatever didn't fill
  if ((isMarket || order->GetOrderType() == OrderType::FillAndKill) && !order->IsFilled())
    CancelOrderInternal(order->GetOrderId());

  return trades;
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
  std::scoped_lock ordersLock{ordersMutex_};

  // returns a vector of price level : quantities
  // e.g. [{100, 15}, {99, 7}] so at price 100, there's 15 units
  const auto toLevelInfos = [](const auto& levels)
  {
    LevelInfos infos;
    infos.reserve(levels.size());
    for (const auto& [price, orders] : levels)
      infos.push_back(LevelInfo{price, std::accumulate(orders.begin(), orders.end(), Quantity{0},
                                                       [](const Quantity sum, const OrderPointer& order)
                                                       { return sum + order->GetRemainingQuantity(); })});
    return infos;
  };

  return OrderBookLevelInfos{toLevelInfos(bids_), toLevelInfos(asks_)};
}

// one line per resting order: best price first, time priority within a level
void OrderBook::Print(std::ostream& out) const
{
  std::scoped_lock ordersLock{ordersMutex_};

  const auto printSide = [&out](const char* name, const auto& levels)
  {
    out << name << '\n';
    for (const auto& [price, orders] : levels)
      for (const auto& order : orders)
        out << price << " id=" << order->GetOrderId() << " qty=" << order->GetRemainingQuantity() << '\n';
  };

  printSide("BIDS", bids_);
  printSide("ASKS", asks_);
}

bool OrderBook::CanFullyFill(const Side side, const Price price, Quantity quantity) const
{
  if (!CanMatch(side, price))
    return false;

  // CanMatch passed, so the opposite side isn't empty
  const Price threshold = side == Side::Buy ? asks_.begin()->first : bids_.begin()->first;

  for (const auto& [levelPrice, levelData] : side == Side::Buy ? askData_ : bidData_)
  {
    if ((side == Side::Buy && threshold > levelPrice) || (side == Side::Sell && threshold < levelPrice))
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

  // assumption: order is at front of price level
  const auto popIfFilled = [this](OrderPointers& level, const OrderPointer& order)
  {
    if (!order->IsFilled())
      return;
    level.pop_front();
    orders_.erase(order->GetOrderId());
  };

  // price is taken by value: the caller's is a reference to the map node's key, which erase() frees
  const auto eraseLevelIfEmpty = [](auto& levels, const OrderPointers& level, const Price price)
  {
    if (!level.empty())
      return;
    levels.erase(price);
  };

  while (!bids_.empty() && !asks_.empty())
  {
    auto& [bidPrice, bids] = *bids_.begin();
    auto& [askPrice, asks] = *asks_.begin();
    if (bidPrice < askPrice)
      break;

    while (!bids.empty() && !asks.empty())
    {
      // copies keep the orders alive after pop_front
      const auto bid = bids.front();
      const auto ask = asks.front();

      const Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

      bid->Fill(quantity);
      ask->Fill(quantity);

      popIfFilled(bids, bid);
      popIfFilled(asks, ask);

      trades.push_back(Trade{TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity},
                             TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity}});

      OnOrderMatched(Side::Buy, bid->GetPrice(), quantity);
      OnOrderMatched(Side::Sell, ask->GetPrice(), quantity);
    }

    eraseLevelIfEmpty(bids_, bids, bidPrice);
    eraseLevelIfEmpty(asks_, asks, askPrice);
  }

  return trades;
}

// start thread that cancels goodForDay orders at end of day
OrderBook::OrderBook() : ordersPruneThread_{[this] { PruneGoodForDayOrders(); }} {};

OrderBook::~OrderBook()
{
  {
    // under the lock so the store can't land between the prune thread's check and its wait
    std::scoped_lock ordersLock{ordersMutex_};
    shutdown_.store(true, std::memory_order_release);
  }
  shutdownConditionVariable_.notify_one();
  ordersPruneThread_.join();
}
