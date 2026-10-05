# TODO2 — Learning roadmap + improvement notes

Captured from a session studying `order-book` (the sibling array/bitmap/pool-based
matching engine at `~/dev/order-book`) as a learning reference for this project.
Two parts: (1) what to go learn to actually understand *why* that repo — and this
one — are built the way they are, and (2) concrete improvement/next-step items,
some lifted from a code review of `order-book` that apply here too. Doesn't
replace `TODO.md` (the perf-work roadmap); this is background + a pitfalls list
to check against while executing it.

## 1. What to learn

### C++ mechanics
- [ ] Class templates + non-type template params (`order-book`'s `PriceLevelArray<N>`) — needed to read/write anything parameterized on a compile-time size.
- [ ] Object lifetime & placement `new` — `order-book`'s `OrderPool::allocate()` reuses storage via placement-new without destructing the old object first. Relevant here because `TODO.md` plans a memory pool for `Order` — read up on this *before* writing it, see improvement item below.
- [ ] `std::array` vs `std::vector` vs `std::map`/`std::list` tradeoffs — this project uses `std::map`+`std::list`+`shared_ptr`; `order-book` uses a fixed array + intrusive index-linked list + pool. Understand *why* each choice was made, not just that they differ.

### Domain
- [ ] Price-time priority matching, order types, FOK/IOC pre-trade liquidity checks (`order-book`'s `addOrder` FOK branch) vs this project's `FillOrKill`/`CanFullyFill` — compare the two implementations side by side once both are fresh in mind.

### Data-structure tricks (the reason to study `order-book` specifically)
- [ ] Direct-mapped array + offset instead of `std::map<Price, ...>` for a bounded price range — O(1) level lookup vs O(log n). Understand when this tradeoff is worth it (bounded, dense price domain) vs when a map is still correct (this project currently has no price bound).
- [ ] Bitmap-over-array + `__builtin_ctzll`/`__builtin_clzll` to skip empty price levels in ~O(1) instead of scanning — read up on bitset-scanning tricks (Hacker's Delight, or any allocator/scheduler bitmap implementation) before attempting anything similar here.
- [ ] Intrusive linked list over an index-based object pool (arena allocator pattern) — this is the direct precursor to the "memory pool for `Order`" item already in `TODO.md`. *Game Programming Patterns* (Nystrom, free online) has an approachable Object Pool chapter.

### Performance engineering
- [ ] Compiler flags: `-O3 -march=native -flto -ffast-math -funroll-loops`, LTO/IPO — know what each does and its tradeoffs (`-ffast-math` breaks strict IEEE compliance; `-march=native` breaks binary portability) before copying them into this project's release build.
- [ ] Thread/core pinning (`order-book`'s `Affinity.h`) — same technique this project's benchmark harness already uses (`--core`); worth understanding the underlying `pthread_setaffinity_np`/mach affinity APIs rather than treating it as a black box.
- [ ] Cache-friendly / data-oriented design — read up on why pointer-chasing (`std::list`, `shared_ptr`) causes cache misses relative to a pool + intrusive list. This is the actual justification for the `TODO.md` memory-pool item; useful to internalize before profiling so you know what a `perf stat` cache-miss improvement should look like.

### Benchmarking & stats
- [ ] Percentile-based latency reporting (p50/p99/p99.9) over mean — already doing this in `bench/`, good instinct; understand *why* tail percentiles matter more than mean for latency-sensitive systems (mirrors this project's own benchmark numbers already in `TODO.md`).
- [ ] Realistic synthetic order-flow generation — `order-book`'s benchmark drives prices via geometric Brownian motion plus multiple distributions (uniform/normal/exponential/Student-t/lognormal) rather than uniform random. Worth comparing against whatever `bench/` currently generates — a Student-t (fat-tailed) price generator might stress this project's book differently than pure uniform.

## 2. Improvements / where to take this project next

Findings from reviewing `order-book`'s `include/`/`src/OrderBook.cpp`, filtered to what's actually relevant to check for in *this* codebase:

- [ ] **Input validation at `AddOrder`'s boundary.** `order-book` silently clamps out-of-range prices instead of rejecting them, and doesn't guard against re-adding an already-active order ID (both are silent-corruption bugs — wrong trade price, and an unreachable "ghost" order stuck in the book forever). Audit `OrderBook::AddOrder` here for the same two gaps: is there a check for a duplicate `OrderId`, and is there a check for degenerate/invalid `Quantity` (zero or negative)? If not, add explicit rejection (return/throw) rather than silently no-op'ing or corrupting state.
- [ ] **Preempt the placement-new/destructor gotcha before building the pool.** `order-book`'s pool allocator reuses a slot via placement-new without destructing the previous occupant — safe only because `Order` there has no owning members. When implementing the `TODO.md` memory pool for `Order` here, explicitly decide: does pooled `Order` stay trivially destructible, or does the pool need to call the destructor before reuse? Get this right on the first pass instead of inheriting a latent bug.
- [ ] **Don't duplicate the pre-trade check against the real matching loop.** `order-book`'s FOK "can I fill?" pre-check is a hand-duplicated copy of its actual matching loop, which can silently drift out of sync with real matching semantics over time. This project's `CanFullyFill` (using the separate `data_` aggregate-quantity map) is a *better* pattern already — it doesn't duplicate the matching loop's logic, it queries aggregate state. Worth explicitly noting in this project's README/architecture section *why* that design was chosen, since it's a genuine improvement over the naive approach.
- [ ] **Avoid redundant dual-source-of-truth state.** `order-book` tracks price-level occupancy in two independently-updated places (a bitmap bit derived from quantity totals, and the same bit derived from queue emptiness), which is a bug waiting for a future edit that updates one but not the other. When/if this project adds an equivalent "is this price level empty" fast-path (e.g. for skipping levels during matching), make sure there's exactly one source of truth, not two paths that both mutate the same flag.
- [ ] **Const-correctness discipline.** Watch for `const_cast` creeping into read-only/debug paths (`order-book`'s `printOrderBook` casts away const unnecessarily, when the const overload would've been selected automatically). Cheap to avoid, easy to let rot into a real mutation bug later — worth a quick grep across this project's `const` methods as a five-minute sanity check.
- [ ] **Print/debug completeness.** Minor, but `order-book`'s book-dump function only prints one side (bids) despite its name implying a full dump — an easy trap for any debug-output function in this project too (verify anything printing "the book" actually covers both `bids_` and `asks_`).

### Sequencing suggestion
Superseded by the numbered "Next: performance work" list in `TODO.md`, which now owns the ordering (housekeeping is step 4, before the pool in step 5) and folds in the `order-book` pitfalls above where each one applies.
