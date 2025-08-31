#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <ctime>
#include <x86intrin.h>

namespace BldgBlocks {


// Original time utilities
inline auto getCurrentTimeStr(std::string* time_str) {
  const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  time_str->assign(ctime(&time));
  if (!time_str->empty())
    time_str->at(time_str->length() - 1) = '\0';
  return time_str;
}

// Enhanced time utilities for ultra low latency

// Get TSC (Time Stamp Counter) - fastest timing method
inline uint64_t rdtsc() noexcept {
  return __rdtsc();
}

// Get TSC with serialization (more accurate but slower)
inline uint64_t rdtscp() noexcept {
  unsigned int aux;
  return __rdtscp(&aux);
}

// Get nanoseconds using CLOCK_MONOTONIC (no system call)
inline uint64_t getNanosSinceEpoch() noexcept {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

// Get nanoseconds using CLOCK_REALTIME for wall clock
inline uint64_t getWallClockNanos() noexcept {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

// Ultra-fast timestamp class using TSC
class TscTimer {
public:
  TscTimer() : freq_ghz_(calibrateFrequency()) {}
  
  // Start timing
  void start() noexcept {
    start_tsc_ = rdtsc();
  }
  
  // Get elapsed nanoseconds
  uint64_t elapsedNanos() const noexcept {
    return static_cast<uint64_t>(static_cast<double>(rdtsc() - start_tsc_) / freq_ghz_);
  }
  
  // Get elapsed microseconds
  double elapsedMicros() const noexcept {
    return static_cast<double>(elapsedNanos()) / 1000.0;
  }
  
  // Convert TSC to nanoseconds
  uint64_t tscToNanos(uint64_t tsc_delta) const noexcept {
    return static_cast<uint64_t>(static_cast<double>(tsc_delta) / freq_ghz_);
  }
  
private:
  // Calibrate TSC frequency
  static double calibrateFrequency() noexcept {
    const auto start = getNanosSinceEpoch();
    const auto start_tsc = rdtsc();
    
    // Busy wait for calibration period
    while (getNanosSinceEpoch() - start < 10'000'000); // 10ms
    
    const auto end_tsc = rdtsc();
    const auto end = getNanosSinceEpoch();
    
    return static_cast<double>(end_tsc - start_tsc) / static_cast<double>(end - start);
  }
  
  uint64_t start_tsc_ = 0;
  const double freq_ghz_;
};

// PTP (Precision Time Protocol) support for network synchronization
class PtpClock {
public:
  // Get PTP hardware clock time if available
  static uint64_t getHardwareTime() noexcept {
    // This would interface with PTP hardware
    // For now, fallback to system clock
    return getWallClockNanos();
  }
  
  // Synchronize with PTP grandmaster
  static void sync() noexcept {
    // Would implement PTP protocol
    // Requires root access and PTP daemon
  }
};

// Fast date/time formatting without allocation
class FastDateTime {
public:
  static void formatNanos(uint64_t nanos, char* buffer) noexcept {
    // Format: YYYYMMDD-HH:MM:SS.nnnnnnnnn
    time_t seconds = static_cast<time_t>(nanos / 1'000'000'000);
    uint32_t ns = static_cast<uint32_t>(nanos % 1'000'000'000);
    
    struct tm tm_time;
    gmtime_r(&seconds, &tm_time);
    
    sprintf(buffer, "%04d%02d%02d-%02d:%02d:%02d.%09u",
            tm_time.tm_year + 1900,
            tm_time.tm_mon + 1,
            tm_time.tm_mday,
            tm_time.tm_hour,
            tm_time.tm_min,
            tm_time.tm_sec,
            ns);
  }
  
  // Format for FIX protocol (YYYYMMDD-HH:MM:SS.sss)
  static void formatFIX(uint64_t nanos, char* buffer) noexcept {
    time_t seconds = static_cast<time_t>(nanos / 1'000'000'000);
    uint32_t ms = static_cast<uint32_t>((nanos % 1'000'000'000) / 1'000'000);
    
    struct tm tm_time;
    gmtime_r(&seconds, &tm_time);
    
    sprintf(buffer, "%04d%02d%02d-%02d:%02d:%02d.%03u",
            tm_time.tm_year + 1900,
            tm_time.tm_mon + 1,
            tm_time.tm_mday,
            tm_time.tm_hour,
            tm_time.tm_min,
            tm_time.tm_sec,
            ms);
  }
};

// Latency statistics tracker
class LatencyTracker {
public:
  void record(uint64_t nanos) noexcept {
    count_++;
    sum_ += nanos;
    
    if (nanos < min_) min_ = nanos;
    if (nanos > max_) max_ = nanos;
    
    // Update percentiles (simplified - real implementation would use histogram)
    if (count_ == 1) {
      p50_ = p99_ = p999_ = nanos;
    } else {
      // Exponential moving average for approximation
      p50_ = static_cast<uint64_t>((static_cast<double>(p50_) * 0.95) + (static_cast<double>(nanos) * 0.05));
      if (nanos > p99_) p99_ = nanos;
      if (nanos > p999_) p999_ = nanos;
    }
  }
  
  uint64_t min() const noexcept { return min_; }
  uint64_t max() const noexcept { return max_; }
  uint64_t avg() const noexcept { return count_ ? sum_ / count_ : 0; }
  uint64_t p50() const noexcept { return p50_; }
  uint64_t p99() const noexcept { return p99_; }
  uint64_t p999() const noexcept { return p999_; }
  uint64_t count() const noexcept { return count_; }
  
  void reset() noexcept {
    count_ = sum_ = 0;
    min_ = UINT64_MAX;
    max_ = p50_ = p99_ = p999_ = 0;
  }
  
private:
  uint64_t count_ = 0;
  uint64_t sum_ = 0;
  uint64_t min_ = UINT64_MAX;
  uint64_t max_ = 0;
  uint64_t p50_ = 0;
  uint64_t p99_ = 0;
  uint64_t p999_ = 0;
};


} // namespace BldgBlocks