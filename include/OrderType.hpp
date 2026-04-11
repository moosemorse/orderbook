#pragma once

enum class OrderType
{
  GoodTillCancel, // type of limit order, filled or cancelled
  FillAndKill,    // try to fill as much, then kill
  FillOrKill,     // fully fill and if can't then kill
  GoodForDay,     // goodTillCancel - cancel at end of day
  Market,         // order at market price, agress against book
};
