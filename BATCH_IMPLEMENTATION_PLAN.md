# Batch Implementation Plan

## Executive Summary
Convert remaining 40 unbatched operations to use the batch dispatcher, achieving the target 240x reduction in SGX overhead.

**Current State**: 1/41 operations batched (2.4%)  
**Target State**: 41/41 operations batched (100%)  
**Estimated Performance Gain**: 240x reduction in ecall overhead

## Phase 1: Implement Batched Table Methods (Priority: HIGH)
**Timeline**: 2-3 days  
**Location**: `/impl/src/app/data_structures/table.cpp`

### 1.1 Implement `batched_map()`
- [ ] Create `batched_map()` method in Table class
- [ ] Use `EcallBatchCollector` with `OP_ECALL_TRANSFORM_*` operations
- [ ] Support parameter passing via `extra_params[]`
- [ ] Handle single-parameter transforms (e.g., `set_local_mult_one`)
- [ ] Handle multi-parameter transforms (e.g., `to_start`, `to_end`)

### 1.2 Implement `batched_linear_pass()`
- [ ] Create `batched_linear_pass()` method in Table class
- [ ] Use `EcallBatchCollector` with `OP_ECALL_WINDOW_*` operations
- [ ] Process entry pairs sequentially
- [ ] Maintain sliding window semantics

### 1.3 Implement `batched_parallel_pass()`
- [ ] Create `batched_parallel_pass()` method in Table class
- [ ] Use `EcallBatchCollector` with update operations
- [ ] Process corresponding entries from two tables
- [ ] Handle `update_target_multiplicity` and `update_target_final_multiplicity`

### 1.4 Extend existing `batched_oblivious_sort()`
- [x] Already implemented for `OP_ECALL_COMPARATOR_JOIN_ATTR`
- [ ] Verify support for all comparator types
- [ ] Test with different comparator operations

## Phase 2: Convert Bottom-Up Phase (Priority: HIGH)
**Timeline**: 1 day  
**Location**: `/impl/src/app/algorithms/bottom_up_phase.cpp`  
**Impact**: 11 operations, most complex phase

### Operations to Convert:
1. [ ] Line 70: `.map()` → `.batched_map()` with `OP_ECALL_INIT_METADATA_NULL` + `OP_ECALL_TRANSFORM_SET_LOCAL_MULT_ONE`
2. [ ] Line 93: `.linear_pass()` → `.batched_linear_pass()` with `OP_ECALL_WINDOW_SET_ORIGINAL_INDEX`
3. [ ] Line 132: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_TO_SOURCE`
4. [ ] Line 139: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_TO_START`
5. [ ] Line 146: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_TO_END`
6. [ ] Line 214: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_INIT_LOCAL_TEMPS`
7. [ ] Line 236: `.linear_pass()` → `.batched_linear_pass()` with `OP_ECALL_WINDOW_COMPUTE_LOCAL_SUM`
8. [ ] Line 246: `.oblivious_sort()` → `.batched_oblivious_sort()` with `OP_ECALL_COMPARATOR_PAIRWISE`
9. [ ] Line 256: `.linear_pass()` → `.batched_linear_pass()` with `OP_ECALL_WINDOW_COMPUTE_LOCAL_INTERVAL`
10. [ ] Line 266: `.oblivious_sort()` → `.batched_oblivious_sort()` with `OP_ECALL_COMPARATOR_END_FIRST`
11. [ ] Line 296: `.parallel_pass()` → `.batched_parallel_pass()` with `OP_ECALL_UPDATE_TARGET_MULTIPLICITY`

## Phase 3: Convert Top-Down Phase (Priority: HIGH)
**Timeline**: 1 day  
**Location**: `/impl/src/app/algorithms/top_down_phase.cpp`  
**Impact**: 12 operations, critical for correctness

### Operations to Convert:
1. [ ] Line 69: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_INIT_FINAL_MULT`
2. [ ] Line 77: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_INIT_FOREIGN_TEMPS`
3. [ ] Line 110: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_TO_SOURCE`
4. [ ] Line 117: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_TO_START`
5. [ ] Line 124: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_TO_END`
6. [ ] Line 178: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_INIT_FOREIGN_TEMPS`
7. [ ] Line 192: `.oblivious_sort()` → `.batched_oblivious_sort()` with `OP_ECALL_COMPARATOR_JOIN_ATTR`
8. [ ] Line 202: `.linear_pass()` → `.batched_linear_pass()` with `OP_ECALL_WINDOW_COMPUTE_FOREIGN_SUM`
9. [ ] Line 216: `.oblivious_sort()` → `.batched_oblivious_sort()` with `OP_ECALL_COMPARATOR_PAIRWISE`
10. [ ] Line 226: `.linear_pass()` → `.batched_linear_pass()` with `OP_ECALL_WINDOW_COMPUTE_FOREIGN_INTERVAL`
11. [ ] Line 241: `.oblivious_sort()` → `.batched_oblivious_sort()` with `OP_ECALL_COMPARATOR_END_FIRST`
12. [ ] Line 283: `.parallel_pass()` → `.batched_parallel_pass()` with `OP_ECALL_UPDATE_TARGET_FINAL_MULTIPLICITY`

## Phase 4: Convert Distribute-Expand Phase (Priority: MEDIUM)
**Timeline**: 0.5 days  
**Location**: `/impl/src/app/algorithms/distribute_expand.cpp`  
**Impact**: 7 operations

### Operations to Convert:
1. [ ] Line 77: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_INIT_DST_IDX`
2. [ ] Line 85: `.linear_pass()` → `.batched_linear_pass()` with `OP_ECALL_WINDOW_COMPUTE_DST_IDX`
3. [ ] Line 110: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_INIT_INDEX`
4. [ ] Line 124: `.oblivious_sort()` → `.batched_oblivious_sort()` with `OP_ECALL_COMPARATOR_DISTRIBUTE`
5. [ ] Line 157: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_MARK_ZERO_MULT_PADDING`
6. [ ] Line 162: `.linear_pass()` → `.batched_linear_pass()` with `OP_ECALL_WINDOW_EXPAND_COPY`
7. [ ] Line 261: `.linear_pass()` → `.batched_linear_pass()` with `OP_ECALL_WINDOW_INCREMENT_INDEX`

## Phase 5: Convert Align-Concat Phase (Priority: MEDIUM)
**Timeline**: 0.5 days  
**Location**: `/impl/src/app/algorithms/align_concat.cpp`  
**Impact**: 6 operations

### Operations to Convert:
1. [ ] Line 76: `.oblivious_sort()` → `.batched_oblivious_sort()` with `OP_ECALL_COMPARATOR_ALIGNMENT_KEY`
2. [ ] Line 91: `.oblivious_sort()` → `.batched_oblivious_sort()` with `OP_ECALL_COMPARATOR_ALIGNMENT_KEY`
3. [ ] Line 134: `.parallel_pass()` → `.batched_parallel_pass()` with `OP_ECALL_CONCAT_ATTRIBUTES`
4. [ ] Line 161: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_INIT_COPY_INDEX`
5. [ ] Line 169: `.linear_pass()` → `.batched_linear_pass()` with `OP_ECALL_WINDOW_UPDATE_COPY_INDEX`
6. [ ] Line 183: `.map()` → `.batched_map()` with `OP_ECALL_TRANSFORM_COMPUTE_ALIGNMENT_KEY`

## Phase 6: Testing and Validation (Priority: CRITICAL)
**Timeline**: 1-2 days

### 6.1 Unit Tests
- [ ] Test `batched_map()` with all transform operations
- [ ] Test `batched_linear_pass()` with all window operations
- [ ] Test `batched_parallel_pass()` with update operations
- [ ] Test `batched_oblivious_sort()` with all comparators

### 6.2 Integration Tests
- [ ] Test TB1 (two-table equality, 45 rows)
- [ ] Test TB2 (two-table inequality)
- [ ] Test two_center (complex tree, 16 rows)
- [ ] Test TM1 (three-table join)

### 6.3 Performance Tests
- [ ] Measure ecall count reduction
- [ ] Benchmark execution time improvement
- [ ] Verify 240x overhead reduction target

## Implementation Guidelines

### Code Pattern for batched_map()
```cpp
Table Table::batched_map(sgx_enclave_id_t eid, OpEcall op_type, 
                         std::function<void(BatchOperation&, size_t)> param_setter) {
    Table result;
    EcallBatchCollector collector(eid, DEFAULT_BATCH_SIZE);
    
    for (size_t i = 0; i < size(); i++) {
        BatchOperation op;
        op.idx1 = collector.add_entry((*this)[i]);
        op.idx2 = BATCH_NO_PARAM;
        
        // Let caller set extra params if needed
        if (param_setter) {
            param_setter(op, i);
        }
        
        collector.add_operation(op);
        
        if (collector.should_flush()) {
            collector.flush(op_type);
        }
    }
    
    // Final flush
    if (collector.has_pending_operations()) {
        collector.flush(op_type);
    }
    
    // Copy results
    for (size_t i = 0; i < size(); i++) {
        result.add_entry((*this)[i]);
    }
    
    return result;
}
```

### Code Pattern for Algorithm Conversion
```cpp
// OLD (unbatched)
combined = combined.map(eid,
    [](sgx_enclave_id_t eid, entry_t* e) {
        return ecall_transform_init_local_temps(eid, e);
    });

// NEW (batched)
combined = combined.batched_map(eid, OP_ECALL_TRANSFORM_INIT_LOCAL_TEMPS);

// OLD (with parameters)
Table start_entries = target.map(eid,
    [dev1, eq1](sgx_enclave_id_t eid, entry_t* e) {
        return ecall_transform_to_start(eid, e, dev1, eq1);
    });

// NEW (with parameters)
Table start_entries = target.batched_map(eid, OP_ECALL_TRANSFORM_TO_START,
    [dev1, eq1](BatchOperation& op, size_t) {
        op.extra_params[0] = dev1;
        op.extra_params[1] = eq1;
    });
```

## Success Metrics

1. **Functionality**: All tests pass (TB1, TB2, two_center, TM1)
2. **Performance**: 
   - Ecall count reduced by ~240x
   - Execution time improved by 10-50x (depending on data size)
3. **Code Quality**:
   - Clean, consistent API
   - No memory leaks
   - Proper error handling

## Risk Mitigation

1. **Incremental Testing**: Test each phase before moving to next
2. **Rollback Plan**: Keep original methods available with a flag
3. **Performance Monitoring**: Track ecall counts at each step
4. **Correctness Validation**: Compare results with SQLite baseline

## Total Estimated Timeline

- Phase 1 (Table methods): 2-3 days
- Phase 2-5 (Algorithm conversion): 3 days
- Phase 6 (Testing): 1-2 days
- **Total**: 6-8 days

## Next Steps

1. Start with Phase 1.1: Implement `batched_map()`
2. Create unit test for `batched_map()`
3. Convert one simple operation in bottom_up_phase.cpp
4. Verify correctness and performance improvement
5. Continue with remaining operations