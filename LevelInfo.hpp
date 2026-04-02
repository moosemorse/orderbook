#pragma once

#include <vector>

#include "Aliases.hpp"

/* LevelInfo useful for public APIs to interact with state of orderbook. */
struct LevelInfo
{
  Price price_;
  Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;