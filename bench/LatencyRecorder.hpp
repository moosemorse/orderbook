#pragma once

// Per-op-type latency buckets, preallocated so recording never allocates mid-run.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "OrderGenerator.hpp"

struct LatencyStats
{
  double p50{};
  double p90{};
  double p99{};
  double p999{};
  double max{};
  double mean{};
  std::size_t count{};
};

class LatencyRecorder
{
public:
  explicit LatencyRecorder(std::size_t expectedOpsPerBucket)
  {
    for (auto &bucket : buckets_)
      bucket.reserve(expectedOpsPerBucket);
  }

  void Record(OpType type, std::uint64_t nanoseconds) { buckets_[BucketIndex(type)].push_back(nanoseconds); }

  LatencyStats StatsFor(OpType type) const { return Compute(buckets_[BucketIndex(type)]); }

  LatencyStats StatsCombined() const
  {
    std::size_t total = 0;
    for (const auto &bucket : buckets_)
      total += bucket.size();

    std::vector<std::uint64_t> all;
    all.reserve(total);
    for (const auto &bucket : buckets_)
      all.insert(all.end(), bucket.begin(), bucket.end());

    return Compute(std::move(all));
  }

  void WriteCsv(const std::string &path) const
  {
    std::ofstream out(path);
    out << "op,nanoseconds\n";
    for (const OpType type : kOpTypes)
      for (const auto ns : buckets_[BucketIndex(type)])
        out << ToString(type) << ',' << ns << '\n';
  }

private:
  static std::size_t BucketIndex(OpType type) { return static_cast<std::size_t>(type); }

  static LatencyStats Compute(std::vector<std::uint64_t> samples)
  {
    if (samples.empty())
      return LatencyStats{};

    std::sort(samples.begin(), samples.end());

    const auto pct = [&samples](double p) -> double
    {
      const auto idx = static_cast<std::size_t>(p * static_cast<double>(samples.size() - 1));
      return static_cast<double>(samples[idx]);
    };

    const double sum = static_cast<double>(std::accumulate(samples.begin(), samples.end(), std::uint64_t{0}));

    return LatencyStats{
        pct(0.50),
        pct(0.90),
        pct(0.99),
        pct(0.999),
        static_cast<double>(samples.back()),
        sum / static_cast<double>(samples.size()),
        samples.size(),
    };
  }

  std::array<std::vector<std::uint64_t>, kOpTypes.size()> buckets_;
};
