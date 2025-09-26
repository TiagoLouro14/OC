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

## 🧪 Testing (Scripts & Manual)

Use the provided scripts to run all traces automatically:

```bash
# Make scripts executable (first time only)
chmod +x runtlbsiml1tests.sh runtlbsiml2tests.sh

# Run only L1 TLB expected outputs (early milestone)
./runtlbsiml1tests.sh

# Run full L1+L2 TLB expected outputs
./runtlbsiml2tests.sh
```

Each script creates a reports/ directory with, for every input trace:
- {input}.out  -> stdout produced by your simulator
- {input}.log  -> stdout + stderr (debug) combined
- {input}.diff -> diff against expected reference (empty file means match)

Manual single-run + diff examples:

```bash
# Build
make

# Run one trace (stdout only) and diff against full (L2) reference
./build/tlbsim inputs/expected_inputs_100.txt > /tmp/run.out
diff -y outputs/tlbsim-l2/expected_inputs_100.out /tmp/run.out

# If you are validating only L1 stage:
diff -y outputs/tlbsim-l1/expected_inputs_100.out /tmp/run.out

# Include debug (stderr) to compare with .log reference (still ignored for grading)
./build/tlbsim inputs/expected_inputs_100.txt > /tmp/run.log 2>&1
diff -y outputs/tlbsim-l2/expected_inputs_100.log /tmp/run.log
```

Notes:
- Only stdout is graded; stderr (debug via log_dbg) is ignored.
- A non-empty *.diff file indicates differences to resolve.
- Create your own traces to stress LRU, invalidations, and write-back paths.

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
