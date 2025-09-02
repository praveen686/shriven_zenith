# Lock-Free Queue API

## Overview

High-performance, cache-aligned, lock-free queue implementation for inter-thread communication.

**Header**: `common/lf_queue.h`  
**Namespace**: `Common`  
**Thread Safety**: Lock-free (SPSC/MPSC variants)  
**Latency**: 42ns typical, 100ns worst-case  

## Class Template

```cpp
template<typename T>
class LFQueue {
public:
    explicit LFQueue(size_t capacity);
    
    [[nodiscard]] auto enqueue(const T& item) noexcept -> bool;
    [[nodiscard]] auto enqueue(T&& item) noexcept -> bool;
    [[nodiscard]] auto dequeue(T& item) noexcept -> bool;
    [[nodiscard]] auto size() const noexcept -> size_t;
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto capacity() const noexcept -> size_t;
};
```

## API Reference

### Constructor

```cpp
explicit LFQueue(size_t capacity)
```

Creates a lock-free queue with fixed capacity.

**Parameters:**
- `capacity`: Maximum number of elements (must be power of 2)

**Complexity:** `O(n)` initialization  
**Memory:** Pre-allocates `capacity * sizeof(T)` bytes  
**Thread Safety:** Not thread-safe (call before sharing)

**Example:**
```cpp
Common::LFQueue<Order> orderQueue(1024);  // 1024 orders max
```

### enqueue

```cpp
[[nodiscard]] auto enqueue(const T& item) noexcept -> bool
[[nodiscard]] auto enqueue(T&& item) noexcept -> bool
```

Adds an item to the queue.

**Parameters:**
- `item`: Item to enqueue (copy or move)

**Returns:** `true` if successful, `false` if queue full

**Complexity:** `O(1)`  
**Latency:** 42ns typical  
**Thread Safety:** Lock-free for single producer  
**Memory:** No allocation

**Example:**
```cpp
Order order{...};
if (!orderQueue.enqueue(std::move(order))) {
    LOG_WARN("Queue full, order dropped");
}
```

### dequeue

```cpp
[[nodiscard]] auto dequeue(T& item) noexcept -> bool
```

Removes an item from the queue.

**Parameters:**
- `item`: Output parameter to receive dequeued item

**Returns:** `true` if item dequeued, `false` if queue empty

**Complexity:** `O(1)`  
**Latency:** 42ns typical  
**Thread Safety:** Lock-free for single consumer  
**Memory:** No allocation

**Example:**
```cpp
Order order;
while (orderQueue.dequeue(order)) {
    processOrder(order);
}
```

### size

```cpp
[[nodiscard]] auto size() const noexcept -> size_t
```

Returns approximate number of elements in queue.

**Returns:** Current size (may be stale in concurrent access)

**Complexity:** `O(1)`  
**Latency:** 15ns  
**Thread Safety:** Lock-free read  

### empty

```cpp
[[nodiscard]] auto empty() const noexcept -> bool
```

Checks if queue is empty.

**Returns:** `true` if empty

**Complexity:** `O(1)`  
**Latency:** 10ns  
**Thread Safety:** Lock-free read

### capacity

```cpp
[[nodiscard]] auto capacity() const noexcept -> size_t
```

Returns maximum capacity.

**Returns:** Maximum number of elements

**Complexity:** `O(1)`  
**Latency:** 5ns  
**Thread Safety:** Safe (immutable)

## Usage Patterns

### Single Producer, Single Consumer (SPSC)
```cpp
// Producer thread
void producer(LFQueue<Data>& queue) {
    Data data{...};
    while (queue.enqueue(data)) {
        // Successfully enqueued
    }
}

// Consumer thread
void consumer(LFQueue<Data>& queue) {
    Data data;
    while (running) {
        if (queue.dequeue(data)) {
            process(data);
        }
    }
}
```

### Multiple Producer, Single Consumer (MPSC)
```cpp
// Multiple producers need external synchronization
std::atomic<uint64_t> sequence{0};

void producer(LFQueue<Data>& queue) {
    Data data{...};
    data.sequence = sequence.fetch_add(1);
    queue.enqueue(data);
}
```

## Performance Characteristics

### Benchmark Results
```
Operation       Median   p99     p99.9   Max
enqueue()       42ns     65ns    89ns    156ns
dequeue()       38ns     61ns    85ns    142ns
empty check     10ns     12ns    15ns    23ns
```

### Cache Behavior
- Queue is cache-aligned (64 bytes)
- Head and tail on separate cache lines (no false sharing)
- Data stored contiguously for prefetching

### Memory Layout
```
[Metadata | 64B aligned]
[Head     | 64B aligned]
[Tail     | 64B aligned]
[Data     | Contiguous array]
```

## Best Practices

### DO ✅
- Pre-size queue appropriately (power of 2)
- Use move semantics for large objects
- Check return values
- Dedicate threads for producer/consumer

### DON'T ❌
- Resize queue after creation
- Use from multiple producers without synchronization
- Block on empty queue (busy-wait instead)
- Mix SPSC and MPSC patterns

## Error Handling

The queue never throws exceptions. All errors are indicated by return values:

| Condition | Return | Action |
|-----------|--------|--------|
| Queue full | `false` from `enqueue()` | Drop message or retry |
| Queue empty | `false` from `dequeue()` | Retry or yield |
| Bad allocation | Constructor fails | Check system memory |

## Implementation Notes

### Lock-Free Algorithm
Uses compare-and-swap (CAS) operations with memory ordering:
- `memory_order_acquire` for loads
- `memory_order_release` for stores
- `memory_order_acq_rel` for CAS

### ABA Problem Prevention
Uses tagged pointers with generation counter to prevent ABA issues.

### Cache Line Padding
```cpp
struct alignas(64) Node {
    T data;
    std::atomic<Node*> next;
    char padding[64 - sizeof(T) - sizeof(std::atomic<Node*>)];
};
```

## Related APIs

- [Memory Pool](mem_pool.md) - For queue node allocation
- [Thread Utils](thread_utils.md) - For CPU pinning
- [Cache Aligned](cache_aligned.md) - For alignment wrapper

---

*See also: [MPMC Queue](mpmc_queue.md) | [Ring Buffer](ring_buffer.md)*