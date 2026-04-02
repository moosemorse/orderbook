#pragma once

// some aliases for better readability
using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

// because we are storing one Order in multiple data structures in our order book
// we're going to want to use reference semantics (orders dict and ask/bids dict)
using OrderPointer = std::shared_ptr<Order>;

// list for order data structure, because a list gives an iterator which cannot
// be invalidated no matter how large it grows (alternative is vector, TODO?)
using OrderPointers = std::list<OrderPointer>;

// note: one order can sweep many orders
using Trades = std::vector<Trade>;

using LevelInfos = std::vector<LevelInfo>;