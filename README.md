# OC - Computer Organization Project

A memory management simulation project implementing Translation Lookaside Buffer (TLB) and page table mechanisms.

## 🚀 Features

- **Two-level TLB implementation** (L1 and L2 caches)
- **Page table translation** with virtual to physical address mapping
- **LRU replacement policy** for cache management
- **Memory access simulation** with read/write operations
- **Performance statistics** tracking hits, misses, and invalidations

## 🏗️ Project Structure

```
src/
├── tlb.c          # TLB implementation with L1/L2 cache logic
├── page_table.c   # Page table translation mechanisms
├── memory.c       # Memory management utilities
├── clock.c        # Timing and clock utilities
└── main.c         # Main simulation driver
```

## 📊 Performance Metrics

The simulator tracks various performance metrics:
- TLB L1/L2 hit rates
- Page fault statistics
- Memory access cycles
- Cache invalidations

## 🛠️ Build & Run

```bash
# Build the project
make

# Run with test inputs
./tlbsim inputs/test_file.txt

# Clean build artifacts
make clean
```

## 📈 Example Output

```
Total instructions executed: 100
Total TLB L1 hits: 87 (87.00%)
Total TLB L2 hits: 11 (11.00%)
Total page faults: 13
Elapsed: 1400 ns
```

## 🎯 Implementation Details

- **L1 TLB**: Fast, small cache for recent translations
- **L2 TLB**: Larger, secondary cache for additional translations
- **LRU Policy**: Least Recently Used replacement for cache management
- **Virtual Memory**: Full virtual-to-physical address translation

---

*Computer Organization Course Project - Instituto Superior Técnico*