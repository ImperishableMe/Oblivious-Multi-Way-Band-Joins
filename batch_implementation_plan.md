# Detailed Step-by-Step Batch Processing Implementation Plan

## Overview
This plan implements batch processing to reduce SGX enclave transitions from ~240x per operation to 1x by batching up to 2000 operations. Each step is atomic with verification before proceeding.

## Key Design Decisions
- Use `int32_t` throughout (not `uint32_t`) for consistency
- Use `int32_t extra_params[4]` array for flexible parameter passing
- Batch ALL operations including START/END transformations
- One change at a time with immediate verification

---

## Phase 0: Setup and Infrastructure

### Step 0.1: Create batch_types.h
**Edit:** Create `common/batch_types.h`
```c
#ifndef BATCH_TYPES_H
#define BATCH_TYPES_H

#include <stdint.h>

#define BATCH_NO_PARAM -1
#define MAX_EXTRA_PARAMS 4
#define MAX_BATCH_SIZE 2000

typedef struct {
    int32_t idx1;                           // First entry index
    int32_t idx2;                           // Second entry index (or BATCH_NO_PARAM)
    int32_t extra_params[MAX_EXTRA_PARAMS]; // Additional parameters
} BatchOperation;

typedef enum {
    // Comparator operations
    OP_ECALL_COMPARATOR_JOIN_ATTR,
    OP_ECALL_COMPARATOR_PAIRWISE,
    OP_ECALL_COMPARATOR_END_FIRST,
    OP_ECALL_COMPARATOR_JOIN_THEN_OTHER,
    OP_ECALL_COMPARATOR_ORIGINAL_INDEX,
    OP_ECALL_COMPARATOR_ALIGNMENT_KEY,
    OP_ECALL_COMPARATOR_PADDING_LAST,
    OP_ECALL_COMPARATOR_DISTRIBUTE,
    
    // Transform operations
    OP_ECALL_TRANSFORM_SET_LOCAL_MULT_ONE,
    OP_ECALL_TRANSFORM_ADD_METADATA,
    OP_ECALL_TRANSFORM_SET_INDEX,
    OP_ECALL_TRANSFORM_INIT_LOCAL_TEMPS,
    OP_ECALL_TRANSFORM_INIT_FINAL_MULT,
    OP_ECALL_TRANSFORM_INIT_FOREIGN_TEMPS,
    OP_ECALL_TRANSFORM_TO_SOURCE,
    OP_ECALL_TRANSFORM_TO_START,
    OP_ECALL_TRANSFORM_TO_END,
    OP_ECALL_TRANSFORM_SET_SORT_PADDING,
    OP_ECALL_TRANSFORM_SET_JOIN_ATTR,
    OP_ECALL_TRANSFORM_INIT_DST_IDX,
    OP_ECALL_TRANSFORM_INIT_INDEX,
    OP_ECALL_TRANSFORM_MARK_ZERO_MULT_PADDING,
    OP_ECALL_TRANSFORM_CREATE_DIST_PADDING,
    OP_ECALL_TRANSFORM_INIT_COPY_INDEX,
    OP_ECALL_TRANSFORM_COMPUTE_ALIGNMENT_KEY,
    OP_ECALL_INIT_METADATA_NULL,
    
    // Window operations
    OP_ECALL_WINDOW_SET_ORIGINAL_INDEX,
    OP_ECALL_WINDOW_COMPUTE_LOCAL_SUM,
    OP_ECALL_WINDOW_COMPUTE_LOCAL_INTERVAL,
    OP_ECALL_WINDOW_COMPUTE_FOREIGN_SUM,
    OP_ECALL_WINDOW_COMPUTE_FOREIGN_INTERVAL,
    OP_ECALL_WINDOW_PROPAGATE_FOREIGN_INTERVAL,
    OP_ECALL_WINDOW_COMPUTE_DST_IDX,
    OP_ECALL_WINDOW_INCREMENT_INDEX,
    OP_ECALL_WINDOW_EXPAND_COPY,
    OP_ECALL_WINDOW_UPDATE_COPY_INDEX,
    
    // Update operations
    OP_ECALL_UPDATE_TARGET_MULTIPLICITY,
    OP_ECALL_UPDATE_TARGET_FINAL_MULTIPLICITY,
    
    // Concat operations
    OP_ECALL_CONCAT_ATTRIBUTES
} OpEcall;

#endif // BATCH_TYPES_H
```

**Verification:**
- `make clean && make` - must compile without errors
- Run: `./test/test_join ../../input/queries/tpch_tb1.sql ../../input/encrypted/data_0_001`
- Expected: 45 rows, matches SQLite
- **Save:** `git add common/batch_types.h && git commit -m "Add batch types header"`

### Step 0.2: Add Batch Collector
**Edit:** Create `app/batch/ecall_batch_collector.h`
```cpp
#ifndef ECALL_BATCH_COLLECTOR_H
#define ECALL_BATCH_COLLECTOR_H

#include <vector>
#include <unordered_map>
#include "../data_structures/entry.h"
#include "../../common/batch_types.h"
#include "sgx_urts.h"

class EcallBatchCollector {
private:
    std::unordered_map<entry_t*, int32_t> entry_indices;
    std::vector<entry_t> unique_entries;
    std::vector<BatchOperation> operations;
    OpEcall op_type;
    sgx_enclave_id_t eid;
    size_t batch_size;
    
    struct Stats {
        size_t total_operations;
        size_t total_flushes;
        size_t total_entries;
    } stats;
    
public:
    EcallBatchCollector(sgx_enclave_id_t enclave_id, OpEcall operation_type, 
                       size_t max_batch = MAX_BATCH_SIZE);
    ~EcallBatchCollector();
    
    void add_operation(Entry& e1, Entry& e2, int32_t* params = nullptr);
    void add_operation(Entry& e, int32_t* params = nullptr);
    void flush();
    Stats get_stats() const { return stats; }
    
private:
    int32_t get_or_add_entry(Entry& entry);
};

#endif
```

**Edit:** Create `app/batch/ecall_batch_collector.cpp` with implementation

**Verification:**
- `make clean && make` - must compile
- TB1 test must still pass (45 rows)
- **Save:** `git add app/batch/* && git commit -m "Add batch collector"`

### Step 0.3: Add Batch Dispatcher
**Edit:** Create `enclave/batch/batch_dispatcher.h`
```c
#ifndef BATCH_DISPATCHER_H
#define BATCH_DISPATCHER_H

#include "../enclave_types.h"
#include "../../common/batch_types.h"

void ecall_batch_dispatcher(entry_t* data_array, size_t data_count,
                           BatchOperation* ops_array, size_t ops_count,
                           OpEcall op_type);

#endif
```

**Edit:** Create `enclave/batch/batch_dispatcher.c` with batch-level decrypt/encrypt

**Verification:**
- `make clean && make`
- TB1 test must still pass
- **Save:** `git add enclave/batch/* && git commit -m "Add batch dispatcher"`

### Step 0.4: Update Build System
**Edit `Enclave.edl`:** Add:
```
void ecall_batch_dispatcher([in, count=data_count] entry_t* data_array, size_t data_count,
                           [in, count=ops_count] BatchOperation* ops_array, size_t ops_count,
                           OpEcall op_type);
```

**Edit `Makefile`:** Add new source files to compilation

**Verification:**
- `make clean && make`
- All tests pass unchanged
- **Save:** `git commit -am "Update build system for batching"`

---

## Phase 1: Add Batched Methods to Table Class (No Algorithm Changes)

### Step 1.1: Add oblivious_sort_batched Method
**Edit `app/data_structures/table.h`:** Add:
```cpp
void oblivious_sort_batched(sgx_enclave_id_t eid, OpEcall op_type);
```

**Edit `app/data_structures/table.cpp`:** Implement using batch collector

**Verification:** Create `unit_test/test_sort_batched.cpp`:
```cpp
void test_sort_equivalence(sgx_enclave_id_t eid) {
    Table t1 = create_test_table(100);
    Table t2 = t1;  // Copy
    
    // Sort using old method
    t1.oblivious_sort(eid, 
        [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
            return ecall_comparator_join_attr(eid, e1, e2);
        });
    
    // Sort using batched method
    t2.oblivious_sort_batched(eid, OP_ECALL_COMPARATOR_JOIN_ATTR);
    
    // Must be identical
    for (size_t i = 0; i < t1.size(); i++) {
        assert(t1[i] == t2[i]);
    }
}
```

**Test with:** 1, 10, 100, 2000, 2001 entries (both encrypted and unencrypted)
**Save:** `git commit -am "Add oblivious_sort_batched method"`

### Step 1.2: Add map_batched Method
**Edit:** Add `Table map_batched(sgx_enclave_id_t eid, OpEcall op_type) const;`

**Verification:** Unit test comparing results
**Save:** `git commit -am "Add map_batched method"`

### Step 1.3: Add linear_pass_batched and parallel_pass_batched
**Edit:** Add remaining batched methods

**Final Verification:**
- Run full test suite
- No algorithm code changed yet
- **Save:** `git commit -m "Complete batched method infrastructure"`

---

## Phase 2: Convert Bottom-Up Sorts One at a Time

### Step 2.1: Convert First Sort (line ~229)
**Edit `app/algorithms/bottom_up_phase.cpp`:**
```cpp
// OLD:
combined.oblivious_sort(eid,
    [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
        return ecall_comparator_join_attr(eid, e1, e2);
    });

// NEW:
combined.oblivious_sort_batched(eid, OP_ECALL_COMPARATOR_JOIN_ATTR);
```

**Verification:**
- Compile and run TB1
- Must get 45 rows
- Look for: "[BATCH] ObliviousSort completed: 240 operations batched into 1 ecalls"
- Compare debug dump with baseline
- **Save:** `git commit -am "Convert first sort to batched"`

### Step 2.2: Convert Second Sort (line ~250)
**Edit:** Change to `oblivious_sort_batched(eid, OP_ECALL_COMPARATOR_PAIRWISE)`

**Verification:**
- TB1: 45 rows
- two_center: 16 rows
- See second batching message
- **Save:** `git commit -am "Convert second sort to batched"`

### Step 2.3: Convert Third Sort (line ~270)
**Edit:** Change to `oblivious_sort_batched(eid, OP_ECALL_COMPARATOR_END_FIRST)`

**Verification:**
- All tests pass
- See 3 batching messages
- **Save:** `git commit -am "Convert third sort to batched"`

---

## Phase 3: Convert Transform Operations

### Step 3.1: Convert transform_set_local_mult_one
**Edit `bottom_up_phase.cpp` line ~71:**
```cpp
// OLD:
node->set_table(node->get_table().map(eid,
    [](sgx_enclave_id_t eid, entry_t* e) {
        return ecall_transform_set_local_mult_one(eid, e);
    }));

// NEW:
node->set_table(node->get_table().map_batched(eid, 
    OP_ECALL_TRANSFORM_SET_LOCAL_MULT_ONE));
```

**Verification:**
- Check debug dump: all entries must have local_mult = 1
- Tests must pass
- **Save:** `git commit -am "Convert transform_set_local_mult_one to batched"`

### Step 3.2: Convert transform_init_local_temps
**Edit:** Similar conversion

**Verification:**
- Check: local_cumsum = local_mult
- Tests pass
- **Save:** `git commit -am "Convert transform_init_local_temps to batched"`

---

## Phase 4: Convert Window Operations

### Step 4.1: Convert window_compute_local_sum
**Edit line ~240:**
```cpp
// OLD:
combined.linear_pass(eid,
    [](sgx_enclave_id_t eid, entry_t* e1, entry_t* e2) {
        return ecall_window_compute_local_sum(eid, e1, e2);
    });

// NEW:
combined.linear_pass_batched(eid, OP_ECALL_WINDOW_COMPUTE_LOCAL_SUM);
```

**Verification:**
- Check debug: local_cumsum values match baseline
- **Save:** `git commit -am "Convert window_compute_local_sum to batched"`

---

## Phase 5: Handle START/END Transformations

### Step 5.1: Extend Batch System for Parameters
**Edit `batch_dispatcher.c`:** Add cases:
```c
case OP_ECALL_TRANSFORM_TO_START:
    for (size_t i = 0; i < ops_count; i++) {
        int32_t deviation = ops_array[i].extra_params[0];
        equality_type_t equality = (equality_type_t)ops_array[i].extra_params[1];
        transform_to_start(&data_array[ops_array[i].idx1], deviation, equality);
    }
    break;

case OP_ECALL_TRANSFORM_TO_END:
    for (size_t i = 0; i < ops_count; i++) {
        int32_t deviation = ops_array[i].extra_params[0];
        equality_type_t equality = (equality_type_t)ops_array[i].extra_params[1];
        transform_to_end(&data_array[ops_array[i].idx1], deviation, equality);
    }
    break;
```

**Verification:**
- Test with inequality join
- Check START/END boundaries correct
- **Save:** `git commit -am "Add parameter support for START/END"`

### Step 5.2: Convert START/END in Bottom-Up
**Edit:** Add `map_batched_with_params` method or extend collector to handle params

**Convert lines ~140 and ~147:**
```cpp
// Convert START transformation
Table start_entries = child.map_batched_with_params(eid,
    OP_ECALL_TRANSFORM_TO_START, {dev1, (int32_t)eq1});

// Convert END transformation  
Table end_entries = child.map_batched_with_params(eid,
    OP_ECALL_TRANSFORM_TO_END, {dev2, (int32_t)eq2});
```

**Verification:**
- TB1: 45 rows
- two_center: 16 rows
- Check debug: deviations applied correctly
- **Save:** `git commit -am "Convert START/END to batched"`

---

## Phase 6: Complete Bottom-Up Conversion

### Checklist of Remaining Operations:
- [ ] window_compute_local_interval (line ~260)
- [ ] update_target_multiplicity (line ~300)
- [ ] All sorts in ComputeLocalMultiplicities

**Verification After Full Conversion:**
- [ ] TB1: 45 rows
- [ ] two_center: 16 rows
- [ ] All batch messages show 240x+ reduction
- [ ] Debug dumps match baseline exactly
- [ ] valgrind shows no memory leaks
- **Save:** `git commit -m "Complete bottom-up batching"`

---

## Phase 7: Convert Top-Down Phase (Carefully)

### Step 7.1: Convert InitializeRootTable
**Edit `top_down_phase.cpp` line ~69:**
```cpp
// Convert transform_init_final_mult to batched
node->set_table(node->get_table().map_batched(eid,
    OP_ECALL_TRANSFORM_INIT_FINAL_MULT));
```

**Critical Verification:**
- Check debug: final_mult = local_mult for root
- Values must be positive, not corrupted
- **Save:** `git commit -am "Convert root table initialization"`

### Step 7.2: Convert transform_to_source
**Edit line ~104:**

**Verification:**
- field_type = SOURCE for parent entries
- **Save:** `git commit -am "Convert transform_to_source"`

### Step 7.3-7.N: Continue One Operation at a Time
**Critical operations to monitor:**
- transform_init_foreign_temps
- window_compute_foreign_sum
- window_compute_foreign_interval
- update_target_final_multiplicity

**After Each:**
- [ ] final_mult values positive where expected
- [ ] foreign_sum computed correctly
- [ ] No corruption
- [ ] TB1: 45 rows

---

## Phase 8: Convert Distribute-Expand

### Step 8.1: Keep ecall_obtain_output_size Unbatched
**Note:** This is a special one-time operation - DO NOT BATCH

### Step 8.2: Convert Distribution Operations
- transform_init_dst_idx
- window_compute_dst_idx
- transform_mark_zero_mult_padding
- window_expand_copy

**Verification:**
- No hang during distribute phase
- Correct expansion count
- **Save:** `git commit -m "Complete distribute-expand batching"`

---

## Phase 9: Final Testing and Cleanup

### Step 9.1: Comprehensive Test Suite
Run all tests:
```bash
# TPC-H queries
./test/test_join ../../input/queries/tpch_tb1.sql ../../input/encrypted/data_0_001
./test/test_join ../../input/queries/tpch_tb2.sql ../../input/encrypted/data_0_001
./test/test_join ../../input/queries/tpch_tm1.sql ../../input/encrypted/data_0_001

# Test cases
./test/test_join ../../test_cases/queries/two_center_chain.sql ../../test_cases/encrypted
./test/test_join ../../test_cases/queries/three_table_chain.sql ../../test_cases/encrypted
./test/test_join ../../test_cases/queries/two_table_basic.sql ../../test_cases/encrypted

# Memory check
valgrind --leak-check=full ./sgx_app [query] [data] [output]
```

**Success Criteria:**
- [ ] All tests pass
- [ ] Performance improvement 100x+
- [ ] No memory leaks
- [ ] Results match SQLite exactly

### Step 9.2: Cleanup
- Remove debug output
- Clean up temporary code
- Add documentation

**Save:** `git commit -m "Final cleanup and optimization"`

---

## Rollback Strategy

If ANY test fails:
1. Check specific failure with debug output
2. If not immediately fixable:
   ```bash
   git reset --hard HEAD~1  # Rollback last change
   git log --oneline -5     # Check where we are
   ```
3. Investigate and try different approach

## Critical Test Cases for Verification

**IMPORTANT:** After EVERY change, these two test cases MUST pass:

### Test Case 1: TPC-H TB1 (Two-table inequality join)
```bash
./test/test_join ../../input/queries/tpch_tb1.sql ../../input/encrypted/data_0_001
```
- **Expected:** 45 rows (must match SQLite baseline exactly)
- **Query:** `SELECT * FROM supplier1, supplier2 WHERE supplier1.S1_S_ACCTBAL < supplier2.S2_S_ACCTBAL`
- **Tests:** Basic inequality join functionality

### Test Case 2: Two-Center Chain
```bash
./test/test_join ../../test_cases/queries/two_center_chain.sql ../../test_cases/encrypted
```
- **Expected:** 16 rows (must match SQLite baseline exactly)  
- **Query:** Complex multi-table join with multiple conditions
- **Tests:** Multi-way joins with complex predicates

**Verification Protocol:**
1. Run both tests after EVERY code change
2. If either test fails, immediately rollback and investigate
3. Do NOT proceed to next step until both tests pass
4. Save debug dumps if output differs from expected

## Key Success Metrics

After complete implementation:
- Performance: 100-240x reduction in ecall transitions
- Correctness: All test results match baseline exactly (especially TB1 and two_center)
- Memory: No leaks, bounded memory usage
- Code quality: Clean, maintainable, well-documented