# Migration Plan: Eliminate Entry ↔ entry_t Conversion Layer

## Executive Summary
**Goal**: Remove redundant Entry ↔ entry_t conversions inherited from SGX EDL constraints, now unnecessary in TDX architecture.

**Approach**: Replace C++ Entry class with direct use of entry_t struct + C++ helper functions, keeping ALL C code unchanged.

**Scope**:
- ~30 C++ files (app/, main/, tests/)
- ~60 conversion call sites
- 2,361 lines of C code (UNTOUCHED)
- 24 test files to validate

**Timeline**: 4-6 days of focused work across 7 phases

---

## Phase 0: Pre-Migration Preparation (Day 0)

### 0.1 Establish Baseline
**Purpose**: Create reference outputs to verify no behavioral changes

```bash
# Build current system
make clean && make
make tests

# Run all tests and capture outputs
./scripts/run_tpch_tests.sh 0_001 > baseline_tpch_0_001.txt 2>&1
./scripts/run_tpch_tests.sh 0_01 > baseline_tpch_0_01.txt 2>&1

# Run unit tests
./test_merge_sort > baseline_merge_sort.txt 2>&1
./test_waksman_shuffle > baseline_waksman.txt 2>&1
./test_bottom_up > baseline_bottom_up.txt 2>&1
./test_top_down > baseline_top_down.txt 2>&1
./test_distribute_expand > baseline_distribute.txt 2>&1
./test_join_correctness > baseline_correctness.txt 2>&1

# Save actual output files
mkdir -p baseline_outputs
cp output/*.csv baseline_outputs/
```

**Deliverable**: Directory `baseline_outputs/` with all test results

### 0.2 Create Migration Branch
```bash
git checkout -b feature/remove-entry-conversion
git push -u origin feature/remove-entry-conversion
```

### 0.3 Audit Conversion Sites
**Purpose**: Document every conversion location for systematic replacement

```bash
# Find all to_entry_t calls
grep -rn "\.to_entry_t()" app/ main/ tests/ > audit_to_entry_t.txt

# Find all from_entry_t calls
grep -rn "\.from_entry_t(" app/ main/ tests/ > audit_from_entry_t.txt

# Find all Entry declarations
grep -rn "Entry " app/ main/ tests/ | grep -v entry_t > audit_entry_decl.txt

# Find vector<Entry> usage
grep -rn "vector<Entry>" app/ main/ tests/ > audit_vector_entry.txt
```

**Deliverable**: Audit files documenting all ~60 conversion sites

---

## Phase 1: Create C++ Compatibility Layer (Day 1)

### 1.1 Create entry_utils.h
**Location**: `common/entry_utils.h`

**Purpose**: Provide C++ convenience functions for entry_t without overhead

```cpp
#ifndef ENTRY_UTILS_H
#define ENTRY_UTILS_H

#include "enclave_types.h"
#include <string>
#include <sstream>
#include <cstring>

#ifdef __cplusplus

// C++ utility functions for entry_t (inline for zero overhead)

// Clear/reset entry
inline void entry_clear(entry_t& entry) {
    memset(&entry, 0, sizeof(entry_t));
}

// Initialize with defaults
inline void entry_init(entry_t& entry) {
    memset(&entry, 0, sizeof(entry_t));
    entry.field_type = SOURCE;
    entry.equality_type = EQ;
}

// String representation (for debugging)
inline std::string entry_to_string(const entry_t& entry) {
    std::stringstream ss;
    ss << "entry_t{type=" << entry.field_type
       << ", join_attr=" << entry.join_attr
       << ", local_mult=" << entry.local_mult
       << ", final_mult=" << entry.final_mult
       << ", attrs=[";
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (i > 0) ss << ", ";
        ss << entry.attributes[i];
    }
    ss << "]}";
    return ss.str();
}

// Comparison operators
inline bool entry_equal(const entry_t& a, const entry_t& b) {
    return a.join_attr == b.join_attr &&
           a.field_type == b.field_type &&
           a.original_index == b.original_index;
}

inline bool entry_less_than(const entry_t& a, const entry_t& b) {
    return a.join_attr < b.join_attr;
}

#endif // __cplusplus

#endif // ENTRY_UTILS_H
```

**Test**: Compile with `-Werror` to ensure no warnings

### 1.2 Update table.h to use entry_t
**Location**: `app/data_structures/table.h`

**Changes**:
```cpp
// OLD:
#include "entry.h"
class Table {
    std::vector<Entry> entries;
    // ...
};

// NEW:
#include "../../common/enclave_types.h"
#include "../../common/entry_utils.h"
class Table {
    std::vector<entry_t> entries;  // ← Changed
    // Keep all method signatures the same initially
};
```

**Test**: Compile-only test - should fail to link, that's OK for now

---

## Phase 2: Update Table Class Implementation (Day 1-2)

### 2.1 Update table.cpp - Entry Access Methods
**Location**: `app/data_structures/table.cpp`

**Pattern for all methods**:
```cpp
// OLD:
Entry& Table::get_entry(size_t index) {
    return entries[index];
}

// NEW: (same signature, different return type)
entry_t& Table::get_entry(size_t index) {
    return entries[index];
}
```

**Critical**: Public API unchanged, internal type changed

### 2.2 Remove Conversion Methods from Table
**Delete**:
- `Table::to_entry_t_vector()`
- `Table::from_entry_t_vector()`

**Replace calls with direct vector access**:
```cpp
// OLD:
std::vector<entry_t> c_vec = table.to_entry_t_vector();

// NEW:
std::vector<entry_t>& c_vec = table.get_entries_ref(); // Add this accessor
```

### 2.3 Add Direct Vector Access
**Add to table.h**:
```cpp
// Direct access for performance-critical paths
std::vector<entry_t>& get_entries_ref() { return entries; }
const std::vector<entry_t>& get_entries_ref() const { return entries; }
entry_t* data() { return entries.data(); }
const entry_t* data() const { return entries.data(); }
```

**Test After Phase 2**:
```bash
# Should compile but not link yet
make table.o
```

---

## Phase 3: Update Manager Classes (Day 2-3)

### 3.1 MergeSortManager
**Location**: `app/algorithms/merge_sort_manager.cpp`

**Key changes**:

```cpp
// OLD:
void MergeSortManager::sort_run_in_enclave(std::vector<Entry>& entries) {
    std::vector<entry_t> entry_array(size);
    for (size_t i = 0; i < size; i++) {
        entry_array[i] = entries[i].to_entry_t();  // ← REMOVE
    }

    heap_sort(entry_array.data(), size, compare);

    for (size_t i = 0; i < size; i++) {
        entries[i].from_entry_t(entry_array[i]);  // ← REMOVE
    }
}

// NEW: Direct operation
void MergeSortManager::sort_run_in_enclave(std::vector<entry_t>& entries) {
    comparator_func_t compare = get_merge_comparator(comparator_type);
    heap_sort(entries.data(), entries.size(), compare);  // ← Direct call!
}
```

**Update internal storage**:
```cpp
// OLD:
std::vector<std::vector<Entry>> runs;

// NEW:
std::vector<std::vector<entry_t>> runs;
```

**Critical section - k_way_merge**:
```cpp
// OLD: ~15 conversions in this function
entry_t e = runs[run_indices[i]][pos].to_entry_t();
heap_push(&heap, &e, i);

// NEW: Direct reference
heap_push(&heap, &runs[run_indices[i]][pos], i);
```

**Test After 3.1**:
```bash
make merge_sort_manager.o
# Manual inspection: no to_entry_t/from_entry_t calls should remain
grep -n "to_entry_t\|from_entry_t" app/algorithms/merge_sort_manager.cpp
# Should output: (empty)
```

### 3.2 ShuffleManager
**Location**: `app/algorithms/shuffle_manager.cpp`

**Similar pattern**:
```cpp
// OLD:
void ShuffleManager::shuffle_small(std::vector<Entry>& entries) {
    std::vector<entry_t> c_entries;
    for (const auto& e : entries) {
        c_entries.push_back(e.to_entry_t());
    }
    oblivious_2way_waksman(c_entries.data(), n);
    entries.clear();
    for (size_t i = 0; i < n; i++) {
        Entry e;
        e.from_entry_t(c_entries[i]);
        entries.push_back(e);
    }
}

// NEW: Direct operation
void ShuffleManager::shuffle_small(std::vector<entry_t>& entries) {
    oblivious_2way_waksman(entries.data(), entries.size());
    // That's it! No conversion!
}
```

**Update group storage**:
```cpp
// OLD:
std::vector<std::vector<Entry>> groups;
std::vector<Entry> output_entries;

// NEW:
std::vector<std::vector<entry_t>> groups;
std::vector<entry_t> output_entries;
```

**Test After 3.2**:
```bash
make shuffle_manager.o
grep -n "to_entry_t\|from_entry_t" app/algorithms/shuffle_manager.cpp
# Should output: (empty)
```

---

## Phase 4: Update Algorithm Phase Files (Day 3-4)

### 4.1 bottom_up_phase.cpp
**Location**: `app/algorithms/bottom_up_phase.cpp`

**Pattern - Direct C function calls**:
```cpp
// OLD:
{
    entry_t e = table[0].to_entry_t();
    transform_set_index_op(&e, 0);
    table[0].from_entry_t(e);
}

// NEW: Direct access
{
    transform_set_index_op(&table[0], 0);  // table[0] is entry_t&
}
```

**Table operations already direct**: No changes needed for:
- `table.map()`
- `table.linear_pass()`
- `table.shuffle_merge_sort()`

These internally already use entry_t* now!

### 4.2 top_down_phase.cpp
**Pattern**: Same as bottom_up - remove wrapper conversions

### 4.3 distribute_expand.cpp
**Pattern**: Same as above

### 4.4 align_concat.cpp
**Pattern**: Same as above

**Test After Phase 4**:
```bash
make app/algorithms/*.o
grep -rn "to_entry_t\|from_entry_t" app/algorithms/
# Should output: (empty)
```

---

## Phase 5: Update Supporting Files (Day 4)

### 5.1 JoinAttributeSetter
**Location**: `app/join/join_attribute_setter.cpp`

Replace `Entry` with `entry_t` in:
- Method parameters
- Local variables
- Table access

### 5.2 File I/O (table_io.cpp, converters.cpp)
**Location**: `app/file_io/`

**Critical**: CSV reading/writing

```cpp
// OLD:
Entry entry;
entry.attributes[0] = std::stoi(fields[0]);
table.add_entry(entry);

// NEW: Same code, different type
entry_t entry;
entry_init(entry);  // Use utility function
entry.attributes[0] = std::stoi(fields[0]);
table.add_entry(entry);
```

### 5.3 Main Entry Point
**Location**: `main/sgx_join/main.cpp`

Update any Entry references to entry_t

**Test After Phase 5**:
```bash
make app/*.o main/*.o
grep -rn "class Entry\|Entry " app/ main/ | grep -v entry_t | grep -v "//.*Entry"
# Should show minimal or no results
```

---

## Phase 6: Delete Entry Class (Day 4)

### 6.1 Remove Files
```bash
git rm app/data_structures/entry.h
git rm app/data_structures/entry.cpp
```

### 6.2 Update Includes
**Find all Entry includes**:
```bash
grep -rn '#include.*entry\.h"' app/ main/ tests/
```

**Replace with**:
```cpp
// OLD:
#include "entry.h"

// NEW:
#include "../../common/enclave_types.h"
#include "../../common/entry_utils.h"
```

### 6.3 First Full Build
```bash
make clean && make 2>&1 | tee build_phase6.log
```

**Expected**: May have compilation errors - fix iteratively:
- Missing includes
- Type mismatches
- Method call syntax

**Iterate**: Fix each error, rebuild, repeat until clean build

**Test After Phase 6**:
```bash
# Should build successfully
make clean && make
./sgx_app --version  # Smoke test
```

---

## Phase 7: Testing & Validation (Day 5-6)

### 7.1 Unit Tests
**Update test files** in `tests/unit/` to use entry_t:

```bash
# List of test files to update
tests/unit/test_merge_sort.cpp
tests/unit/test_waksman_shuffle.cpp
tests/unit/test_bottom_up.cpp
tests/unit/test_top_down.cpp
tests/unit/test_distribute_expand.cpp
tests/unit/test_join_correctness.cpp
# ... + 18 more
```

**Pattern for test updates**:
```cpp
// OLD:
Entry e;
e.join_attr = 42;

// NEW:
entry_t e;
entry_init(e);
e.join_attr = 42;
```

**Build tests**:
```bash
make tests 2>&1 | tee build_tests.log
```

### 7.2 Run Unit Tests
```bash
# Run each test and compare to baseline
./test_merge_sort > new_merge_sort.txt 2>&1
diff baseline_merge_sort.txt new_merge_sort.txt

./test_waksman_shuffle > new_waksman.txt 2>&1
diff baseline_waksman.txt new_waksman.txt

./test_bottom_up > new_bottom_up.txt 2>&1
diff baseline_bottom_up.txt new_bottom_up.txt

# ... repeat for all 24 tests
```

**Success Criteria**:
- All tests pass
- Output diffs show ONLY:
  - Timing differences (acceptable)
  - Debug message format changes (acceptable)
- NO differences in:
  - Join results
  - Table sizes
  - Attribute values
  - Sort orders

### 7.3 Integration Tests - TPC-H Queries
```bash
# Run full TPC-H test suite
./scripts/run_tpch_tests.sh 0_001 > new_tpch_0_001.txt 2>&1
./scripts/run_tpch_tests.sh 0_01 > new_tpch_0_01.txt 2>&1

# Compare results
diff baseline_tpch_0_001.txt new_tpch_0_001.txt
diff baseline_tpch_0_01.txt new_tpch_0_01.txt

# Compare output files (most important!)
for f in baseline_outputs/*.csv; do
    fname=$(basename "$f")
    diff "$f" "output/$fname" || echo "DIFF FOUND: $fname"
done
```

**Success Criteria**:
- All queries produce IDENTICAL output CSVs (byte-for-byte)
- Match: YES for all comparisons
- No crashes or segfaults

### 7.4 Performance Benchmarking
**Measure conversion overhead removed**:

```bash
# Time a large join before migration (from baseline branch)
git checkout main
make clean && make
time ./sgx_app input/queries/tpch_complex.sql input/plaintext/data_0_01 output_before.csv

# Time same join after migration
git checkout feature/remove-entry-conversion
make clean && make
time ./sgx_app input/queries/tpch_complex.sql input/plaintext/data_0_01 output_after.csv

# Compare
diff output_before.csv output_after.csv  # Should be identical
# Compare timing - expect 5-15% speedup
```

**Expected Results**:
- Identical outputs
- Faster execution (5-15% improvement)
- Lower memory bandwidth usage

### 7.5 Obliviousness Verification
**Critical**: Ensure no side-channel leakage

**Method 1: Access Pattern Logging** (if enabled)
```bash
# Run with two different datasets but same size
DEBUG=1 make
./sgx_app query.sql dataset1/ out1.csv 2>&1 | grep "ACCESS:" > accesses1.log
./sgx_app query.sql dataset2/ out2.csv 2>&1 | grep "ACCESS:" > accesses2.log

# Compare access patterns
diff accesses1.log accesses2.log
# Should be IDENTICAL (oblivious property)
```

**Method 2: Manual Code Inspection**
- Verify NO new if/else branches based on data values added
- Verify all comparator_*_op functions unchanged
- Verify oblivious_swap unchanged
- Verify all window/transform functions unchanged

**Success Criteria**:
- Access patterns identical across different data
- Zero new data-dependent branches in core logic
- All oblivious primitives unchanged

### 7.6 Memory Safety Checks
```bash
# Valgrind full check
valgrind --leak-check=full --show-leak-kinds=all \
    ./sgx_app input/queries/tpch_tb1.sql input/plaintext/data_0_001 output.csv \
    2>&1 | tee valgrind_check.log

# Look for:
# - No memory leaks
# - No invalid reads/writes
# - No use of uninitialized values
```

**Success Criteria**:
- Zero memory leaks
- Zero invalid memory accesses
- Clean Valgrind run

---

## Phase 8: Cleanup & Documentation (Day 6)

### 8.1 Code Cleanup
- Remove dead code (Entry class remnants)
- Remove unused includes
- Update comments referencing "Entry class"
- Run code formatter

### 8.2 Update Documentation
**Files to update**:
- `CLAUDE.md` - Remove Entry class references
- `TDX_MIGRATION_SUMMARY.md` - Add section on conversion removal
- `README.md` - Update architecture description

**Add new section**:
```markdown
## Post-TDX Optimization: Entry Type Unification

After migrating from SGX to TDX, we eliminated the redundant Entry ↔ entry_t
conversion layer. In SGX, the EDL (Enclave Definition Language) required C types,
necessitating a C struct (entry_t) separate from the C++ class (Entry).

In TDX, this boundary no longer exists. We now use entry_t directly throughout
the codebase, with C++ utility functions providing convenience methods. This
eliminates ~60 conversion sites and reduces memory copying overhead by ~5-15%.

**Key changes**:
- Removed: Entry class (entry.h/entry.cpp)
- Kept: entry_t struct (enclave_types.h)
- Added: C++ utilities (entry_utils.h)
- Result: Zero-copy operations between C++ and C code
```

### 8.3 Update Build System
**Update Makefile comments**:
```makefile
# C++ source files - use entry_t directly (no Entry class conversion)
App_Cpp_Files := ...

# C source files - oblivious primitives (unchanged)
App_C_Files := app/core_logic/algorithms/*.c ...
```

---

## Rollback Plan

### If Issues Found at Any Phase:

**Phase 0-5**: Easy rollback
```bash
git checkout -- <affected_files>
git status
```

**Phase 6+**: Branch-level rollback
```bash
git checkout main
make clean && make
# Keep feature branch for later retry
```

### Incremental Merge Strategy:
```bash
# Don't merge everything at once
git checkout main
git merge --no-ff --no-commit feature/remove-entry-conversion

# Test in main
make clean && make && make tests
./scripts/run_tpch_tests.sh 0_001

# If good:
git commit -m "Eliminate Entry ↔ entry_t conversion layer

- Remove Entry class (SGX legacy)
- Use entry_t directly throughout codebase
- Add C++ utility functions (entry_utils.h)
- Zero changes to C oblivious primitives
- All tests pass, outputs identical
- Performance improvement: ~10% faster"

# If bad:
git reset --hard HEAD
```

---

## Risk Assessment & Mitigation

### High Risks:
1. **Breaking oblivious properties**
   - Mitigation: NO changes to .c files, access pattern testing
   - Checkpoint: Phase 7.5 verification

2. **Test failures due to subtle bugs**
   - Mitigation: Byte-for-byte output comparison
   - Checkpoint: Phase 7.3 CSV diffs

3. **Memory layout issues**
   - Mitigation: Valgrind checks, sizeof() assertions
   - Checkpoint: Phase 7.6 memory safety

### Medium Risks:
4. **Compilation errors in tests**
   - Mitigation: Incremental compilation, fix-as-you-go
   - Checkpoint: Phase 7.1 test builds

5. **Performance regression**
   - Mitigation: Before/after benchmarks
   - Checkpoint: Phase 7.4 timing comparisons

### Low Risks:
6. **Documentation outdated**
   - Mitigation: Update in Phase 8
   - Impact: Low (doesn't affect functionality)

---

## Success Metrics

### Must-Have (Go/No-Go):
✅ All 24 unit tests pass
✅ All TPC-H queries produce identical outputs
✅ Zero Valgrind errors
✅ Zero oblivious property changes
✅ Clean build with no warnings

### Should-Have (Nice-to-Have):
✅ 5-15% performance improvement
✅ Reduced code complexity (fewer files)
✅ Cleaner architecture (single type)

### Metrics to Track:
- Build time: Before vs After
- Binary size: Before vs After
- Test execution time: Before vs After
- Lines of code: Before vs After (expect -200 lines)
- Conversion calls: 60 → 0

---

## Timeline Summary

| Phase | Days | Key Deliverable |
|-------|------|-----------------|
| 0: Prep | 0.5 | Baseline outputs |
| 1: Compat Layer | 0.5 | entry_utils.h |
| 2: Table Class | 1.0 | table.cpp updated |
| 3: Managers | 1.0 | No conversions in managers |
| 4: Algorithms | 1.0 | Phase files clean |
| 5: Supporting | 0.5 | I/O and helpers updated |
| 6: Delete Entry | 0.5 | Entry class removed, builds |
| 7: Testing | 1.5 | All tests pass |
| 8: Cleanup | 0.5 | Docs updated |
| **Total** | **6.5 days** | **Production ready** |

---

## Final Checklist

Before marking complete:

- [ ] All .c files unchanged (verify with `git diff`)
- [ ] Zero `to_entry_t()` calls remain (`grep -r`)
- [ ] Zero `from_entry_t()` calls remain (`grep -r`)
- [ ] Entry class files deleted
- [ ] All tests pass
- [ ] TPC-H outputs identical
- [ ] Valgrind clean
- [ ] Performance improved
- [ ] Documentation updated
- [ ] Code review completed
- [ ] Git history clean (squashed commits)

---

## Post-Migration Tasks

After successful merge:
1. Close related GitHub issues
2. Update project README with new architecture
3. Consider publishing blog post about the optimization
4. Benchmark against other TDX applications
5. Consider open-sourcing C oblivious primitives as standalone library

---

This plan prioritizes **safety over speed**. Each phase has clear checkpoints and rollback options. The C layer remains completely untouched, preserving all security properties.
