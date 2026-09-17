# C++ OrderBook

Implementing an orderbook in C++ from scratch.

## Architecture
 
Orders are stored in two independent indices:
 
- `bids_` / `asks_` := `std::map` keyed by price (descending/ascending), values are `std::list<OrderPointer>` at each price level
- `orders_` := `std::unordered_map<OrderId, OrderEntry>` for O(1) lookup by ID
 
Each `OrderEntry` stores a `std::list` iterator pointing directly to the order's node in the price-level list for O(1) removal. `std::list` is used over `std::vector` specifically because its iterators are never invalidated by insertions or deletions elsewhere in the container, however this might be interesting to explore if I can use vectors instead.
 
A separate `data_` map tracks aggregate quantity and order count per price level, used by `CanFullyFill` to evaluate FillOrKill orders without iterating individual orders.
 
## Order Types
 
| Type | Behaviour |
|---|---|
| `GoodTillCancel` | Rests in the book until explicitly cancelled |
| `GoodForDay` | Automatically cancelled at 16:00 by a background pruning thread |
| `FillAndKill` | Matches immediately for whatever quantity is available, remainder cancelled |
| `FillOrKill` | Only enters the book if it can be fully filled immediately |
 
## Threading
 
A dedicated `ordersPruneThread_` runs `PruneGoodForDayOrders()` which sleeps until 16:00 using `std::condition_variable::wait_for`, then batch-cancels all `GoodForDay` orders. All public methods acquire a `std::mutex` via `std::scoped_lock`. Shutdown is signalled via an atomic `shutdown_` flag and `notify_one` on the condition variable, with the destructor joining the thread.
 
## Matching
 
`MatchOrders()` runs after every `AddOrder`. It compares `bids_.begin()` (best bid) against `asks_.begin()` (best ask) - if best bid is more than best ask, a trade is generated. Trade quantity is `min(bid remaining, ask remaining)`. Filled orders are erased from both the price-level list and the global `orders_` map. Empty price levels are pruned from the map immediately.
 
## Build

1. Clone repo
```bash
git clone git@github.com:moosemorse/orderbook.git
```

2. Run make
```bash
make          # debug build -> build/orderbook
make release  # optimised build
make clean    # remove binary 
```

Requires GCC or Clang with C++20 support, and CMake >= 3.14.

1. Find `orderbook` in `build/` directory and run with
```bash
./build/orderbook
```

1. Run the test suite with
```bash
./build/orderbook_tests
```

## Benchmarking

`orderbook_bench` (`bench/`) constructs one `OrderBook`, generates a synthetic order-flow script up front, replays it on a pinned core, and reports throughput plus median/p99/p99.9 latency (combined and broken down by op type: Add/Cancel/Modify). It's a local measurement tool only - not run in CI, since shared CI runners produce noisy, unpinned, unrepresentative numbers.

```bash
make release
./build/orderbook_bench --orders 5000000 --core 3
# or: make bench ARGS="--orders 5000000 --core 3"
```

Flags (all optional):

| Flag | Default | Meaning |
|---|---|---|
| `--orders` | 5000000 | measured ops |
| `--warmup` | 500000 | unrecorded ops replayed first, to settle allocator/tree shape |
| `--core` | 0 | core to pin to (`sched_setaffinity`) - required, not best-effort |
| `--seed` | 42 | RNG seed, for reproducible runs |
| `--target-depth` | 5000 | steady-state resting orders per side the generator aims for |
| `--cancel-ratio` / `--modify-ratio` | 0.2 / 0.1 | op mix once book depth is in-band |
| `--tick-spread` | 25 | stddev, in ticks, of order price offset from the synthetic mid |
| `--csv PATH` | (none) | dump raw per-op latencies for offline analysis/plotting |

The binary refuses to report numbers from a non-release (`NDEBUG` undefined) build - it prints a warning banner instead.

**Getting stable numbers:** pin to an isolated/quiet core if possible (`isolcpus`, or at least a core with no other load), disable turbo boost and set the `performance` CPU governor (`sudo cpupower frequency-set --governor performance`), and close other work on the machine. `taskset -c N ./build/orderbook_bench --core N` as an outer pin is a cheap extra guard on top of the binary's own affinity call.

## Todos
- [X] Chore: re-organise structure of files using guide (https://www.studyplan.dev/cmake/organizing-a-cpp-project)
- [ ] Refactor: remove duplicate code in some places (erase logic)
- [X] Test suite
- [X] Update CI for test suite

## Ideas (not immediate todos but could be fun ways to extend project):
- [ ] Use networking as a client to submit order onto orderbook e.g. develop a C++ HTTP server using crow, cpp-httplib
- [ ] Visualiser for orderbook (and each abstraction level of the order book)
- [ ] For performance: experiment with lock-free data structures, Memory pooling
- [ ] Besides unit tests, try backtest with actual market data e.g. from LOBSTER
