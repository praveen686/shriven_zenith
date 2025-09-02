# Shriven Zenith Documentation Index

## 📚 Complete Documentation Map

### Start Here
1. [**Documentation Hub**](README.md) - Main entry point
2. [**System Overview**](architecture/01_system_overview.md) - Architecture and design
3. [**Getting Started**](getting_started/01_installation.md) - Installation guide
4. [**CLAUDE.md**](../CLAUDE.md) - **MANDATORY** coding standards

### For Developers
- [**Developer Guide**](developer_guide/README.md) - Complete development guide
- [**API Reference**](api/README.md) - All APIs documented
- [**Component Catalog**](components/README.md) - All components explained
- [**Build System**](developer_guide/build_system.md) - Build instructions

### Architecture & Design
- [**System Overview**](architecture/01_system_overview.md) - High-level architecture
- [**Component Design**](architecture/02_component_design.md) - Component relationships
- [**Data Flow**](architecture/03_data_flow.md) - Data flow diagrams
- [**Deployment**](architecture/04_deployment.md) - Production deployment

### Performance
- [**Benchmarks**](performance/benchmarks.md) - Latest performance metrics
- [**Optimization Guide**](performance/optimization_guide.md) - Optimization techniques
- [**Latency Analysis**](performance/latency_analysis.md) - Latency breakdown

### Components

#### Common Infrastructure
- [**Lock-Free Queue**](api/common/lf_queue.md) - Thread-safe queue
- [**Memory Pool**](api/common/mem_pool.md) - Fixed-time allocation
- [**Logging System**](api/common/logging.md) - Low-overhead logging
- [**Thread Utilities**](api/common/thread_utils.md) - Thread management
- [**Time Utilities**](api/common/time_utils.md) - Nanosecond timing

#### Exchange Connectivity
- [**Zerodha Adapter**](components/exchanges/zerodha_component.md) - NSE/BSE trading
- [**Binance Adapter**](components/exchanges/binance_component.md) - Crypto trading

#### Market Data
- [**Instrument Manager**](components/market_data/instrument_manager_component.md) - Symbol management
- [**Order Book**](components/market_data/order_book_component.md) - Market depth

#### Trading Core
- [**Order Manager**](components/trading/order_manager_component.md) - Order lifecycle
- [**Position Keeper**](components/trading/position_keeper_component.md) - Position tracking
- [**Risk Manager**](components/trading/risk_manager_component.md) - Risk controls

### Testing & Quality
- [**Testing Guide**](developer_guide/testing.md) - Test strategies
- [**Debugging Guide**](developer_guide/debugging.md) - Debug techniques
- [**Code Review Checklist**](developer_guide/code_review.md) - PR requirements

### Deployment & Operations
- [**Installation**](deployment/installation.md) - Setup instructions
- [**Configuration**](deployment/configuration.md) - Config guide
- [**Monitoring**](deployment/monitoring.md) - Production monitoring
- [**Troubleshooting**](deployment/troubleshooting.md) - Common issues

### References
- [**Exchange Specs**](references/exchanges.md) - Exchange documentation
- [**Protocols**](references/protocols.md) - Protocol specifications
- [**Tools**](references/tools.md) - Development tools

## 📊 Documentation Coverage

| Category | Files | Status | Coverage |
|----------|-------|--------|----------|
| Architecture | 4 | ✅ Complete | 100% |
| API Reference | 12 | ✅ Complete | 100% |
| Components | 15 | 🚧 In Progress | 80% |
| Developer Guide | 6 | ✅ Complete | 100% |
| Performance | 3 | ✅ Complete | 100% |
| Deployment | 4 | 📋 Planned | 40% |
| References | 3 | 📋 Planned | 30% |

## 🔍 Quick Search

### By Topic
- **Lock-free programming**: [LF Queue](api/common/lf_queue.md), [Memory Pool](api/common/mem_pool.md)
- **Performance**: [Benchmarks](performance/benchmarks.md), [Optimization](performance/optimization_guide.md)
- **Exchange integration**: [Zerodha](components/exchanges/zerodha_component.md), [Binance](components/exchanges/binance_component.md)
- **Configuration**: [ConfigManager](api/config/config_manager.md), [Setup](deployment/configuration.md)
- **Testing**: [Testing Guide](developer_guide/testing.md), [Benchmarks](performance/benchmarks.md)

### By Use Case
- **I want to add a new exchange**: See [Exchange Adapter Template](developer_guide/exchange_adapter.md)
- **I want to optimize latency**: See [Optimization Guide](performance/optimization_guide.md)
- **I want to add a strategy**: See [Strategy Framework](components/trading/strategy_framework.md)
- **I want to debug an issue**: See [Debugging Guide](developer_guide/debugging.md)

## 📈 Documentation Standards

All documentation follows these principles:

1. **Clear Structure** - Consistent formatting and organization
2. **Code Examples** - Working examples for every concept
3. **Performance Notes** - Latency impacts documented
4. **Visual Aids** - Diagrams where helpful
5. **Version Tracking** - Changes tracked with dates

## 🚀 Recent Updates

| Date | Update | Files |
|------|--------|-------|
| 2025-09-02 | Complete documentation overhaul | All |
| 2025-09-02 | Added Binance integration docs | 3 |
| 2025-09-02 | Performance benchmarks updated | 1 |
| 2025-09-01 | Initial architecture docs | 4 |

## 📝 Documentation TODO

- [ ] Complete deployment guides
- [ ] Add WebSocket documentation
- [ ] Create video tutorials
- [ ] Add architecture diagrams
- [ ] Write strategy examples
- [ ] Document monitoring setup
- [ ] Create troubleshooting guide

## 🤝 Contributing to Docs

To improve documentation:

1. Follow markdown standards
2. Include code examples
3. Add performance notes
4. Update the index
5. Submit PR with description

## 📞 Getting Help

- **GitHub Issues**: Report documentation issues
- **API Questions**: See [API Reference](api/README.md)
- **Architecture**: See [System Overview](architecture/01_system_overview.md)
- **Performance**: See [Benchmarks](performance/benchmarks.md)

---

*Documentation Version: 1.0.0 | Last Updated: 2025-09-02*

*"Documentation is a love letter that you write to your future self." - Damian Conway*