// OrderBook benchmark harness: replays a synthetic order-flow script on a
// pinned core and reports throughput plus latency percentiles by op type,
// and a fingerprint of the run that --check can compare against.
//
// Usage: ./orderbook_bench [--orders N] [--warmup N] [--core C] [--seed S]
//                           [--target-depth N] [--cancel-ratio F]
//                           [--modify-ratio F] [--tick-spread N] [--csv PATH]
//                           [--check HEX] [--check-every N]

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include "OrderBook.hpp"

#include "Affinity.hpp"
#include "Fingerprint.hpp"
#include "LatencyRecorder.hpp"
#include "OrderGenerator.hpp"

struct Args
{
  std::size_t orders = 5'000'000;
  std::size_t warmup = 500'000;
  int core = 0;
  std::uint64_t seed = 42;
  std::size_t targetDepth = 5000;
  double cancelRatio = 0.2;
  double modifyRatio = 0.1;
  Price tickSpread = 25;
  std::string csvPath;
  std::uint64_t expectedFingerprint = 0; // 0 = don't check
  std::size_t checkEvery = 0;            // 0 = only check invariants at the end
};

void PrintUsage()
{
  std::fprintf(stderr,
                "usage: orderbook_bench [--orders N] [--warmup N] [--core C] [--seed S]\n"
                "                        [--target-depth N] [--cancel-ratio F]\n"
                "                        [--modify-ratio F] [--tick-spread N] [--csv PATH]\n"
                "                        [--check HEX] [--check-every N]\n"
                "defaults: --orders 5000000 --warmup 500000 --core 0 --seed 42\n"
                "          --target-depth 5000 --cancel-ratio 0.2 --modify-ratio 0.1\n"
                "          --tick-spread 25\n");
}

Args ParseArgs(int argc, char **argv)
{
  Args args;

  for (int i = 1; i < argc; ++i)
  {
    const std::string flag = argv[i];

    const auto next = [&]() -> std::string
    {
      if (i + 1 >= argc)
      {
        std::fprintf(stderr, "error: %s requires a value\n", flag.c_str());
        PrintUsage();
        std::exit(1);
      }
      return argv[++i];
    };

    if (flag == "--orders")
      args.orders = std::stoull(next());
    else if (flag == "--warmup")
      args.warmup = std::stoull(next());
    else if (flag == "--core")
      args.core = std::stoi(next());
    else if (flag == "--seed")
      args.seed = std::stoull(next());
    else if (flag == "--target-depth")
      args.targetDepth = std::stoull(next());
    else if (flag == "--cancel-ratio")
      args.cancelRatio = std::stod(next());
    else if (flag == "--modify-ratio")
      args.modifyRatio = std::stod(next());
    else if (flag == "--tick-spread")
      args.tickSpread = static_cast<Price>(std::stoi(next()));
    else if (flag == "--csv")
      args.csvPath = next();
    else if (flag == "--check")
      args.expectedFingerprint = std::stoull(next(), nullptr, 16);
    else if (flag == "--check-every")
      args.checkEvery = std::stoull(next());
    else if (flag == "--help" || flag == "-h")
    {
      PrintUsage();
      std::exit(0);
    }
    else
    {
      std::fprintf(stderr, "error: unknown flag %s\n", flag.c_str());
      PrintUsage();
      std::exit(1);
    }
  }

  return args;
}

// returns nullptr if the book is consistent, else what's wrong with it
const char *CheckInvariants(const OrderBook &book)
{
  const auto infos = book.GetOrderInfos();
  const auto &bids = infos.GetBids();
  const auto &asks = infos.GetAsks();

  if (!bids.empty() && !asks.empty() && bids.front().price_ >= asks.front().price_)
    return "book is crossed (best bid >= best ask)";
  if (book.Size() < bids.size() + asks.size())
    return "more price levels than resting orders";
  for (const auto *levels : {&bids, &asks})
    for (const LevelInfo &level : *levels)
      if (level.quantity_ == 0)
        return "empty price level left in the book";

  return nullptr;
}

void CheckInvariantsOrDie(const OrderBook &book, std::size_t opIndex)
{
  if (const char *failure = CheckInvariants(book))
  {
    std::fprintf(stderr, "error: invariant violated after op %zu: %s\n", opIndex, failure);
    std::exit(1);
  }
}

void PrintBucket(const char *label, const LatencyStats &s)
{
  std::printf("  %-7s (n=%9zu): p50=%9.0fns  p90=%9.0fns  p99=%9.0fns  p99.9=%9.0fns  max=%9.0fns\n", label, s.count,
              s.p50, s.p90, s.p99, s.p999, s.max);
}


int main(int argc, char **argv)
{
  const Args args = ParseArgs(argc, argv);

#ifndef NDEBUG
  std::fprintf(stderr, "==================================================================\n"
                        "WARNING: this is a DEBUG build (NDEBUG not defined).\n"
                        "Latency/throughput numbers from a debug build are meaningless.\n"
                        "Build with `make release` and re-run before recording any numbers.\n"
                        "==================================================================\n\n");
#endif

  PinToCore(args.core);

  GeneratorConfig genConfig;
  genConfig.seed = args.seed;
  genConfig.targetDepth = args.targetDepth;
  genConfig.cancelRatio = args.cancelRatio;
  genConfig.modifyRatio = args.modifyRatio;
  genConfig.tickSpread = args.tickSpread;

  OrderGenerator generator{genConfig};

  const std::size_t totalOps = args.warmup + args.orders;
  std::vector<Op> script;
  generator.Generate(totalOps, script);

  OrderBook book;
  LatencyRecorder recorder{args.orders};
  Fingerprint fingerprint;

  const auto maybeCheck = [&](std::size_t i)
  {
    if (args.checkEvery != 0 && (i + 1) % args.checkEvery == 0)
      CheckInvariantsOrDie(book, i);
  };

  for (std::size_t i = 0; i < args.warmup; ++i)
  {
    fingerprint.AddTrades(Dispatch(book, script[i]));
    maybeCheck(i);
  }

  const auto wallStart = std::chrono::steady_clock::now();
  for (std::size_t i = args.warmup; i < totalOps; ++i)
  {
    const Op &op = script[i];
    const auto t0 = std::chrono::steady_clock::now();
    fingerprint.AddTrades(Dispatch(book, op)); // Trades freed inside the timed region, as a real caller would pay
    const auto t1 = std::chrono::steady_clock::now();
    recorder.Record(op.type, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    maybeCheck(i);
  }
  const auto wallEnd = std::chrono::steady_clock::now();

  fingerprint.Finish(book);

  const double elapsedSec = std::chrono::duration<double>(wallEnd - wallStart).count();
  const double throughput = static_cast<double>(args.orders) / elapsedSec;

  const LatencyStats combined = recorder.StatsCombined();

  std::printf("\n=== OrderBook Benchmark ===\n");
  std::printf("CPU: %s (pinned to core %d)\n", CpuModelName().c_str(), args.core);
  std::printf("Warmup ops: %zu | Measured ops: %zu | Seed: %llu\n", args.warmup, args.orders,
              static_cast<unsigned long long>(args.seed));
  std::printf("Wall time: %.3fs | Throughput: %.0f orders/sec\n", elapsedSec, throughput);

  std::printf("\nCombined latency: p50=%.3fus  p90=%.3fus  p99=%.3fus  p99.9=%.3fus  max=%.3fus  mean=%.3fus\n",
              combined.p50 / 1000.0, combined.p90 / 1000.0, combined.p99 / 1000.0, combined.p999 / 1000.0,
              combined.max / 1000.0, combined.mean / 1000.0);

  std::printf("\nBy op type:\n");
  for (const OpType type : kOpTypes)
    PrintBucket(ToString(type), recorder.StatsFor(type));

  const GeneratorStats &gen = generator.Stats();
  const auto pctOf = [](std::size_t part, std::size_t whole)
  { return whole == 0 ? 100.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole); };
  std::printf("\nTarget hit-rate (op hit a resting order): cancel %.1f%%  modify %.1f%%\n",
              pctOf(gen.cancelHits, gen.cancels), pctOf(gen.modifyHits, gen.modifies));

  const char *invariantFailure = CheckInvariants(book);
  std::printf("Book invariants: %s%s\n", invariantFailure ? "FAIL - " : "PASS",
              invariantFailure ? invariantFailure : "");

  std::printf("Fingerprint: %016llx  (trades=%llu  traded qty=%llu  resting orders=%llu)\n",
              static_cast<unsigned long long>(fingerprint.Value()),
              static_cast<unsigned long long>(fingerprint.TradeCount()),
              static_cast<unsigned long long>(fingerprint.TradedQuantity()),
              static_cast<unsigned long long>(fingerprint.RestingOrders()));

  if (!args.csvPath.empty())
  {
    recorder.WriteCsv(args.csvPath);
    std::printf("\nraw per-op latencies written to %s\n", args.csvPath.c_str());
  }

  if (invariantFailure)
    return 1;

  if (args.expectedFingerprint != 0 && fingerprint.Value() != args.expectedFingerprint)
  {
    std::fprintf(stderr, "error: fingerprint %016llx does not match expected %016llx - matching behaviour changed\n",
                 static_cast<unsigned long long>(fingerprint.Value()),
                 static_cast<unsigned long long>(args.expectedFingerprint));
    return 1;
  }

  return 0;
}
