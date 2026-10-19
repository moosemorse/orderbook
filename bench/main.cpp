// Replays a seeded synthetic order flow on a pinned core and reports latency
// percentiles per op type. Correctness is checked by tests/regression_tests.cpp.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "OrderBook.hpp"

#include "Affinity.hpp"
#include "LatencyRecorder.hpp"
#include "OrderGenerator.hpp"

struct Args
{
  std::size_t orders = 5'000'000;
  std::size_t warmup = 500'000;
  int core = 0;
};

template <typename... FormatArgs>
void Print(std::FILE *stream, std::format_string<FormatArgs...> format, FormatArgs &&...formatArgs)
{
  std::fputs(std::format(format, std::forward<FormatArgs>(formatArgs)...).c_str(), stream);
}

void PrintUsage()
{
  Print(stderr, "usage: orderbook_bench [--orders N] [--warmup N] [--core C]\n");
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

  for (std::size_t i = 0; i < args.warmup; ++i)
    Dispatch(book, script[i]);

  const auto wallStart = std::chrono::steady_clock::now();
  for (std::size_t i = args.warmup; i < totalOps; ++i)
  {
    const Op &op = script[i];
    const auto t0 = std::chrono::steady_clock::now();
    Dispatch(book, op); // returned Trades are freed before t1, as a real caller would pay
    const auto t1 = std::chrono::steady_clock::now();
    recorder.Record(op.type, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
  }
  const auto wallEnd = std::chrono::steady_clock::now();

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

  return 0;
}
