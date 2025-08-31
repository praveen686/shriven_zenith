#pragma once

#include <atomic>
#include <thread>
#include <array>
#include <cstring>
#include <chrono>
#include <immintrin.h>

#include "macros.h"
#include "time_utils.h"

namespace BldgBlocks {


/// Lock-free circular buffer for ultra-low latency logging
template<size_t BUFFER_SIZE>
class alignas(64) LockFreeLogBuffer {
    static_assert((BUFFER_SIZE & (BUFFER_SIZE - 1)) == 0, "Buffer size must be power of 2");
    
public:
    struct alignas(32) LogEntry {
        uint64_t timestamp;
        uint32_t thread_id;
        uint16_t level;
        uint16_t length;
        char data[256 - 16]; // Total 256 bytes for cache efficiency
    };
    
    LockFreeLogBuffer() : buffer_{} {
        // Initialize all entries to zero
        memset(buffer_.data(), 0, sizeof(buffer_));
    }
    
    // Producer: Write log entry (returns false if buffer full)
    bool write(uint16_t level, const char* data, size_t len) noexcept {
        const auto write_idx = write_idx_.load(std::memory_order_relaxed);
        const auto next_write = write_idx + 1;
        
        // Check if buffer is full (leaving one slot empty to distinguish full/empty)
        if (UNLIKELY(next_write == read_idx_.load(std::memory_order_acquire))) {
            return false; // Buffer full
        }
        
        auto& entry = buffer_[write_idx & (BUFFER_SIZE - 1)];
        entry.timestamp = rdtsc();
        entry.thread_id = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        entry.level = level;
        entry.length = static_cast<uint16_t>(std::min(len, sizeof(entry.data) - 1));
        
        // Fast string copy with SIMD when possible
#ifdef __AVX2__
        if (entry.length >= 32) {
            size_t simd_len = static_cast<size_t>(entry.length) & ~31U; // Round down to multiple of 32
            for (size_t i = 0; i < simd_len; i += 32) {
                __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(entry.data + i), chunk);
            }
            memcpy(entry.data + simd_len, data + simd_len, static_cast<size_t>(entry.length) - simd_len);
        } else {
            memcpy(entry.data, data, entry.length);
        }
#else
        // Fallback to standard memcpy when AVX2 not available
        memcpy(entry.data, data, entry.length);
#endif
        entry.data[entry.length] = '\0';
        
        // Publish the write
        write_idx_.store(next_write, std::memory_order_release);
        return true;
    }
    
    // Consumer: Read log entry (returns false if buffer empty)
    bool read(LogEntry& entry) noexcept {
        const auto read_idx = read_idx_.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (UNLIKELY(read_idx == write_idx_.load(std::memory_order_acquire))) {
            return false; // Buffer empty
        }
        
        // Copy the entry
        entry = buffer_[read_idx & (BUFFER_SIZE - 1)];
        
        // Advance read index
        read_idx_.store(read_idx + 1, std::memory_order_release);
        return true;
    }
    
    size_t size() const noexcept {
        const auto write = write_idx_.load(std::memory_order_acquire);
        const auto read = read_idx_.load(std::memory_order_acquire);
        return write - read;
    }
    
    bool empty() const noexcept { return size() == 0; }
    bool full() const noexcept { return size() >= BUFFER_SIZE - 1; }
    
private:
    // Cache-line aligned atomic indices
    alignas(64) std::atomic<uint64_t> write_idx_{0};
    alignas(64) std::atomic<uint64_t> read_idx_{0};
    
    // Ring buffer of log entries
    std::array<LogEntry, BUFFER_SIZE> buffer_;
};

/// Ultra-fast logger with async background thread
class Logger {
public:
    enum Level : uint16_t {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        ERROR = 3,
        FATAL = 4
    };
    
    static constexpr size_t LOG_BUFFER_SIZE = 16 * 1024; // 16K entries
    
    explicit Logger(const std::string& filename = "") 
        : filename_(),
          running_(true),
          writer_thread_(),
          log_buffer_() {
        
        // Use config if available, otherwise use default
        if (filename.empty()) {
            // Try to use config path
            try {
                std::string logs_dir = "/tmp"; // Default fallback
                std::string prefix = "trading";
                
                // Check if config is initialized (without including config.h to avoid circular dependency)
                char* config_dir = std::getenv("SHRIVEN_LOGS_DIR");
                if (config_dir) {
                    logs_dir = config_dir;
                }
                
                // Create timestamped log file
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                char time_buf[32];
                strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", localtime(&time_t));
                
                filename_ = logs_dir + "/" + prefix + "_" + time_buf + ".log";
            } catch (...) {
                filename_ = "trading.log";
            }
        } else {
            filename_ = filename;
        }
        
        // Start background logging thread
        writer_thread_ = std::thread([this] { writerLoop(); });
        
        // Pin writer thread to last CPU core to avoid interference
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(std::thread::hardware_concurrency() - 1, &cpuset);
        pthread_setaffinity_np(writer_thread_.native_handle(), sizeof(cpuset), &cpuset);
    }
    
    ~Logger() {
        stop();
    }
    
    // Fast log method - just writes to lock-free buffer
    template<typename... Args>
    void log(Level level, const char* format, Args&&... args) noexcept {
        constexpr size_t MAX_MSG_SIZE = 240; // Leave room for header
        char buffer[MAX_MSG_SIZE];
        
        // Format message using stack buffer
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-security"
        int len = snprintf(buffer, sizeof(buffer), format, std::forward<Args>(args)...);
#pragma GCC diagnostic pop
        if (len > 0 && len < static_cast<int>(sizeof(buffer))) {
            // Try to write to ring buffer
            if (UNLIKELY(!log_buffer_.write(level, buffer, static_cast<size_t>(len)))) {
                // Buffer full - increment drop counter
                drops_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    // Convenience methods for different log levels
    template<typename... Args>
    void debug(const char* format, Args&&... args) noexcept {
        log(DEBUG, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void info(const char* format, Args&&... args) noexcept {
        log(INFO, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void warn(const char* format, Args&&... args) noexcept {
        log(WARN, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void error(const char* format, Args&&... args) noexcept {
        log(ERROR, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void fatal(const char* format, Args&&... args) noexcept {
        log(FATAL, format, std::forward<Args>(args)...);
    }
    
    void stop() noexcept {
        if (running_.exchange(false)) {
            if (writer_thread_.joinable()) {
                writer_thread_.join();
            }
        }
    }
    
    struct Stats {
        uint64_t messages_written = 0;
        uint64_t messages_dropped = 0;
        uint64_t bytes_written = 0;
    };
    
    Stats getStats() const noexcept {
        Stats stats;
        stats.messages_written = messages_written_.load();
        stats.messages_dropped = drops_.load();
        stats.bytes_written = bytes_written_.load();
        return stats;
    }
    
private:
    void writerLoop() noexcept {
        // Open log file
        FILE* file = fopen(filename_.c_str(), "a");
        if (!file) {
            return; // Can't write error without logger!
        }
        
        // Use large buffer for file I/O
        setvbuf(file, nullptr, _IOFBF, 64 * 1024);
        
        typename LockFreeLogBuffer<LOG_BUFFER_SIZE>::LogEntry entry;
        char timestamp_buf[32];
        
        while (running_.load(std::memory_order_acquire) || !log_buffer_.empty()) {
            if (log_buffer_.read(entry)) {
                // Format timestamp
                formatTimestamp(entry.timestamp, timestamp_buf, sizeof(timestamp_buf));
                
                // Write formatted log entry
                int written = fprintf(file, "[%s][%s][T%u] %s\n",
                                    timestamp_buf,
                                    levelToString(static_cast<Level>(entry.level)),
                                    entry.thread_id,
                                    entry.data);
                
                if (written > 0) {
                    messages_written_.fetch_add(1, std::memory_order_relaxed);
                    bytes_written_.fetch_add(static_cast<uint64_t>(written), std::memory_order_relaxed);
                }
                
                // Flush periodically or on fatal errors
                static thread_local uint32_t flush_counter = 0;
                if (++flush_counter >= 100 || entry.level >= FATAL) {
                    fflush(file);
                    flush_counter = 0;
                }
            } else {
                // Buffer empty, brief yield to avoid busy waiting
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        
        fclose(file);
    }
    
    const char* levelToString(Level level) const noexcept {
        switch (level) {
            case DEBUG: return "DEBUG";
            case INFO:  return "INFO ";
            case WARN:  return "WARN ";
            case ERROR: return "ERROR";
            case FATAL: return "FATAL";
            default:    return "UNKN ";
        }
    }
    
    void formatTimestamp(uint64_t rdtsc_time, char* buffer, size_t size) const noexcept {
        // Convert RDTSC to nanoseconds (approximate)
        static const double ns_per_tick = 1e9 / 3.0e9; // Assume 3GHz CPU
        uint64_t ns = static_cast<uint64_t>(static_cast<double>(rdtsc_time) * ns_per_tick);
        
        auto time_s = ns / 1000000000ULL;
        auto time_ns = ns % 1000000000ULL;
        
        snprintf(buffer, size, "%llu.%09llu", static_cast<unsigned long long>(time_s), static_cast<unsigned long long>(time_ns));
    }
    
    std::string filename_;
    std::atomic<bool> running_;
    std::thread writer_thread_;
    
    LockFreeLogBuffer<LOG_BUFFER_SIZE> log_buffer_;
    
    // Statistics
    std::atomic<uint64_t> messages_written_{0};
    std::atomic<uint64_t> drops_{0};
    std::atomic<uint64_t> bytes_written_{0};
};

// Global logger instance
extern Logger* g_opt_logger;

// Initialize global logger
inline void initLogging(const std::string& filename = "trading.log") {
    if (!g_opt_logger) {
        g_opt_logger = new Logger(filename);
    }
}

// Cleanup global logger
inline void shutdownLogging() {
    if (g_opt_logger) {
        g_opt_logger->stop();
        delete g_opt_logger;
        g_opt_logger = nullptr;
    }
}

// Fast logging macros
#define LOG_OPT_DEBUG(...) if (BldgBlocks::g_opt_logger) BldgBlocks::g_opt_logger->debug(__VA_ARGS__)
#define LOG_OPT_INFO(...)  if (BldgBlocks::g_opt_logger) BldgBlocks::g_opt_logger->info(__VA_ARGS__)
#define LOG_OPT_WARN(...)  if (BldgBlocks::g_opt_logger) BldgBlocks::g_opt_logger->warn(__VA_ARGS__)
#define LOG_OPT_ERROR(...) if (BldgBlocks::g_opt_logger) BldgBlocks::g_opt_logger->error(__VA_ARGS__)
#define LOG_OPT_FATAL(...) if (BldgBlocks::g_opt_logger) BldgBlocks::g_opt_logger->fatal(__VA_ARGS__)


} // namespace BldgBlocks