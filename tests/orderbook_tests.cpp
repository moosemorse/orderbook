#include "../pch.h"

#include "../src/OrderBook.cpp"

namespace googletest = ::testing;

enum class ActionType
{
  Add,
  Modify,
  Cancel,
};

struct Information
{
  ActionType type_;
  OrderType orderType_;
  Side side_;
  Price price_;
  Quantity quantity_;
  OrderId orderId_;
}

using Informations = std::vector<Information>;

struct Result
{
  // if more robust compare 2 vectors of orders
  std::size_t allCount;
  std::size_t bidCount;
  std::size_t askCount;
};

struct InputHandler
{
private:
  std::uint32_t ToNumber(const std::string_view& str) const
  {
    std::int64_t value{};
    std::from_chars(str.data(), str.data() + str.size(), value);
    if (value < 0)
      throw std::logic_error("Value is below 0");
    return static_cast<std::uint64_t>(value);
  }

  bool TryParseResult(const std::string_view& str, Result& result) const
  {
    if (str.at(0) != 'R')
      return false;

    auto values = Split(str, ' ');
    result.allCount_ = ToNumber(values.at(1));
    result.bidCount_ = ToNumber(values.at(2));
    result.askCount_ = ToNumber(values.at(3));

    return true;
  }

  bool TryParseInformation(const std::string_view& str, Information& info) const
  {
    auto value = str.at(0);
    auto values = Split(str, ' ');
    if (value == 'A')
    {
      info.type_ = ActionType::Add;
      info.side_ = ParseSide(values.at(1));
      info.orderType_ = ParseOrderType(values.at(2));
      info.price_ = ParsePrice(values.at(3));
    }
    else if (value == 'M')
    {
    }
    else if (value == 'C')
    {
    }
    else
      return false;

    return true;
  }
}
