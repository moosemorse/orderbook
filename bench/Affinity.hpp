#pragma once

// Linux-only core pinning + CPU identification for the benchmark harness.
// PinToCore hard-fails instead of falling back, so numbers can't silently
// come from an unpinned run.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

inline void PinToCore(int core)
{
  const long numCores = sysconf(_SC_NPROCESSORS_ONLN);
  if (core < 0 || numCores <= 0 || core >= numCores)
  {
    std::fprintf(stderr, "error: --core %d out of range (machine has %ld cores)\n", core, numCores);
    std::exit(1);
  }

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core, &cpuset);

  const int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  if (rc != 0)
  {
    std::fprintf(stderr, "error: failed to pin to core %d: %s\n", core, std::strerror(rc));
    std::exit(1);
  }
}

inline std::string CpuModelName()
{
  std::ifstream cpuinfo("/proc/cpuinfo");
  std::string line;
  while (std::getline(cpuinfo, line))
  {
    if (line.rfind("model name", 0) == 0)
    {
      const auto colon = line.find(':');
      if (colon != std::string::npos && colon + 2 <= line.size())
        return line.substr(colon + 2);
    }
  }
  return "unknown";
}
