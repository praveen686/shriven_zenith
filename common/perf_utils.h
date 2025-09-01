#pragma once

#include <cstdint>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstring>
#include <array>

#include "time_utils.h"

namespace Common {


// Hardware Performance Monitoring Unit (PMU) counters
class PmuCounters {
public:
  enum Event {
    CPU_CYCLES,
    INSTRUCTIONS,
    CACHE_REFERENCES,
    CACHE_MISSES,
    BRANCH_INSTRUCTIONS,
    BRANCH_MISSES,
    L1D_CACHE_MISSES,
    LLC_CACHE_MISSES,
    MAX_EVENTS
  };
  
  PmuCounters() {
    memset(fds_, -1, sizeof(fds_));
  }
  
  ~PmuCounters() {
    stop();
  }
  
  // Initialize PMU counter for specific event
  bool init(Event event) noexcept {
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(pe));
    pe.size = sizeof(pe);
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;
    
    switch (event) {
      case CPU_CYCLES:
        pe.type = PERF_TYPE_HARDWARE;
        pe.config = PERF_COUNT_HW_CPU_CYCLES;
        break;
      case INSTRUCTIONS:
        pe.type = PERF_TYPE_HARDWARE;
        pe.config = PERF_COUNT_HW_INSTRUCTIONS;
        break;
      case CACHE_REFERENCES:
        pe.type = PERF_TYPE_HARDWARE;
        pe.config = PERF_COUNT_HW_CACHE_REFERENCES;
        break;
      case CACHE_MISSES:
        pe.type = PERF_TYPE_HARDWARE;
        pe.config = PERF_COUNT_HW_CACHE_MISSES;
        break;
      case BRANCH_INSTRUCTIONS:
        pe.type = PERF_TYPE_HARDWARE;
        pe.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
        break;
      case BRANCH_MISSES:
        pe.type = PERF_TYPE_HARDWARE;
        pe.config = PERF_COUNT_HW_BRANCH_MISSES;
        break;
      case L1D_CACHE_MISSES:
        pe.type = PERF_TYPE_HW_CACHE;
        pe.config = (PERF_COUNT_HW_CACHE_L1D) | 
                   (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                   (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
        break;
      case LLC_CACHE_MISSES:
        pe.type = PERF_TYPE_HW_CACHE;
        pe.config = (PERF_COUNT_HW_CACHE_LL) | 
                   (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                   (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
        break;
      default:
        return false;
    }
    
    fds_[event] = syscall(SYS_perf_event_open, &pe, 0, -1, -1, 0);
    return fds_[event] >= 0;
  }
  
  // Start counting
  void start(Event event) noexcept {
    if (fds_[event] >= 0) {
      ioctl(fds_[event], PERF_EVENT_IOC_RESET, 0);
      ioctl(fds_[event], PERF_EVENT_IOC_ENABLE, 0);
    }
  }
  
  // Read counter value
  uint64_t read(Event event) noexcept {
    uint64_t value = 0;
    if (fds_[event] >= 0) {
      ::read(fds_[event], &value, sizeof(value));
    }
    return value;
  }
  
  // Stop all counters
  void stop() noexcept {
    for (int& fd : fds_) {
      if (fd >= 0) {
        ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
        close(fd);
        fd = -1;
      }
    }
  }
  
private:
  int fds_[MAX_EVENTS];
};

// Memory bandwidth monitoring
class MemoryBandwidth {
public:
  static uint64_t getBytesRead() noexcept {
    // Would read from /sys/devices/system/node/node*/meminfo
    // or use Intel PCM library
    return 0; // Placeholder
  }
  
  static uint64_t getBytesWritten() noexcept {
    // Would read from performance counters
    return 0; // Placeholder
  }
};

// CPU frequency monitoring
class CpuFrequency {
public:
  static uint64_t getCurrentFreqMHz(int cpu = 0) noexcept {
    char path[256];
    snprintf(path, sizeof(path), 
             "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
    
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    
    uint64_t freq_khz = 0;
    fscanf(f, "%lu", &freq_khz);
    fclose(f);
    
    return freq_khz / 1000; // Convert to MHz
  }
  
  static bool setGovernor(const char* governor = "performance") noexcept {
    // Requires root
    char path[256];
    snprintf(path, sizeof(path), 
             "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    
    FILE* f = fopen(path, "w");
    if (!f) return false;
    
    fprintf(f, "%s\n", governor);
    fclose(f);
    return true;
  }
};

// Cache line monitoring utilities
class CacheUtils {
public:
  // Flush cache line
  static void flushCacheLine(const void* addr) noexcept {
    __builtin_ia32_clflush(addr);
  }
  
  // Prefetch to all cache levels
  static void prefetchAllLevels(const void* addr) noexcept {
    __builtin_prefetch(addr, 0, 3); // L1
    __builtin_prefetch(addr, 0, 2); // L2
    __builtin_prefetch(addr, 0, 1); // L3
  }
  
  // Non-temporal store (bypass cache)
  template<typename T>
  static void nonTemporalStore(T* dst, T value) noexcept {
    if constexpr (sizeof(T) == 4) {
      _mm_stream_si32(reinterpret_cast<int*>(dst), *reinterpret_cast<int*>(&value));
    } else if constexpr (sizeof(T) == 8) {
      _mm_stream_si64(reinterpret_cast<long long*>(dst), *reinterpret_cast<long long*>(&value));
    } else {
      *dst = value; // Fallback
    }
  }
};

// NUMA utilities
class NumaUtils {
public:
  static int getCurrentNode() noexcept {
    return numa_node_of_cpu(sched_getcpu());
  }
  
  static void* allocateOnNode(size_t size, int node) noexcept {
    return numa_alloc_onnode(size, node);
  }
  
  static void bindToNode(int node) noexcept {
    numa_run_on_node(node);
  }
  
  static int getNodeCount() noexcept {
    return numa_max_node() + 1;
  }
};

// Profile scope for automatic timing
class ProfileScope {
public:
  ProfileScope(const char* name, LatencyTracker* tracker = nullptr) 
    : name_(name), tracker_(tracker) {
    start_ = rdtsc();
  }
  
  ~ProfileScope() {
    uint64_t elapsed = rdtsc() - start_;
    if (tracker_) {
      tracker_->record(elapsed);
    }
    // Could also log or accumulate stats
  }
  
private:
  const char* name_;
  LatencyTracker* tracker_;
  uint64_t start_;
  
};


} // namespace Common