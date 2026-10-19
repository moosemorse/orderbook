#pragma once

#include <limits>

#include "Aliases.hpp"

struct Constants
{
    static constexpr Price InvalidPrice = std::numeric_limits<Price>::quiet_NaN();
};