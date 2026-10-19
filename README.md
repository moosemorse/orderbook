# C++ OrderBook

A limit order book and matching engine in C++20, with price-time priority.

## How it works

- **Two books of price levels.** `bids_` and `asks_` are `std::map<Price, std::list<OrderPointer>>`. `bids_` uses `std::greater` and `asks_` uses `std::less`, so `begin()` is the best price on both sides: the highest bid and the lowest ask.
- **Orders** are held as `OrderPointer` (`std::shared_ptr<Order>`), because the same order lives in its price level and in `orders_`.
- **`orders_`** maps an order id to `{order, iterator into its price level's list}`. Cancelling uses the stored iterator to erase the order directly, with no search through the level.
- **`data_`** tracks the total quantity at each price, so `FillOrKill` can check whether it would fully fill without walking individual orders.
- **Matching** runs after every add. While the best bid is at or above the best ask, the orders at the front of each level trade `min(bid remaining, ask remaining)`. Filled orders and empty levels are removed.
- **Threading.** Every public method takes one `std::mutex`. A background thread sleeps until 16:00 and then cancels all `GoodForDay` orders.

"Resting" means sitting in the book waiting to be matched.

### Order types

| Type | Behaviour |
|---|---|
| `GoodTillCancel` | Rests until cancelled |
| `GoodForDay` | Rests until cancelled, or until 16:00 |
| `FillAndKill` | Fills what it can immediately, then the rest is cancelled |
| `FillOrKill` | Only accepted if it can be fully filled immediately |
| `Market` | Sweeps the opposite side. Rejected if that side is empty |

## Build

Requires GCC or Clang with C++20 and CMake >= 3.14.

```bash
git clone git@github.com:moosemorse/orderbook.git
cd orderbook
make          # debug build into build/
make release  # optimised build
make clean    # remove build/
```

`make` wraps CMake. The root `CMakeLists.txt` sets C++20 and builds `src/OrderBook.cpp` as `orderbook_lib`. `src/`, `bench/` and `tests/` each have their own `CMakeLists.txt`, and the tests one fetches GoogleTest.

## Tests

```bash
make
ctest --test-dir build --output-on-failure
```

To run the GTest binary directly, run it from `build/`, because the test files are loaded relative to the working directory:

```bash
cd build && ./orderbook_tests --gtest_filter='Regression*'
```

- **End-to-end tests** (`tests/orderbook_tests.cpp`): each file in `tests/TestFolder/` (e.g. `Match_GoodTillCancel.txt`) is a script of order actions plus the expected result, parsed and replayed against a fresh book.
- **Regression (golden master) test** (`tests/regression_tests.cpp`): replays a fixed-seed script of 110k ops from the benchmark's generator, checks book invariants every 1000 ops, and compares a hash of the trades and the final book (`bench/Fingerprint.hpp`) against `kExpectedFingerprint`. A refactor must leave the hash unchanged. If a change is meant to alter matching, update the value and say so in the commit.

CI builds and runs `ctest` on every push and pull request to `main`.

## Benchmark

`orderbook_bench` measures single-operation latency of the book's API: add, cancel and modify.

```bash
make bench ARGS="--orders 5000000 --core 3"
# or: make release && ./build/orderbook_bench --orders 5000000 --core 3
```

| Flag | Default | Meaning |
|---|---|---|
| `--orders` | 5000000 | measured ops |
| `--warmup` | 500000 | untimed ops replayed first |
| `--core` | 0 | core to pin the thread to |

- **Input.** A seeded generator (`bench/OrderGenerator.hpp`) builds the whole script before timing starts. It keeps the book at about 5000 resting orders: below the band it adds, above it it cancels, and in between it picks a seeded mix of adds, cancels and modifies. Prices drift around a moving mid. While generating, every op is replayed on a second copy of the book, so cancels and modifies only target orders that are still resting.
- **Timed region.** `steady_clock::now()` is called just before and after each API call. The measured time covers allocating the order (`make_shared`), taking the mutex, matching, and building and destroying the returned trades vector.
- **Output.** p50, p90, p99, p99.9, max and mean, per op type and combined. Throughput is measured ops divided by wall-clock time.

Only trust numbers from a release build, on a machine that's otherwise idle.

## Performance

- **Thread pinning** (`bench/Affinity.hpp`). The bench pins itself to one core. Linux can otherwise move the thread to another core partway through a run, which leaves the caches cold and causes p99 spikes. Pinning keeps caches warm and makes runs comparable.
- **O(1) cancel.** The list iterator stored in `orders_` erases an order without searching its price level. `std::list` is used because its iterators stay valid when other elements are inserted or erased.
- **Best price in O(1).** It's always `begin()` of the ordered map. Adding a new price level is O(log n).
- **`FillOrKill` check** uses the per-price totals in `data_` instead of walking individual orders.
- **Clean measurement.** The script is generated before timing, warmup ops settle the caches and allocator first, latency buckets are preallocated so recording never allocates, and results are reported as percentiles because the tail matters more than the mean.
- **Stable numbers.** Use a quiet core if you can (`isolcpus`, or at least nothing else running on it), set the `performance` governor (`sudo cpupower frequency-set --governor performance`) and turn off turbo boost.

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
