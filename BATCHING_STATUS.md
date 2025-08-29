# Ecall Batching Status Checklist

## Overview
This document tracks which ecall operations have been converted to use the batch dispatcher and which still use individual ecalls.

**Last Updated**: 2025-08-29  
**Current Status**: 25/41 operations batched (61.0%)

## ✅ BATCHED Operations (Using Batch Dispatcher)

Currently, **25 operations** have been converted to use batching:

### Bottom-Up Phase (bottom_up_phase.cpp) - FULLY CONVERTED ✅
- [x] `batched_map(eid, OP_ECALL_INIT_METADATA_NULL)` - Initialize metadata to NULL
- [x] `batched_map(eid, OP_ECALL_TRANSFORM_SET_LOCAL_MULT_ONE)` - Set local_mult to 1
- [x] `batched_linear_pass(eid, OP_ECALL_WINDOW_SET_ORIGINAL_INDEX)` - Set original indices
- [x] `batched_map(eid, OP_ECALL_TRANSFORM_TO_SOURCE)` - Transform to SOURCE
- [x] `batched_map(eid, OP_ECALL_TRANSFORM_TO_START, params)` - Transform to START
- [x] `batched_map(eid, OP_ECALL_TRANSFORM_TO_END, params)` - Transform to END
- [x] `batched_map(eid, OP_ECALL_TRANSFORM_INIT_LOCAL_TEMPS)` - Initialize temporary fields
- [x] `batched_oblivious_sort(eid, OP_ECALL_COMPARATOR_JOIN_ATTR)` - Sort by join attribute
- [x] `batched_linear_pass(eid, OP_ECALL_WINDOW_COMPUTE_LOCAL_SUM)` - Compute cumulative sums
- [x] `batched_oblivious_sort(eid, OP_ECALL_COMPARATOR_PAIRWISE)` - Pairwise sort
- [x] `batched_linear_pass(eid, OP_ECALL_WINDOW_COMPUTE_LOCAL_INTERVAL)` - Compute intervals
- [x] `batched_oblivious_sort(eid, OP_ECALL_COMPARATOR_END_FIRST)` - Sort END entries first
- [x] `batched_parallel_pass(parent, eid, OP_ECALL_UPDATE_TARGET_MULTIPLICITY)` - Update multiplicities

### Top-Down Phase (top_down_phase.cpp) - FULLY CONVERTED ✅
- [x] `batched_map(eid, OP_ECALL_TRANSFORM_INIT_FINAL_MULT)` - Initialize final_mult
- [x] `batched_map(eid, OP_ECALL_TRANSFORM_INIT_FOREIGN_TEMPS)` - Initialize foreign temps (2 locations)
- [x] `batched_map(eid, OP_ECALL_TRANSFORM_TO_SOURCE)` - Transform to SOURCE
- [x] `batched_map(eid, OP_ECALL_TRANSFORM_TO_START, params)` - Transform to START
- [x] `batched_map(eid, OP_ECALL_TRANSFORM_TO_END, params)` - Transform to END
- [x] `batched_oblivious_sort(eid, OP_ECALL_COMPARATOR_JOIN_ATTR)` - Sort by join attribute
- [x] `batched_linear_pass(eid, OP_ECALL_WINDOW_COMPUTE_FOREIGN_SUM)` - Compute foreign sums
- [x] `batched_oblivious_sort(eid, OP_ECALL_COMPARATOR_PAIRWISE)` - Pairwise sort
- [x] `batched_linear_pass(eid, OP_ECALL_WINDOW_COMPUTE_FOREIGN_INTERVAL)` - Compute foreign intervals
- [x] `batched_oblivious_sort(eid, OP_ECALL_COMPARATOR_END_FIRST)` - Sort END entries first
- [x] `batched_parallel_pass(child, eid, OP_ECALL_UPDATE_TARGET_FINAL_MULTIPLICITY)` - Update final multiplicities

## ❌ NOT BATCHED Operations (Still Using Individual Ecalls)

### Distribute-Expand Phase (distribute_expand.cpp)
- [ ] Line 77: `.map()` - `ecall_transform_init_dst_idx`
- [ ] Line 85: `.linear_pass()` - `ecall_window_compute_dst_idx`
- [ ] Line 110: `.map()` - `ecall_transform_init_index`
- [ ] Line 124: `.oblivious_sort()` - `ecall_comparator_distribute`
- [ ] Line 157: `.map()` - `ecall_transform_mark_zero_mult_padding`
- [ ] Line 162: `.linear_pass()` - `ecall_window_expand_copy`
- [ ] Line 261: `.linear_pass()` - `ecall_window_increment_index`

### Align-Concat Phase (align_concat.cpp)
- [ ] Line 76: `.oblivious_sort()` - `ecall_comparator_alignment_key`
- [ ] Line 91: `.oblivious_sort()` - `ecall_comparator_alignment_key`
- [ ] Line 134: `.parallel_pass()` - `ecall_concat_attributes`
- [ ] Line 161: `.map()` - `ecall_transform_init_copy_index`
- [ ] Line 169: `.linear_pass()` - `ecall_window_update_copy_index`
- [ ] Line 183: `.map()` - `ecall_transform_compute_alignment_key`

## Summary Statistics

| Phase | Total Operations | Batched | Remaining | % Complete |
|-------|-----------------|---------|-----------|------------|
| Bottom-Up | 13 | 13 | 0 | 100% ✅ |
| Top-Down | 12 | 12 | 0 | 100% ✅ |
| Distribute-Expand | 10 | 0 | 10 | 0% |
| Align-Concat | 6 | 0 | 6 | 0% |
| **TOTAL** | **41** | **25** | **16** | **61.0%** |

## Priority for Batching

Operations ranked by frequency and potential performance impact:

1. **`.map()` operations** - 17 instances
   - Highest frequency, used for transformations
   - Each currently makes 1 ecall per entry
   
2. **`.linear_pass()` operations** - 10 instances
   - Used for window functions and sequential processing
   - Each currently makes 1 ecall per entry pair
   
3. **`.oblivious_sort()` operations** - 7 instances (1 already batched)
   - Critical for algorithm correctness
   - Each makes O(n log n) ecalls for comparisons
   
4. **`.parallel_pass()` operations** - 4 instances
   - Used for final updates between tables
   - Each currently makes 1 ecall per entry pair

## Implementation Status

### Batch Dispatcher Support
The batch dispatcher (`/impl/src/enclave/batch/batch_dispatcher.c`) has been updated to support all 32 operation types with direct `_op` function calls:

- ✅ All comparator operations (8 types)
- ✅ All window operations (13 types)
- ✅ All transform operations (11 types)

### Required Changes for Full Batching

To achieve the target 240x reduction in SGX overhead:

1. **Implement batched versions in Table class**:
   - `batched_map()` 
   - `batched_linear_pass()`
   - `batched_parallel_pass()`
   - Already have: `batched_oblivious_sort()`

2. **Update all algorithm files** to use batched versions

3. **Test each conversion** to ensure correctness

## Notes

- The batch dispatcher processes up to 2000 operations per batch
- All `_op` functions have been exposed and work directly on decrypted data
- Global variables have been removed from transform functions in favor of direct parameters
- The infrastructure is ready; only the application-level changes remain