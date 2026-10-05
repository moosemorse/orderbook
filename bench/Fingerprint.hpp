#pragma once

// Deterministic summary of a run: the same script (seed + generator flags)
// must always produce the same fingerprint. A refactor that changes it has
// changed matching behaviour, which is how the bench doubles as a regression test.

#include <cstdint>
#include <initializer_list>

#include "OrderBook.hpp"

class Fingerprint
{
public:
  void AddTrades(const Trades &trades)
  {
    tradeCount_ += trades.size();
    for (const Trade &trade : trades)
      tradedQuantity_ += trade.GetBidTrade().quantity_;
  }

  // Folds the final book state into the hash; call once, after the run.
  void Finish(const OrderBook &book)
  {
    restingOrders_ = book.Size();

    const auto infos = book.GetOrderInfos();
    for (const auto *levels : {&infos.GetBids(), &infos.GetAsks()})
    {
      for (const LevelInfo &level : *levels)
      {
        Mix(levelsHash_, static_cast<std::uint64_t>(level.price_));
        Mix(levelsHash_, level.quantity_);
      }
      Mix(levelsHash_, levels->size()); // separates the bid run from the ask run
    }
  }

  std::uint64_t Value() const
  {
    std::uint64_t hash = kFnvOffset;
    Mix(hash, tradeCount_);
    Mix(hash, tradedQuantity_);
    Mix(hash, restingOrders_);
    Mix(hash, levelsHash_);
    return hash;
  }

  std::uint64_t TradeCount() const { return tradeCount_; }
  std::uint64_t TradedQuantity() const { return tradedQuantity_; }
  std::uint64_t RestingOrders() const { return restingOrders_; }

private:
  static constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
  static constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;

  // FNV-1a over the value's 8 bytes
  static void Mix(std::uint64_t &hash, std::uint64_t value)
  {
    for (int byte = 0; byte < 8; ++byte)
    {
      hash ^= (value >> (byte * 8)) & 0xff;
      hash *= kFnvPrime;
    }
  }

  std::uint64_t tradeCount_{};
  std::uint64_t tradedQuantity_{};
  std::uint64_t restingOrders_{};
  std::uint64_t levelsHash_{kFnvOffset};
};
