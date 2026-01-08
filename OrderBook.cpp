#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <deque>
#include <format>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

enum class OrderType
{
  GoodTillCancel,
  FillAndKill
};

enum class Side
{
  Buy,
  Sell
};

// some aliases for better readability
using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

/* LevelInfo useful for public APIs to interact with state of orderbook. */
struct LevelInfo
{
  Price price_;
  Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

// we want to encapsulate level infos
class OrderBookLevelInfos
{
public:
  OrderBookLevelInfos(const LevelInfos &bids, const LevelInfos &asks)
      : bids_{bids}, asks_{asks}
  {
  }

  const LevelInfos &GetBids() const
  {
    return bids_;
  }
  const LevelInfos &GetAsks() const
  {
    return asks_;
  }

private:
  LevelInfos bids_;
  LevelInfos asks_;
};

int main()
{
  return 0;
}