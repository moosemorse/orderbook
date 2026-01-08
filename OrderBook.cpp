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

int main()
{
  return 0;
}