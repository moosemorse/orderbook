#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include "OrderBook.hpp"
#include "OrderGenerator.hpp"

namespace
{
constexpr std::size_t kOps = 110'000;
constexpr std::size_t kCheckEvery = 1'000;

// If a change is *meant* to alter matching behaviour, rerun with UPDATE_SNAPSHOT=1 and say so in the commit.
// <random> distributions are implementation-defined, so the snapshot is libstdc++-specific.
constexpr const char *kSnapshotPath = REGRESSION_SNAPSHOT_PATH;
constexpr const char *kActualPath = "regression_snapshot.actual.txt";

testing::AssertionResult CheckInvariants(const OrderBook &book)
{
  const auto infos = book.GetOrderInfos();
  const auto &bids = infos.GetBids();
  const auto &asks = infos.GetAsks();

  if (!bids.empty() && !asks.empty() && bids.front().price_ >= asks.front().price_)
    return testing::AssertionFailure() << "book is crossed (best bid >= best ask)";
  if (book.Size() < bids.size() + asks.size())
    return testing::AssertionFailure() << "more price levels than resting orders";
  for (const auto *levels : {&bids, &asks})
    for (const LevelInfo &level : *levels)
      if (level.quantity_ == 0)
        return testing::AssertionFailure() << "empty price level left in the book at " << level.price_;

  return testing::AssertionSuccess();
}
} // namespace

TEST(Regression, SeededReplayMatchesSnapshot)
{
  OrderGenerator generator{GeneratorConfig{}};
  std::vector<Op> script;
  generator.Generate(kOps, script);

  OrderBook book;
  std::uint64_t tradeCount = 0;
  std::uint64_t tradedQuantity = 0;

  for (std::size_t i = 0; i < script.size(); ++i)
  {
    for (const Trade &trade : Dispatch(book, script[i]))
    {
      ++tradeCount;
      tradedQuantity += trade.GetBidTrade().quantity_;
    }

    if ((i + 1) % kCheckEvery == 0)
    {
      ASSERT_TRUE(CheckInvariants(book)) << "after op " << i;
    }
  }

  ASSERT_TRUE(CheckInvariants(book)) << "at end of run";

  std::ostringstream actual;
  actual << "# trades=" << tradeCount << " traded_qty=" << tradedQuantity << '\n';
  book.Print(actual);

  if (std::getenv("UPDATE_SNAPSHOT"))
  {
    std::ofstream{kSnapshotPath} << actual.str();
    return;
  }

  std::ostringstream expected;
  expected << std::ifstream{kSnapshotPath}.rdbuf();

  if (actual.str() != expected.str())
  {
    std::ofstream{kActualPath} << actual.str();
    FAIL() << "book differs from the snapshot, see: diff " << kSnapshotPath << " build/" << kActualPath;
  }
}
