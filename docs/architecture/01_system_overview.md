# System Architecture Overview

## Executive Summary

Shriven Zenith is an ultra-low latency trading system designed for nanosecond-precision execution in high-frequency trading environments. Built with **zero-compromise performance** principles, it achieves sub-microsecond order processing through lock-free architectures and cache-optimized data structures.

## Architecture Principles

### Core Design Philosophy
```
┌─────────────────────────────────────────────────────┐
│                 PRAVEEN'S FOUR RULES                 │
├─────────────────────────────────────────────────────┤
│ 1. Zero Dynamic Allocation in Hot Path              │
│ 2. Lock-Free Data Structures Only                   │
│ 3. Cache-Line Aligned Everything                    │
│ 4. Nanosecond Precision Measurements                │
└─────────────────────────────────────────────────────┘
```

### System Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────────┐
│                         SHRIVEN ZENITH TRADING SYSTEM                │
├──────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐     │
│  │   Market Data   │  │  Order Gateway  │  │   Risk Manager  │     │
│  │    Consumer     │  │                 │  │                 │     │
│  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘     │
│           │                     │                     │              │
│  ┌────────▼──────────────────────▼─────────────────────▼────────┐   │
│  │                      TRADE ENGINE (Core)                      │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │   │
│  │  │ Strategy │  │  Order   │  │ Position │  │  Market  │    │   │
│  │  │  Engine  │◄─┤ Manager  │◄─┤  Keeper  │◄─┤   Book   │    │   │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘    │   │
│  └───────────────────────────────────────────────────────────────┘   │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    COMMON INFRASTRUCTURE                     │    │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │    │
│  │  │  Memory  │  │  Thread  │  │  Logging │  │   Time   │   │    │
│  │  │   Pool   │  │   Utils  │  │  System  │  │  Utils   │   │    │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │    │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │    │
│  │  │ LF Queue │  │  Socket  │  │  Config  │  │  Cache   │   │    │
│  │  │          │  │   Utils  │  │  Manager │  │  Aligned │   │    │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    EXCHANGE CONNECTIVITY                     │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │    │
│  │  │   Zerodha    │  │   Binance    │  │   Future     │      │    │
│  │  │   Adapter    │  │   Adapter    │  │   Adapters   │      │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘      │    │
│  └─────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────────┘
```

## Component Layers

### 1. Exchange Connectivity Layer
- **Purpose**: Abstract exchange-specific protocols
- **Components**: 
  - Zerodha Adapter (Kite Connect API)
  - Binance Adapter (REST/WebSocket)
- **Latency Target**: < 1ms for order submission

### 2. Common Infrastructure Layer
- **Purpose**: Zero-overhead utilities
- **Components**:
  - Lock-free queues (42ns operations)
  - Memory pools (26ns allocation)
  - Logging system (35ns overhead)
  - Cache-aligned containers
- **Design**: Header-only templates for zero-cost abstractions

### 3. Trade Engine Core
- **Purpose**: Central trading logic
- **Components**:
  - Strategy Engine (signal generation)
  - Order Manager (order lifecycle)
  - Position Keeper (P&L tracking)
  - Market Book (order book maintenance)
- **Latency Target**: < 5μs end-to-end

### 4. Risk Management Layer
- **Purpose**: Real-time risk controls
- **Features**:
  - Pre-trade risk checks
  - Position limits
  - Loss limits
  - Rate limiting
- **Latency Impact**: < 100ns per check

## Data Flow

### Market Data Flow
```
Exchange → Socket → Parser → Queue → Strategy → Signal
         1μs      100ns    42ns    200ns    = ~1.5μs
```

### Order Flow
```
Signal → Risk → Order → Queue → Socket → Exchange
       100ns   200ns   42ns    1μs     = ~1.5μs
```

### Total Round Trip
```
Market Data In → Processing → Order Out = ~3μs typical
```

## Memory Architecture

### Cache-Line Optimization
```cpp
struct alignas(64) TradingData {
    // Hot data - frequently accessed together
    std::atomic<uint64_t> sequence_num;
    Price last_price;
    Quantity last_qty;
    char padding[40];  // Ensure 64-byte alignment
};
```

### Memory Pools
- Pre-allocated at startup
- Fixed-size blocks
- O(1) allocation/deallocation
- Zero fragmentation

## Thread Architecture

### Thread Topology
```
CPU 0: OS/Interrupts (isolated)
CPU 1: Market Data Consumer (pinned, RT priority)
CPU 2: Strategy Engine (pinned, RT priority)
CPU 3: Order Gateway (pinned, RT priority)
CPU 4: Risk Manager (pinned, RT priority)
CPU 5: Logging Thread (normal priority)
CPU 6-7: Auxiliary services
```

### Inter-Thread Communication
- Lock-free SPSC/MPSC queues
- Cache-aligned message passing
- No mutexes in hot path
- Wait-free algorithms where possible

## Performance Characteristics

### Latency Breakdown (Typical)
| Component | Latency | Percentage |
|-----------|---------|------------|
| Network I/O | 1-2μs | 40% |
| Parsing | 100-200ns | 5% |
| Strategy | 200-500ns | 10% |
| Risk Checks | 100-200ns | 5% |
| Order Creation | 100ns | 3% |
| Queue Operations | 84ns | 2% |
| Serialization | 100-200ns | 5% |
| Network Out | 1-2μs | 30% |
| **Total** | **3-5μs** | **100%** |

### Throughput Metrics
- Market Data: 1M+ messages/second
- Order Rate: 100K+ orders/second
- Strategy Calculations: 10M+ signals/second

## Scalability Considerations

### Horizontal Scaling
- Multiple instances per exchange
- Symbol partitioning
- Independent strategy instances

### Vertical Scaling
- NUMA-aware memory allocation
- CPU core scaling (up to 128 cores tested)
- Kernel bypass networking (future)

## Fault Tolerance

### Failure Modes
1. **Exchange Disconnect**: Automatic reconnection with exponential backoff
2. **Strategy Crash**: Isolated in separate process
3. **Risk Breach**: Immediate position closeout
4. **System Overload**: Circuit breakers and rate limiting

### Recovery Mechanisms
- Persistent session management
- Order state recovery
- Position reconciliation
- Audit trail for all actions

## Security Architecture

### Authentication
- API key management
- TOTP-based 2FA (Zerodha)
- HMAC signatures (Binance)
- Session token rotation

### Network Security
- TLS 1.3 for all connections
- Certificate pinning
- IP whitelisting
- Rate limiting per IP

## Monitoring & Observability

### Metrics Collection
- Nanosecond-precision timestamps
- Lock-free metric aggregation
- Zero-overhead when disabled

### Key Metrics
- Order latency percentiles (p50, p99, p99.9)
- Message rates
- Queue depths
- Memory usage
- CPU utilization per thread

## Future Architecture Enhancements

### Planned Improvements
1. **Kernel Bypass Networking** (DPDK/XDP)
2. **FPGA Acceleration** for parsing
3. **Colocated Deployment** optimization
4. **Multi-Asset Support** (Options, Futures, Crypto)
5. **Machine Learning Integration** for signals

### Research Areas
- Hardware timestamping
- InfiniBand support
- Persistent memory (Intel Optane)
- GPU acceleration for complex strategies

---

*Next: [Component Design](02_component_design.md) →*