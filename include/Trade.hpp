#pragma once

#include "TradeInfo.hpp"

// an order matched is the aggregation of two trade info objects (for ask and bid)
class Trade
{
public:
  // TODO, define candidates by compiler
  // separate this into module?
  Trade(const TradeInfo &bidTrade, const TradeInfo &askTrade)
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

// note: one order can sweep many orders
using Trades = std::vector<Trade>;