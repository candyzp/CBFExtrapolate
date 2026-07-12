#pragma once

#include <Geode/platform/cplatform.h>

using TimestampType = double;

#ifdef GEODE_IS_WINDOWS

#include <Geode/Geode.hpp>

inline LARGE_INTEGER freq = []() {
  LARGE_INTEGER f;
  QueryPerformanceFrequency(&f);
  return f;
}();

inline bool g_linuxNative = false;

inline TimestampType getCurrentTimestamp() {
  LARGE_INTEGER t;
  if (g_linuxNative) {
    GetSystemTimePreciseAsFileTime((FILETIME *)&t);
  } else {
    QueryPerformanceCounter(&t);
  }
  return static_cast<TimestampType>(t.QuadPart) /
         static_cast<TimestampType>(freq.QuadPart);
}

#elif defined(GEODE_IS_ANDROID)

#include <time.h>

inline TimestampType getCurrentTimestamp() {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec + ((double)now.tv_nsec / 1'000'000'000.0);
}

#elif defined(GEODE_IS_MACOS) || defined(GEODE_IS_IOS)

#include <time.h>

inline TimestampType getCurrentTimestamp() {
  return (double)clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1'000'000'000.0;
}

#endif
