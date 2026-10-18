#pragma once

// Linux-only. Fails hard rather than silently running unpinned.

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
