#pragma once

#include <limits>

#include "Aliases.hpp"

struct Constants
{
    static constexpr Price InvalidPrice = std::numeric_limits<Price>::min(); // integer types have no NaN
};