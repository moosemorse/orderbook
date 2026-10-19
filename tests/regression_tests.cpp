#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

#include "Fingerprint.hpp"
#include "OrderBook.hpp"
#include "OrderGenerator.hpp"

namespace
{
constexpr std::size_t kOps = 110'000;
constexpr std::size_t kCheckEvery = 1'000;

// If a change is *meant* to alter matching behaviour, update this and say so in the commit.
// <random> distributions are implementation-defined, so the value is libstdc++-specific.
constexpr std::uint64_t kExpectedFingerprint = 0x248e61fc4f9417f3;

// returns nullptr if the book is consistent, else what's wrong with it
const char *CheckInvariants(const OrderBook &book)
{
  const auto infos = book.GetOrderInfos();
  const auto &bids = infos.GetBids();
  const auto &asks = infos.GetAsks();

  if (!bids.empty() && !asks.empty() && bids.front().price_ >= asks.front().price_)
    return "book is crossed (best bid >= best ask)";
  if (book.Size() < bids.size() + asks.size())
    return "more price levels than resting orders";
  for (const auto *levels : {&bids, &asks})
    for (const LevelInfo &level : *levels)
      if (level.quantity_ == 0)
        return "empty price level left in the book";

  return nullptr;
}
} // namespace

TEST(Regression, SeededReplayMatchesFingerprint)
{
  OrderGenerator generator{GeneratorConfig{}};
  std::vector<Op> script;
  generator.Generate(kOps, script);

  OrderBook book;
  Fingerprint fingerprint;

  for (std::size_t i = 0; i < script.size(); ++i)
  {
    fingerprint.AddTrades(Dispatch(book, script[i]));

    if ((i + 1) % kCheckEvery == 0)
      if (const char *failure = CheckInvariants(book))
        FAIL() << failure << " after op " << i;
  }

  fingerprint.Finish(book);

  if (const char *failure = CheckInvariants(book))
    FAIL() << failure << " at end of run";

  EXPECT_EQ(fingerprint.Value(), kExpectedFingerprint)
      << std::hex << "got 0x" << fingerprint.Value() << " (trades=" << std::dec << fingerprint.TradeCount()
      << " traded qty=" << fingerprint.TradedQuantity() << " resting=" << fingerprint.RestingOrders() << ")";
}
