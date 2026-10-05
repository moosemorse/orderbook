#pragma once
// Generates a weighted stream of orderbook events. Ops are replayed against a
// shadow OrderBook as they're generated, so cancels/modifies only ever target
// orders that are still resting (not ones already filled by a crossing order).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

#include "Aliases.hpp"
#include "OrderBook.hpp"
#include "OrderModify.hpp"
#include "OrderType.hpp"
#include "Side.hpp"

enum class OpType
{
  Add,
  Cancel,
  Modify,
};

inline constexpr std::array<OpType, 3> kOpTypes{OpType::Add, OpType::Cancel, OpType::Modify};

constexpr const char *ToString(OpType type)
{
  switch (type)
  {
  case OpType::Add:
    return "Add";
  case OpType::Cancel:
    return "Cancel";
  case OpType::Modify:
    return "Modify";
  }
  return "?";
}

struct Op
{
  OpType type;
  OrderId orderId;
  Side side;
  OrderType orderType; // only meaningful for Add
  Price price;         // unused for Cancel
  Quantity quantity;   // unused for Cancel
};

// make_shared<Order> is deliberately part of this (and so timed by the
// harness) - it's a real per-call cost today, and the one a memory pool would remove.
inline Trades Dispatch(OrderBook &book, const Op &op)
{
  switch (op.type)
  {
  case OpType::Add:
    return book.AddOrder(op.orderType == OrderType::Market
                             ? std::make_shared<Order>(op.orderId, op.side, op.quantity)
                             : std::make_shared<Order>(op.orderType, op.orderId, op.side, op.price, op.quantity));
  case OpType::Cancel:
    book.CancelOrder(op.orderId);
    return {};
  case OpType::Modify:
    return book.ModifyOrder(OrderModify{op.orderId, op.side, op.price, op.quantity});
  }
  return {};
}

// how many generated cancels/modifies targeted an order that was actually
// resting in the shadow book - anything under 100% means the generator's
// view of the book has drifted and the workload is partly no-ops
struct GeneratorStats
{
  std::size_t cancels{};
  std::size_t cancelHits{};
  std::size_t modifies{};
  std::size_t modifyHits{};
};

struct GeneratorConfig
{
  std::uint64_t seed = 42;
  std::size_t targetDepth = 5000; // steady-state resting orders, both sides combined
  double cancelRatio = 0.2;       // of free-choice ops (i.e. when depth is in-band)
  double modifyRatio = 0.1;
  Price midPrice = 10'000;
  Price tickSpread = 25; // stddev, in ticks, of price offset from mid
  Price midDrift = 1;    // max +-1 tick random walk per generated op
};

class OrderGenerator
{
public:
  explicit OrderGenerator(GeneratorConfig config)
      : config_{config}, rng_{config.seed}, mid_{config.midPrice},
        orderTypeDist_{BuildOrderTypeWeights()}
  {
  }

  // Appends `count` ops to `out`, evolving internal generator state.
  void Generate(std::size_t count, std::vector<Op> &out)
  {
    out.reserve(out.size() + count);

    const auto lowerBound = static_cast<std::size_t>(static_cast<double>(config_.targetDepth) * 0.8);
    const auto upperBound = static_cast<std::size_t>(static_cast<double>(config_.targetDepth) * 1.2);
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    for (std::size_t i = 0; i < count; ++i)
    {
      OpType choice;
      if (restingPool_.size() < lowerBound)
        choice = OpType::Add;
      else if (restingPool_.size() > upperBound)
        choice = OpType::Cancel;
      else
      {
        const double r = unit(rng_);
        if (r < config_.cancelRatio)
          choice = OpType::Cancel;
        else if (r < config_.cancelRatio + config_.modifyRatio)
          choice = OpType::Modify;
        else
          choice = OpType::Add;
      }

      if ((choice == OpType::Cancel || choice == OpType::Modify) && restingPool_.empty())
        choice = OpType::Add;

      Op op{};
      switch (choice)
      {
      case OpType::Add:
        op = MakeAdd();
        break;
      case OpType::Cancel:
        op = MakeCancel();
        ++stats_.cancels;
        stats_.cancelHits += shadow_.Contains(op.orderId);
        break;
      case OpType::Modify:
        op = MakeModify();
        ++stats_.modifies;
        stats_.modifyHits += shadow_.Contains(op.orderId);
        break;
      }

      ApplyToShadow(op);
      out.push_back(op);
    }
  }

  const GeneratorStats &Stats() const { return stats_; }

private:
  struct RestingOrder
  {
    OrderId id;
    Side side;
  };

  struct OrderTypeWeight
  {
    OrderType type;
    int weight;
  };

  static constexpr std::array<OrderTypeWeight, 5> kOrderTypeWeights{{
      {OrderType::GoodTillCancel, 90},
      {OrderType::FillAndKill, 5},
      {OrderType::FillOrKill, 3},
      {OrderType::Market, 1},
      {OrderType::GoodForDay, 1},
  }};

  static std::discrete_distribution<std::size_t> BuildOrderTypeWeights()
  {
    std::vector<double> weights;
    weights.reserve(kOrderTypeWeights.size());
    for (const auto &entry : kOrderTypeWeights)
      weights.push_back(static_cast<double>(entry.weight));
    return std::discrete_distribution<std::size_t>(weights.begin(), weights.end());
  }

  // Keeps restingPool_ in sync with what's really resting: drops orders a
  // trade filled, and adds the new order only if the book kept it.
  void ApplyToShadow(const Op &op)
  {
    for (const Trade &trade : Dispatch(shadow_, op))
    {
      for (const OrderId id : {trade.GetBidTrade().orderId_, trade.GetAskTrade().orderId_})
        if (!shadow_.Contains(id))
          RemoveFromPool(id);
    }

    if (op.type == OpType::Add && shadow_.Contains(op.orderId))
      AddToPool(op.orderId, op.side);
  }

  void AddToPool(OrderId id, Side side)
  {
    poolIndex_[id] = restingPool_.size();
    restingPool_.push_back(RestingOrder{id, side});
  }

  // O(1) swap-with-back removal; no-op if id isn't pooled
  void RemoveFromPool(OrderId id)
  {
    const auto it = poolIndex_.find(id);
    if (it == poolIndex_.end())
      return;

    const std::size_t idx = it->second;
    const RestingOrder moved = restingPool_.back();
    restingPool_[idx] = moved;
    poolIndex_[moved.id] = idx;
    restingPool_.pop_back();
    poolIndex_.erase(id);
  }

  OrderType SampleOrderType() { return kOrderTypeWeights[orderTypeDist_(rng_)].type; }

  Side SampleSide() { return sideDist_(rng_) ? Side::Buy : Side::Sell; }

  Quantity SampleQuantity() { return static_cast<Quantity>(quantityDist_(rng_)); }

  void DriftMid()
  { 
    std::uniform_int_distribution<Price> step(-config_.midDrift, config_.midDrift);
    const Price next = static_cast<Price>(mid_ + step(rng_));
    const Price lowerClamp = config_.midPrice / 2;
    const Price upperClamp = config_.midPrice * 2;
    mid_ = std::clamp(next, lowerClamp, upperClamp);
  }

  // passive: priced away from the touch (rests). Otherwise priced to cross
  // (aggressive - matches how FillAndKill/FillOrKill/Market are meant to be used).
  Price PricedFor(Side side, bool passive)
  {
    std::normal_distribution<double> offsetDist(0.0, static_cast<double>(config_.tickSpread));
    const auto offset = static_cast<Price>(std::abs(offsetDist(rng_))) + 1;

    if (passive)
      return side == Side::Buy ? static_cast<Price>(mid_ - offset) : static_cast<Price>(mid_ + offset);
    return side == Side::Buy ? static_cast<Price>(mid_ + offset) : static_cast<Price>(mid_ - offset);
  }

  Op MakeAdd()
  {
    DriftMid();
    const OrderType type = SampleOrderType();
    const Side side = SampleSide();
    const bool passive = (type == OrderType::GoodTillCancel || type == OrderType::GoodForDay);
    const Price price = PricedFor(side, passive);
    const Quantity quantity = SampleQuantity();
    const OrderId id = nextOrderId_++;
    return Op{OpType::Add, id, side, type, price, quantity};
  }

  Op MakeCancel()
  {
    std::uniform_int_distribution<std::size_t> pick(0, restingPool_.size() - 1);
    const OrderId id = restingPool_[pick(rng_)].id;
    RemoveFromPool(id);
    return Op{OpType::Cancel, id, Side::Buy, OrderType::GoodTillCancel, 0, 0};
  }

  Op MakeModify()
  {
    // real modifies reprice/resize an order, they don't flip its side
    std::uniform_int_distribution<std::size_t> pick(0, restingPool_.size() - 1);
    const auto [id, side] = restingPool_[pick(rng_)];
    DriftMid();
    const Price price = PricedFor(side, /*passive=*/true);
    const Quantity quantity = SampleQuantity();
    return Op{OpType::Modify, id, side, OrderType::GoodTillCancel, price, quantity};
  }

  GeneratorConfig config_;
  std::mt19937_64 rng_;
  Price mid_;
  std::discrete_distribution<std::size_t> orderTypeDist_;
  std::bernoulli_distribution sideDist_{0.5};
  std::uniform_int_distribution<std::uint32_t> quantityDist_{1, 100};
  OrderId nextOrderId_{1};
  std::vector<RestingOrder> restingPool_;
  std::unordered_map<OrderId, std::size_t> poolIndex_; // id -> index in restingPool_
  OrderBook shadow_;
  GeneratorStats stats_;
};
