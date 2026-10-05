# TODO

Roadmap toward a CV-ready "sustaining X orders/sec at Yus median / Zus p99, measured over NM synthetic orders on a pinned core" bullet point, alongside the redis-clone project.

## Ground rules for every perf step
- One step = one commit, with before/after `orderbook_bench --orders 5000000 --core 2` numbers recorded below it. The before/after delta is worth more in an interview than the final number alone.
- `ctest` must stay green, in particular `bench_fingerprint`: a fixed-seed run must reproduce the same fingerprint, which proves the optimisation didn't change matching behaviour. Only update the expected value in `CMakeLists.txt` when a change is *meant* to alter behaviour (or the generated workload), and say so in the commit.
- Ideas from `~/dev/order-book` (MIT, bozoslav/order-book): re-implement in this codebase, don't copy files - it's C++17, `int` ids, `double`-based `Price`, GTC/IOC/FOK only, and has known bugs (see `TODO2.md`). Credit it in the README as design inspiration.

## Done: benchmark harness (`feat/benchmark-harness`)
- [x] UAF fix in `MatchOrders` price-level cleanup (`6ae9340`, `6da7b2b`)
- [x] `orderbook_bench` harness, CMake/Makefile wiring, README section (`8f5bcfc`, `ba1bf91`, `816b933`)
- [x] Generator replays against a shadow `OrderBook` so cancels/modifies only target genuinely resting orders (was ~52% no-ops); new `OrderBook::Contains` (`59da94d`)
- [x] Run fingerprint + `--check HEX` + `--check-every N` invariant checks; `bench_fingerprint` ctest (`59da94d`)
- [x] `orderbook_lib` static library linked by main/bench/tests; tests no longer `#include` the `.cpp` (`59da94d`)
- [x] Bench cleanups: `OpType` everywhere, shared `kOpTypes`/`ToString`, modifies keep the order's side, `targetDepth` is both sides combined
- [ ] Merge `feat/benchmark-harness` into `main`

## Next: performance work, in this order
1. [ ] **Re-record the baseline** on a quiet machine (performance governor, nothing else running): `make release && ./build/orderbook_bench --orders 5000000 --core 2`. Old baseline (5.70M/s, Cancel p50=68ns, Modify p50=76ns) was on the skewed workload - discard it. First corrected run, not under quiet conditions: 4.07M orders/sec, Add p50=175ns, Cancel p50=134ns, Modify p50=307ns.
   - Record here: _(pending)_
2. [ ] **Profile** before changing anything: build `RelWithDebInfo`, `perf record -g ./build/orderbook_bench --orders 2000000 --core 2`, `perf report`; plus `perf stat -e cache-misses,instructions,cycles`. Note where time actually goes (mutex, `shared_ptr` alloc, `std::map`/`std::list` nodes, `MatchOrders`).
3. [ ] **Suspected quick win:** `MatchOrders` does `trades.reserve(orders_.size())` on every call - reserves ~5000 `Trade`s (~160KB) per Add although most adds produce 0-1 trades. Confirm in the profile, then reserve nothing/a small constant. Behaviour unchanged, so fingerprint must match.
4. [ ] **Housekeeping before the architecture changes** (cheaper now; fingerprint is the safety net):
   - [ ] Remove duplicate erase logic in `OrderBook.cpp` (order cleanup written out in several places)
   - [ ] Input validation in `AddOrder`: reject zero quantity (duplicate ids are already rejected); reject rather than silently clamp/no-op
5. [ ] **Memory pool + index-linked list** for `Order`, replacing `shared_ptr<Order>` + `std::list` per level. Design reference: `order-book`'s `OrderPool` + `OrderQueue` (`include/OrderPool.h`, `include/OrderQueue.h`). Fix their flaws:
   - placement-new reuses a slot without destroying the previous `Order` - decide up front whether pooled `Order` stays trivially destructible or the pool calls the destructor
   - `nodes.emplace_back` can reallocate and invalidate `Node&` held across `allocate` - use fixed capacity or never hold references across allocation
6. [ ] **Direct-mapped price-level array + occupancy bitmap**, replacing `std::map<Price, ...>` and the `data_` map. Design reference: `order-book`'s `PriceLevelArray<N>` (`include/PriceArray.h`), `__builtin_ctzll`/`clzll` to skip empty levels.
   - needs a bounded price range: generator clamps mid to [mid/2, 2*mid] = 5000..20000 (~15k levels)
   - *reject* out-of-range prices (they silently clamp - wrong trade prices)
   - per-level totals replace `data_` and must be the single source of truth for "level is empty" (they track it in two places)
7. [ ] **Compiler flags** as their own commit: `-O3 -march=native -flto`, default `CMAKE_BUILD_TYPE` to Release. Skip `-ffast-math` (no floating point in the hot path). Separate commit so compiler gains aren't confused with code gains.
8. [ ] **Drop `ordersMutex_`**: single-writer engine thread fed by a lock-free SPSC/MPSC ingress queue (the legitimate "lock-free" claim - a genuinely lock-free concurrent price-time-priority book isn't tractable). GoodForDay pruning becomes a command through the queue instead of a thread taking the lock. Not in `order-book` - this part is our own.
   - Pin the engine thread and the producer thread to *separate* cores (reuse `bench/Affinity.hpp`'s `PinToCore`) - here pinning is a real latency feature (warm caches, no migration, fewer tail spikes), not just benchmark hygiene. Sharing a core turns every enqueue into a context switch.

## Bench extras (deferred, optional)
- [ ] Report timer overhead: calibrated empty-timed-region p50, so readers can subtract the ~15-25ns `steady_clock::now()` cost
- [ ] Untimed pass for headline throughput (current figure includes timer calls + recording)
- [ ] `--price-dist` (normal/uniform/Student-t/lognormal, as in `order-book`'s bench) for a README table
- [ ] Adapter to run our generated script against `order-book`'s engine - their README's 42ns p50 is on a different workload, so this is the only fair comparison
- [ ] Note: `bench_fingerprint` could flake if a run spans 16:00 local (GoodForDay prune thread). Goes away with step 8

## Validation / credibility
- [ ] LOBSTER data parser + replay harness: validates matching output against real exchange data, and doubles as a second, more realistic benchmark dataset than pure synthetic flow
- [ ] README: architecture diagram + a results table/graph (one row per perf step above), with exact reproduction steps (CPU, `--core` command, compiler flags); credit `order-book` for the pool/price-array designs

## Lower priority / demo-only (don't let these crowd out the above)
- [ ] HTTP API (crow or cpp-httplib) as a thin layer over `OrderBook`, kept out of the latency-critical benchmark path
- [ ] Web visualiser (order book depth chart, live trade ticker) off JSON snapshots from the HTTP API

## Housekeeping (not blocking)
- [ ] Rework `tests/orderbook_tests.cpp` - current structure was a first GTest exercise and doesn't need to stay as-is
