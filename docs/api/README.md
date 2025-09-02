# API Reference Documentation

## Overview

Complete API reference for all components in the Shriven Zenith trading system. All APIs follow strict performance guarantees with documented latency characteristics.

## API Categories

### [Common Infrastructure](common/README.md)
Core utilities and data structures used throughout the system.

- [Lock-Free Queue](common/lf_queue.md) - `O(1)` thread-safe queue
- [Memory Pool](common/mem_pool.md) - Fixed-time allocation
- [Logging System](common/logging.md) - Zero-overhead logging
- [Thread Utilities](common/thread_utils.md) - CPU affinity and RT priority
- [Time Utilities](common/time_utils.md) - Nanosecond precision timing
- [Socket Utilities](common/socket_utils.md) - Low-latency networking

### [Trading Components](trading/README.md)
Trading-specific components and adapters.

- [Zerodha Authentication](trading/zerodha_auth.md) - Kite Connect integration
- [Binance Authentication](trading/binance_auth.md) - Binance API integration
- [Instrument Fetcher](trading/instrument_fetcher.md) - Symbol management
- [Order Manager](trading/order_manager.md) - Order lifecycle (Coming Soon)
- [Risk Manager](trading/risk_manager.md) - Risk controls (Coming Soon)

### [Configuration](config/README.md)
System configuration and environment management.

- [ConfigManager](config/config_manager.md) - Master configuration
- [Environment Loader](config/env_loader.md) - Environment variables

## API Documentation Standards

Each API is documented with:

### 1. Function Signature
```cpp
[[nodiscard]] auto functionName(ParamType param) noexcept -> ReturnType;
```

### 2. Performance Characteristics
- **Complexity**: `O(1)`, `O(log n)`, etc.
- **Latency**: Typical and worst-case in nanoseconds
- **Thread Safety**: Thread-safe, Lock-free, or Single-threaded
- **Memory**: Stack-only, Pre-allocated, or Dynamic

### 3. Usage Example
```cpp
// Example code showing typical usage
auto result = functionName(param);
```

### 4. Error Handling
- Return codes
- Error conditions
- Recovery mechanisms

## Quick Reference

### Most Used APIs

| API | Purpose | Latency | Thread-Safe |
|-----|---------|---------|-------------|
| `LFQueue::enqueue()` | Add to queue | 42ns | ✅ Lock-free |
| `MemPool::allocate()` | Get memory | 26ns | ✅ Lock-free |
| `LOG_INFO()` | Log message | 35ns | ✅ Lock-free |
| `rdtsc()` | Get timestamp | 15ns | ✅ |
| `setThreadAffinity()` | Pin to CPU | 1μs | ❌ |

### Performance Critical APIs

APIs marked with 🔥 are in the hot path and have strict latency requirements:

- 🔥 `LFQueue::enqueue/dequeue` - Must be < 100ns
- 🔥 `MemPool::allocate/deallocate` - Must be < 50ns
- 🔥 `getCurrentNanos()` - Must be < 20ns
- 🔥 `CacheAligned<T>` - Zero overhead wrapper

### Exchange-Specific APIs

#### Zerodha (NSE/BSE)
- Session management with TOTP
- Instrument fetching (68K+ symbols)
- Order placement (REST API)
- Market data subscription

#### Binance (Crypto)
- HMAC authentication
- Symbol fetching (Top 25 or All)
- Order placement (REST API)
- WebSocket streams (planned)

## API Versioning

Current API Version: **1.0.0**

### Version History
- v1.0.0 (2025-09-02): Initial release
  - Common infrastructure
  - Zerodha integration
  - Binance integration

### Breaking Changes Policy
- Major version for breaking changes
- Minor version for additions
- Patch version for fixes

## API Stability Guarantees

| Stability Level | Meaning | Can Break? |
|----------------|---------|------------|
| 🟢 **Stable** | Production ready | Never |
| 🟡 **Beta** | Testing phase | Minor versions |
| 🔴 **Experimental** | Research only | Any time |

### Current Stability Levels

- 🟢 Common Infrastructure - **Stable**
- 🟢 Configuration - **Stable**
- 🟢 Zerodha Auth - **Stable**
- 🟢 Binance Auth - **Stable**
- 🟡 Instrument Fetcher - **Beta**
- 🔴 Order Manager - **Experimental**

---

*Continue to: [Common Infrastructure APIs](common/README.md) →*