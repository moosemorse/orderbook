#pragma once

#include <vector>

#include "Aliases.hpp"

struct LevelInfo
{
  Price price_;
  Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;