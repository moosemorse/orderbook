// Replays a seeded synthetic order flow on a pinned core, reports latency
// percentiles per op type and a fingerprint of the run for --check.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <initializer_list>
#include <string>
#include <utility>
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
  std::uint64_t expectedFingerprint = 0; // 0 = don't check
  std::size_t checkEvery = 0;            // 0 = only check invariants at the end
};

template <typename... FormatArgs>
void Print(std::FILE *stream, std::format_string<FormatArgs...> format, FormatArgs &&...formatArgs)
{
  std::fputs(std::format(format, std::forward<FormatArgs>(formatArgs)...).c_str(), stream);
}

void PrintUsage()
{
  Print(stderr, "usage: orderbook_bench [--orders N] [--warmup N] [--core C] [--check HEX] [--check-every N]\n");
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
        Print(stderr, "error: {} requires a value\n", flag);
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
      Print(stderr, "error: unknown flag {}\n", flag);
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
    Print(stderr, "error: invariant violated after op {}: {}\n", opIndex, failure);
    std::exit(1);
  }
}

void PrintBucket(const char *label, const LatencyStats &s)
{
  Print(stdout, "  {:<7} (n={:9}): p50={:9.0f}ns  p90={:9.0f}ns  p99={:9.0f}ns  p99.9={:9.0f}ns  max={:9.0f}ns\n", label,
        s.count, s.p50, s.p90, s.p99, s.p999, s.max);
}

int main(int argc, char **argv)
{
  const Args args = ParseArgs(argc, argv);

  PinToCore(args.core);

  OrderGenerator generator{GeneratorConfig{}};

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
    fingerprint.AddTrades(Dispatch(book, op));
    const auto t1 = std::chrono::steady_clock::now();
    recorder.Record(op.type, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    maybeCheck(i);
  }
  const auto wallEnd = std::chrono::steady_clock::now();

  fingerprint.Finish(book);

  const double elapsedSec = std::chrono::duration<double>(wallEnd - wallStart).count();
  const double throughput = static_cast<double>(args.orders) / elapsedSec;

  const LatencyStats combined = recorder.StatsCombined();

  Print(stdout, "\n=== OrderBook Benchmark ===\n");
  Print(stdout, "Core: {} | Warmup ops: {} | Measured ops: {}\n", args.core, args.warmup, args.orders);
  Print(stdout, "Wall time: {:.3f}s | Throughput: {:.0f} orders/sec\n", elapsedSec, throughput);

  Print(stdout, "\nCombined latency: p50={:.3f}us  p90={:.3f}us  p99={:.3f}us  p99.9={:.3f}us  max={:.3f}us  mean={:.3f}us\n",
        combined.p50 / 1000.0, combined.p90 / 1000.0, combined.p99 / 1000.0, combined.p999 / 1000.0,
        combined.max / 1000.0, combined.mean / 1000.0);

  Print(stdout, "\nBy op type:\n");
  for (const OpType type : kOpTypes)
    PrintBucket(ToString(type), recorder.StatsFor(type));

  const char *invariantFailure = CheckInvariants(book);
  Print(stdout, "\nBook invariants: {}{}\n", invariantFailure ? "FAIL - " : "PASS",
        invariantFailure ? invariantFailure : "");

  Print(stdout, "Fingerprint: {:016x}  (trades={}  traded qty={}  resting orders={})\n", fingerprint.Value(),
        fingerprint.TradeCount(), fingerprint.TradedQuantity(), fingerprint.RestingOrders());

  if (invariantFailure)
    return 1;

  if (args.expectedFingerprint != 0 && fingerprint.Value() != args.expectedFingerprint)
  {
    Print(stderr, "error: fingerprint {:016x} does not match expected {:016x} - matching behaviour changed\n",
          fingerprint.Value(), args.expectedFingerprint);
    return 1;
  }

  return 0;
}
