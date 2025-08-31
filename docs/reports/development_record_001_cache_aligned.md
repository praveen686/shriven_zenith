# Development Record 001: CacheAligned Template Design

**Date**: 2025-08-31  
**Component**: `bldg_blocks/types.h::CacheAligned<T>`  
**Issue**: Template constructor complexity and strict compiler warnings  
**Status**: In Progress

## Problem Statement

The `CacheAligned<T>` template struct needs to handle both atomic and non-atomic types while preventing false sharing in multi-threaded environments. Initial implementation had issues with:

1. Implicit type conversions causing strict compiler warnings
2. SFINAE-based constructor overloading being overly complex
3. Incorrect handling of atomic type initialization
4. Potential false sharing due to improper alignment

Example problematic usage:
```cpp
CacheAligned<std::atomic<uint64_t>> counter{0};  // int 0 → atomic<uint64_t> conversion warning
```

## Initial Approach (Problematic)

Attempted to use SFINAE with `std::enable_if_t`:
```cpp
template<typename U>
CacheAligned(const U& v, typename std::enable_if_t<!std::is_atomic_v<T>>* = nullptr) 
    : value(v) {}

template<typename U>  
CacheAligned(const U& v, typename std::enable_if_t<std::is_atomic_v<T>>* = nullptr)
    : value(static_cast<typename T::value_type>(v)) {}
```

### Issues with this approach:
- `std::is_atomic_v` doesn't exist in standard library
- SFINAE makes code hard to read and maintain
- Compiler couldn't resolve template specializations correctly
- Constructor overloading created ambiguity

## Recommended Solution

### 1. Custom Atomic Type Detector

**Requirement**: Reliable compile-time detection of `std::atomic<T>` types

```cpp
template<class> struct is_atomic : std::false_type {};
template<class U> struct is_atomic<std::atomic<U>> : std::true_type {};
template<class T> inline constexpr bool is_atomic_v = is_atomic<T>::value;
```

**Rationale**: 
- Standard library lacks `is_atomic_v`
- Clean template specialization pattern
- Compile-time constant for `if constexpr`

### 2. Memory Ordering in Constructor

**Requirement**: Use `memory_order_relaxed` for atomic initialization

```cpp
if constexpr (is_atomic_v<T>) {
  using V = typename T::value_type;
  value.store(static_cast<V>(v), std::memory_order_relaxed);
} else {
  value = v;
}
```

**Rationale**:
- Constructor is single-threaded initialization
- No cross-thread synchronization needed yet
- `relaxed` ordering is sufficient and fastest

### 3. Construction vs Assignment

**Requirement**: Proper in-place construction for non-atomic types

```cpp
template<typename U>
explicit CacheAligned(U&& u) : value() {
  init(std::forward<U>(u));
}

template<typename U>
void init(U&& u) {
  if constexpr (is_atomic_v<T>) {
    using V = typename T::value_type;
    value.store(static_cast<V>(std::forward<U>(u)), std::memory_order_relaxed);
  } else {
    // Placement new for non-assignable types
    ::new (std::addressof(value)) T(std::forward<U>(u));
  }
}
```

**Rationale**:
- Avoids default construct + assign pattern
- Works with non-assignable types
- Perfect forwarding preserves value categories

### 4. Alignment Requirements

**Requirement**: Guarantee cache-line alignment to prevent false sharing

```cpp
template<typename T, std::size_t Align = 64>
struct alignas(Align) CacheAligned {
  static_assert(Align % alignof(T) == 0, "Align must be multiple of alignof(T)");
  T value;
  char padding[Align - sizeof(T)];  // Ensure size is multiple of Align
};

// Compile-time verification
static_assert(alignof(CacheAligned<std::atomic<uint64_t>>) >= 64);
static_assert(sizeof(CacheAligned<std::atomic<uint64_t>>) % 64 == 0);
```

**Rationale**:
- 64 bytes is typical cache line size (x86_64)
- Size must be multiple of alignment for arrays
- Static assertions catch misalignment at compile time

### 5. Handle Const/Volatile Types

**Requirement**: Prevent invalid operations on const atomic types

```cpp
static_assert(!std::is_const_v<T> || !is_atomic_v<T>, 
              "CacheAligned cannot wrap const atomic types");
```

**Rationale**:
- `const std::atomic<T>` cannot use `store()`
- Better compile-time error than cryptic template errors

### 6. Alternative: Partial Specialization

**Option**: Separate specializations for atomic and non-atomic

```cpp
// Primary template for non-atomic types
template<typename T, std::size_t Align = 64>
struct alignas(Align) CacheAligned {
  T value;
  CacheAligned() : value() {}
  template<class U>
  explicit CacheAligned(U&& u) : value(std::forward<U>(u)) {}
};

// Specialization for atomic types
template<typename U, std::size_t Align>
struct alignas(Align) CacheAligned<std::atomic<U>, Align> {
  std::atomic<U> value;
  CacheAligned() : value() {}
  explicit CacheAligned(U init) : value() {
    value.store(init, std::memory_order_relaxed);
  }
};
```

**Pros**:
- Clearest intent and constraints
- Best compiler diagnostics
- No runtime branches (even though `if constexpr` is compile-time)

**Cons**:
- More code duplication
- Need to maintain two specializations

## Decision Matrix

| Approach | Complexity | Performance | Maintainability | Error Messages |
|----------|------------|-------------|-----------------|----------------|
| SFINAE | High | Excellent | Poor | Cryptic |
| `if constexpr` | Medium | Excellent | Good | Good |
| Partial Specialization | Low | Excellent | Good | Excellent |

## Final Recommendation

Use **`if constexpr` with proper type traits** for initial implementation:
- Balances simplicity and functionality
- Single point of maintenance
- Clear compile-time branching

Consider **partial specialization** if:
- Need clearest possible error messages
- Different member functions for atomic vs non-atomic
- Performance profiling shows any overhead (unlikely)

## Implementation Checklist

- [ ] Implement custom `is_atomic_v` trait
- [ ] Use `memory_order_relaxed` in constructor
- [ ] Add static assertions for alignment
- [ ] Ensure size is multiple of cache line
- [ ] Handle const atomic types
- [ ] Add unit tests for both atomic and non-atomic types
- [ ] Verify no false sharing with performance tests

## Lessons Learned

1. **Don't assume standard library completeness** - `is_atomic_v` doesn't exist
2. **Prefer `if constexpr` over SFINAE** for C++17+ code
3. **Memory ordering matters** - even in constructors
4. **Alignment isn't just about the declaration** - size matters for arrays
5. **Template error messages guide design** - if they're cryptic, simplify

## Performance Implications

- Cache line alignment prevents false sharing (up to 100x performance improvement)
- `memory_order_relaxed` in constructor avoids unnecessary memory fences
- Compile-time branching has zero runtime cost
- Proper size padding ensures array stride maintains alignment

## References

- [Intel 64 and IA-32 Architectures Optimization Reference Manual](https://www.intel.com/content/www/us/en/developer/articles/manual/64-ia-32-architectures-optimization-manual.html)
- [C++ Memory Model and Atomic Types](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [False Sharing in Multi-threaded Applications](https://mechanical-sympathy.blogspot.com/2011/07/false-sharing.html)

## Code Review Notes

**Reviewer**: User  
**Date**: 2025-08-31  
**Status**: Approved with recommendations

Key feedback:
- Solution is sound and clean
- Avoids SFINAE complexity appropriately  
- Makes atomic vs non-atomic semantics obvious
- Needs proper trait implementation and memory ordering

---

*This document serves as a record of design decisions and rationale for future reference and code maintenance.*