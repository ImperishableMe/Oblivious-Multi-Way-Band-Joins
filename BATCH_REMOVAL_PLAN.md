# Batch Layer Removal - Complete Implementation Plan

## Context & Background

### Why Remove Batching?
The batching system was designed for Intel SGX to reduce ecall/ocall overhead (8,000-12,000 CPU cycles per boundary crossing). After TDX migration:
- ❌ No enclave boundary - everything runs in same VM
- ❌ No ecall overhead - direct function calls
- ❌ Unnecessary complexity - Entry ↔ entry_t conversions
- ❌ Deduplication overhead - no longer needed
- ✅ Can call core functions directly with zero overhead

### Current Batching Architecture

```
Table::batched_map(OpEcall op_type)
  ↓
EcallBatchCollector collector(op_type)
  ↓ (collects operations, deduplicates entries)
collector.add_operation(entry1, entry2)
  ↓ (converts Entry → entry_t)
collector.flush()
  ↓
batch_dispatcher(entry_t array, ops, op_type)
  ↓ (giant switch statement on op_type)
switch(op_type) {
  case OP_ECALL_COMPARATOR_JOIN_ATTR:
    comparator_join_attr_op(&data[idx1], &data[idx2])
  ...
}
  ↓ (writes back entry_t → Entry)
collector.write_back_results()
```

### Target Direct Call Architecture

```
Table::map(OpEcall op_type)
  ↓
for each entry:
  entry_t e = entry.to_entry_t()
  call core_function(&e)  // Direct call via function pointer
  entry.from_entry_t(e)
```

### Files Involved

**To Delete (4 files):**
- `app/batch/ecall_batch_collector.h`
- `app/batch/ecall_batch_collector.cpp`
- `app/batch/ecall_wrapper.h`
- `app/batch/ecall_wrapper.cpp`
- `app/core_logic/batch/batch_dispatcher.h`
- `app/core_logic/batch/batch_dispatcher.c`

**To Modify:**
- `app/data_structures/table.h` (rename batched_* → map/linear_pass/etc)
- `app/data_structures/table.cpp` (implement direct calls)
- `app/algorithms/distribute_expand.cpp` (update callers)
- `app/algorithms/align_concat.cpp` (update callers)
- `app/algorithms/bottom_up_phase.cpp` (update callers)
- `app/algorithms/top_down_phase.cpp` (update callers)
- `app/algorithms/oblivious_join.cpp` (update callers)
- `Makefile` (remove batch object files)

**To Keep (temporarily):**
- `common/batch_types.h` - OpEcall enum still useful for now

---

## Phase 1: Implement Direct Table Operations

### Goal
Replace batched operations with direct function calls while keeping the same API surface (just rename methods).

### 1.1 Update Table Class Declaration

**File:** `app/data_structures/table.h`

**Changes:**
```cpp
// OLD NAMES (lines ~91-130):
Table batched_map(OpEcall op_type, int32_t* params = nullptr) const;
void batched_linear_pass(OpEcall op_type, int32_t* params = nullptr);
void batched_parallel_pass(Table& other, OpEcall op_type, int32_t* params = nullptr);
void batched_distribute_pass(size_t distance, OpEcall op_type, int32_t* params = nullptr);
void add_batched_padding(size_t count, OpEcall padding_op = OP_ECALL_TRANSFORM_CREATE_DIST_PADDING);

// NEW NAMES:
Table map(OpEcall op_type, int32_t* params = nullptr) const;
void linear_pass(OpEcall op_type, int32_t* params = nullptr);
void parallel_pass(Table& other, OpEcall op_type, int32_t* params = nullptr);
void distribute_pass(size_t distance, OpEcall op_type, int32_t* params = nullptr);
void add_padding(size_t count, OpEcall padding_op = OP_ECALL_TRANSFORM_CREATE_DIST_PADDING);
```

**Keep shuffle_merge_sort, pad_to_shuffle_size, etc unchanged.**

### 1.2 Implement Direct Operations

**File:** `app/data_structures/table.cpp`

**Replace entire batched operations section (~lines 207-334) with:**

```cpp
// ============================================================================
// Direct Operations Implementation (TDX - no batching needed)
// ============================================================================

// Helper: Get single-entry operation function
typedef void (*single_op_fn)(entry_t*);
typedef void (*single_op_with_params_fn)(entry_t*, int32_t);
typedef void (*two_param_op_fn)(entry_t*, int32_t, equality_type_t);
typedef void (*dual_entry_op_fn)(entry_t*, entry_t*);

static single_op_fn get_single_op_function(OpEcall op_type) {
    switch(op_type) {
        case OP_ECALL_TRANSFORM_SET_LOCAL_MULT_ONE:
            return transform_set_local_mult_one_op;
        case OP_ECALL_TRANSFORM_ADD_METADATA:
            return transform_add_metadata_op;
        case OP_ECALL_TRANSFORM_INIT_LOCAL_TEMPS:
            return transform_init_local_temps_op;
        case OP_ECALL_TRANSFORM_INIT_FINAL_MULT:
            return transform_init_final_mult_op;
        case OP_ECALL_TRANSFORM_INIT_FOREIGN_TEMPS:
            return transform_init_foreign_temps_op;
        case OP_ECALL_TRANSFORM_TO_SOURCE:
            return transform_to_source_op;
        case OP_ECALL_TRANSFORM_SET_SORT_PADDING:
            return transform_set_sort_padding_op;
        case OP_ECALL_TRANSFORM_INIT_DST_IDX:
            return transform_init_dst_idx_op;
        case OP_ECALL_TRANSFORM_INIT_INDEX:
            return transform_init_index_op;
        case OP_ECALL_TRANSFORM_MARK_ZERO_MULT_PADDING:
            return transform_mark_zero_mult_padding_op;
        case OP_ECALL_TRANSFORM_CREATE_DIST_PADDING:
            return transform_create_dist_padding_op;
        case OP_ECALL_TRANSFORM_INIT_COPY_INDEX:
            return transform_init_copy_index_op;
        case OP_ECALL_TRANSFORM_COMPUTE_ALIGNMENT_KEY:
            return transform_compute_alignment_key_op;
        default:
            return nullptr;
    }
}

static dual_entry_op_fn get_dual_entry_op_function(OpEcall op_type) {
    switch(op_type) {
        // Comparators
        case OP_ECALL_COMPARATOR_JOIN_ATTR:
            return comparator_join_attr_op;
        case OP_ECALL_COMPARATOR_PAIRWISE:
            return comparator_pairwise_op;
        case OP_ECALL_COMPARATOR_END_FIRST:
            return comparator_end_first_op;
        case OP_ECALL_COMPARATOR_JOIN_THEN_OTHER:
            return comparator_join_then_other_op;
        case OP_ECALL_COMPARATOR_ORIGINAL_INDEX:
            return comparator_original_index_op;
        case OP_ECALL_COMPARATOR_ALIGNMENT_KEY:
            return comparator_alignment_key_op;
        case OP_ECALL_COMPARATOR_PADDING_LAST:
            return comparator_padding_last_op;
        case OP_ECALL_COMPARATOR_DISTRIBUTE:
            return comparator_distribute_op;
        // Window operations
        case OP_ECALL_WINDOW_SET_ORIGINAL_INDEX:
            return window_set_original_index_op;
        case OP_ECALL_WINDOW_COMPUTE_LOCAL_SUM:
            return window_compute_local_sum_op;
        case OP_ECALL_WINDOW_COMPUTE_LOCAL_INTERVAL:
            return window_compute_local_interval_op;
        case OP_ECALL_WINDOW_COMPUTE_FOREIGN_SUM:
            return window_compute_foreign_sum_op;
        case OP_ECALL_WINDOW_COMPUTE_FOREIGN_INTERVAL:
            return window_compute_foreign_interval_op;
        case OP_ECALL_WINDOW_PROPAGATE_FOREIGN_INTERVAL:
            return window_propagate_foreign_interval_op;
        case OP_ECALL_WINDOW_COMPUTE_DST_IDX:
            return window_compute_dst_idx_op;
        case OP_ECALL_WINDOW_INCREMENT_INDEX:
            return window_increment_index_op;
        case OP_ECALL_WINDOW_EXPAND_COPY:
            return window_expand_copy_op;
        case OP_ECALL_WINDOW_UPDATE_COPY_INDEX:
            return window_update_copy_index_op;
        // Update operations
        case OP_ECALL_UPDATE_TARGET_MULTIPLICITY:
            return update_target_multiplicity_op;
        case OP_ECALL_UPDATE_TARGET_FINAL_MULTIPLICITY:
            return update_target_final_multiplicity_op;
        default:
            return nullptr;
    }
}

Table Table::map(OpEcall op_type, int32_t* params) const {
    DEBUG_TRACE("Table::map: Starting with %zu entries, op_type=%d", entries.size(), op_type);

    Table result(table_name, schema_column_names);
    result.set_num_columns(num_columns);

    // Handle operations with special parameter signatures
    if (op_type == OP_ECALL_TRANSFORM_TO_START || op_type == OP_ECALL_TRANSFORM_TO_END) {
        // Two-parameter operations: deviation, equality_type
        for (const auto& entry : entries) {
            Entry new_entry = entry;
            entry_t e = new_entry.to_entry_t();

            int32_t deviation = params ? params[0] : 0;
            equality_type_t equality = params ? (equality_type_t)params[1] : EQUALITY_EQ;

            if (op_type == OP_ECALL_TRANSFORM_TO_START) {
                transform_to_start_op(&e, deviation, equality);
            } else {
                transform_to_end_op(&e, deviation, equality);
            }

            new_entry.from_entry_t(e);
            result.add_entry(new_entry);
        }
    } else if (op_type == OP_ECALL_TRANSFORM_SET_INDEX ||
               op_type == OP_ECALL_TRANSFORM_SET_JOIN_ATTR ||
               op_type == OP_ECALL_INIT_METADATA_NULL) {
        // Single-parameter operations
        for (const auto& entry : entries) {
            Entry new_entry = entry;
            entry_t e = new_entry.to_entry_t();

            int32_t param = params ? params[0] : 0;

            if (op_type == OP_ECALL_TRANSFORM_SET_INDEX) {
                transform_set_index_op(&e, (uint32_t)param);
            } else if (op_type == OP_ECALL_TRANSFORM_SET_JOIN_ATTR) {
                transform_set_join_attr_op(&e, param);
            } else {
                transform_init_metadata_null_op(&e, (uint32_t)param);
            }

            new_entry.from_entry_t(e);
            result.add_entry(new_entry);
        }
    } else {
        // No-parameter operations
        single_op_fn func = get_single_op_function(op_type);
        if (!func) {
            throw std::runtime_error("Unknown single-entry operation type");
        }

        for (const auto& entry : entries) {
            Entry new_entry = entry;
            entry_t e = new_entry.to_entry_t();
            func(&e);
            new_entry.from_entry_t(e);
            result.add_entry(new_entry);
        }
    }

    DEBUG_TRACE("Table::map: Complete with %zu entries", result.size());
    return result;
}

void Table::linear_pass(OpEcall op_type, int32_t* params) {
    if (entries.size() < 2) return;

    DEBUG_TRACE("Table::linear_pass: Starting with %zu entries, op_type=%d", entries.size(), op_type);

    dual_entry_op_fn func = get_dual_entry_op_function(op_type);
    if (!func) {
        throw std::runtime_error("Unknown dual-entry operation type for linear_pass");
    }

    // Window operation: process adjacent pairs
    for (size_t i = 0; i < entries.size() - 1; i++) {
        entry_t e1 = entries[i].to_entry_t();
        entry_t e2 = entries[i+1].to_entry_t();

        func(&e1, &e2);

        entries[i].from_entry_t(e1);
        entries[i+1].from_entry_t(e2);
    }

    DEBUG_TRACE("Table::linear_pass: Complete");
}

void Table::parallel_pass(Table& other, OpEcall op_type, int32_t* params) {
    if (entries.size() != other.entries.size()) {
        throw std::runtime_error("Tables must have same size for parallel_pass");
    }

    DEBUG_TRACE("Table::parallel_pass: Starting with %zu entries, op_type=%d", entries.size(), op_type);

    // Handle concat operation specially (has extra parameters)
    if (op_type == OP_ECALL_CONCAT_ATTRIBUTES) {
        int32_t left_attr_count = params ? params[0] : 0;
        int32_t right_attr_count = params ? params[1] : 0;

        for (size_t i = 0; i < entries.size(); i++) {
            entry_t e1 = entries[i].to_entry_t();
            entry_t e2 = other.entries[i].to_entry_t();

            concat_attributes_op(&e1, &e2, left_attr_count, right_attr_count);

            entries[i].from_entry_t(e1);
            other.entries[i].from_entry_t(e2);
        }
    } else {
        dual_entry_op_fn func = get_dual_entry_op_function(op_type);
        if (!func) {
            throw std::runtime_error("Unknown dual-entry operation type for parallel_pass");
        }

        for (size_t i = 0; i < entries.size(); i++) {
            entry_t e1 = entries[i].to_entry_t();
            entry_t e2 = other.entries[i].to_entry_t();

            func(&e1, &e2);

            entries[i].from_entry_t(e1);
            other.entries[i].from_entry_t(e2);
        }
    }

    DEBUG_TRACE("Table::parallel_pass: Complete");
}

void Table::distribute_pass(size_t distance, OpEcall op_type, int32_t* params) {
    DEBUG_TRACE("Table::distribute_pass: Starting with distance %zu, op_type=%d", distance, op_type);

    dual_entry_op_fn func = get_dual_entry_op_function(op_type);
    if (!func) {
        throw std::runtime_error("Unknown dual-entry operation type for distribute_pass");
    }

    // Process pairs at given distance (right to left to avoid underflow)
    for (size_t i = entries.size() - distance; i > 0; i--) {
        entry_t e1 = entries[i - 1].to_entry_t();
        entry_t e2 = entries[i - 1 + distance].to_entry_t();

        func(&e1, &e2);

        entries[i - 1].from_entry_t(e1);
        entries[i - 1 + distance].from_entry_t(e2);
    }

    // Handle i = 0 separately
    if (distance < entries.size()) {
        entry_t e1 = entries[0].to_entry_t();
        entry_t e2 = entries[distance].to_entry_t();

        func(&e1, &e2);

        entries[0].from_entry_t(e1);
        entries[distance].from_entry_t(e2);
    }

    DEBUG_TRACE("Table::distribute_pass: Complete");
}

void Table::add_padding(size_t count, OpEcall padding_op) {
    if (count == 0) return;

    DEBUG_TRACE("Table::add_padding: Adding %zu padding entries", count);

    entries.reserve(entries.size() + count);

    single_op_fn func = get_single_op_function(padding_op);
    if (!func) {
        throw std::runtime_error("Unknown padding operation type");
    }

    for (size_t i = 0; i < count; i++) {
        entry_t padding_entry;
        memset(&padding_entry, 0, sizeof(entry_t));
        func(&padding_entry);

        Entry padding(padding_entry);
        entries.push_back(padding);
    }

    DEBUG_TRACE("Table::add_padding: Complete - added %zu entries", count);
}
```

**Notes:**
- Remove `#include "../batch/ecall_batch_collector.h"` from table.cpp
- Add `#include "../core_logic/core.h"` for core function declarations
- Add `#include "../core_logic/operations/concat_op.h"` if concat function is separate

### 1.3 Test After Phase 1

```bash
make clean
make

# Should compile successfully
```

---

## Phase 2: Update Algorithm Callers

### Goal
Find and replace all `batched_*` method calls with new method names.

### 2.1 Find All Usage Sites

```bash
cd /home/b2bhatta/CLionProjects/Oblivious-Multi-Way-Band-Joins
grep -r "batched_map" app/algorithms/
grep -r "batched_linear_pass" app/algorithms/
grep -r "batched_parallel_pass" app/algorithms/
grep -r "batched_distribute_pass" app/algorithms/
grep -r "add_batched_padding" app/algorithms/
```

### 2.2 Update distribute_expand.cpp

**File:** `app/algorithms/distribute_expand.cpp`

**Find and replace:**
- `table.batched_map(` → `table.map(`
- `.batched_linear_pass(` → `.linear_pass(`
- `.add_batched_padding(` → `.add_padding(`

**Example locations:**
- Line 68: `Table working = table.batched_map(OP_ECALL_TRANSFORM_INIT_DST_IDX);`
- Line 73: `working.batched_linear_pass(OP_ECALL_WINDOW_COMPUTE_DST_IDX);`
- Line 94: `working = working.batched_map(OP_ECALL_TRANSFORM_MARK_ZERO_MULT_PADDING);`
- And more throughout the file...

### 2.3 Update align_concat.cpp

**File:** `app/algorithms/align_concat.cpp`

Same replacements as distribute_expand.cpp.

### 2.4 Update bottom_up_phase.cpp

**File:** `app/algorithms/bottom_up_phase.cpp`

Same replacements.

### 2.5 Update top_down_phase.cpp

**File:** `app/algorithms/top_down_phase.cpp`

Same replacements.

### 2.6 Update oblivious_join.cpp

**File:** `app/algorithms/oblivious_join.cpp`

Same replacements.

### 2.7 Test After Phase 2

```bash
make clean
make

# Should compile successfully
./test_join input/queries/tpch_tb1.sql input/plaintext/data_0_001

# Should produce correct output
```

---

## Phase 3: Remove Batch Infrastructure

### Goal
Delete obsolete batch code and update build system.

### 3.1 Delete Batch Directories

```bash
cd /home/b2bhatta/CLionProjects/Oblivious-Multi-Way-Band-Joins

# Backup first (optional)
git add -A
git commit -m "Before deleting batch infrastructure"

# Delete batch code
rm -rf app/batch/
rm -rf app/core_logic/batch/
```

### 3.2 Update Makefile

**File:** `Makefile`

**Remove from App_Cpp_Files (lines ~45-46):**
```makefile
app/batch/ecall_batch_collector.cpp \
app/batch/ecall_wrapper.cpp \
```

**Remove from App_C_Files (line ~61):**
```makefile
app/core_logic/batch/batch_dispatcher.c
```

**Remove from Test_Common_Objects (lines ~144-145):**
```makefile
app/batch/ecall_batch_collector.o \
app/batch/ecall_wrapper.o \
```

### 3.3 Update pad_to_shuffle_size

**File:** `app/data_structures/table.cpp`

**Find (around line 347):**
```cpp
add_batched_padding(padding_count, OP_ECALL_TRANSFORM_SET_SORT_PADDING);
```

**Replace with:**
```cpp
add_padding(padding_count, OP_ECALL_TRANSFORM_SET_SORT_PADDING);
```

### 3.4 Test After Phase 3

```bash
make clean
make
make tests

# Run all integration tests
./test_join input/queries/tpch_tb1.sql input/plaintext/data_0_001
./test_join input/queries/tpch_tb2.sql input/plaintext/data_0_001
./test_join input/queries/tpch_tm1.sql input/plaintext/data_0_001
./test_join input/queries/tpch_tm2.sql input/plaintext/data_0_001
./test_join input/queries/tpch_tm3.sql input/plaintext/data_0_001

# Or use the script
./scripts/run_tpch_tests.sh 0_001
```

---

## Phase 4: Full Validation

### 4.1 Test All Scales

```bash
# Scale 0.001 (~150 rows)
./scripts/run_tpch_tests.sh 0_001

# Scale 0.01 (~1500 rows)
./scripts/run_tpch_tests.sh 0_01
```

**Expected:** All tests pass with "PASSED" status

### 4.2 Compare Output with Baseline

```bash
# Run SGX version
./sgx_app input/queries/tpch_tb1.sql input/plaintext/data_0_001 /tmp/sgx_output.csv

# Run SQLite baseline
./sqlite_baseline input/queries/tpch_tb1.sql input/plaintext/data_0_001 /tmp/baseline_output.csv

# Compare (should be identical)
diff /tmp/sgx_output.csv /tmp/baseline_output.csv
```

### 4.3 Performance Check (Optional)

```bash
# Measure before removal (if you have old branch)
time ./sgx_app input/queries/tpch_tm3.sql input/plaintext/data_0_01 /tmp/out1.csv

# Measure after removal
time ./sgx_app input/queries/tpch_tm3.sql input/plaintext/data_0_01 /tmp/out2.csv

# Should be same or faster
```

---

## Phase 5: Cleanup & Documentation (Optional)

### 5.1 Update CLAUDE.md

**File:** `CLAUDE.md`

**Remove references to batching:**
- Line ~33-38: Remove batch processing section
- Update architecture description

**Update "Recent Development" section:**
```markdown
## Recent Development

### TDX Migration (October 2025) ✅
- Migrated from SGX enclaves to TDX trusted VMs
- Removed application-level encryption
- Removed batching layer (no longer needed without ecalls)
- Simplified architecture with direct function calls
```

### 5.2 Update TDX_MIGRATION_SUMMARY.md

Add section:
```markdown
### Batching Removal (Phase 9)
**Status:** ✅ Complete

**Changes:**
- Removed `app/batch/` directory
- Removed `app/core_logic/batch/` directory
- Replaced batched operations with direct function calls
- Renamed: batched_map → map, batched_linear_pass → linear_pass, etc.

**Performance Impact:** Same or better (eliminated conversion overhead)
```

### 5.3 Optional: Replace OpEcall with Function Pointers

This is a future optimization - could replace the OpEcall enum with direct function pointer types. Not required for now since OpEcall still works fine.

---

## Rollback Plan

If anything goes wrong:

```bash
# Restore from git
git reset --hard HEAD

# Or restore specific phase
git log --oneline  # Find commit before issue
git reset --hard <commit-hash>
```

---

## Success Criteria

- ✅ All tests compile without warnings
- ✅ All 5 TPC-H queries pass on scale 0.001
- ✅ All 5 TPC-H queries pass on scale 0.01
- ✅ Output matches SQLite baseline exactly
- ✅ No performance regression
- ✅ Code is simpler and easier to understand

---

## Key Functions Reference

### Core Functions to Call Directly (from app/core_logic/core.h)

**Single-entry transforms:**
- `transform_set_local_mult_one_op(entry_t*)`
- `transform_add_metadata_op(entry_t*)`
- `transform_init_local_temps_op(entry_t*)`
- `transform_init_final_mult_op(entry_t*)`
- `transform_init_foreign_temps_op(entry_t*)`
- `transform_to_source_op(entry_t*)`
- `transform_to_start_op(entry_t*, int32_t deviation, equality_type_t)`
- `transform_to_end_op(entry_t*, int32_t deviation, equality_type_t)`
- `transform_set_sort_padding_op(entry_t*)`
- `transform_init_dst_idx_op(entry_t*)`
- `transform_init_index_op(entry_t*)`
- `transform_mark_zero_mult_padding_op(entry_t*)`
- `transform_create_dist_padding_op(entry_t*)`
- `transform_init_copy_index_op(entry_t*)`
- `transform_compute_alignment_key_op(entry_t*)`
- `transform_set_index_op(entry_t*, uint32_t)`
- `transform_set_join_attr_op(entry_t*, int32_t)`
- `transform_init_metadata_null_op(entry_t*, uint32_t)`

**Dual-entry operations:**
- Comparators: `comparator_join_attr_op(entry_t*, entry_t*)`
- Window functions: `window_compute_local_sum_op(entry_t*, entry_t*)`
- Update functions: `update_target_multiplicity_op(entry_t*, entry_t*)`
- Concat: `concat_attributes_op(entry_t*, entry_t*, int32_t, int32_t)`

All declared in `app/core_logic/core.h`

---

## Notes

- Keep `common/batch_types.h` for now (OpEcall enum still useful)
- Can delete later if we move to pure function pointers
- All `_op` functions work directly on entry_t structs
- No encryption/decryption in TDX - data always plaintext in VM
