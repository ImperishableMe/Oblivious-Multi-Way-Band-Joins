# Ecall Batching Status Checklist

## Overview
This document tracks which ecall operations have been converted to use the batch dispatcher and which still use individual ecalls.

**Generated**: 2025-08-29  
**Current Status**: 1/41 operations batched (2.4%)

## ✅ BATCHED Operations (Using Batch Dispatcher)

Currently, only **1 operation** has been converted to use batching:

### Bottom-Up Phase (bottom_up_phase.cpp)
- [x] Line 228: `combined.batched_oblivious_sort(eid, OP_ECALL_COMPARATOR_JOIN_ATTR)`

## ❌ NOT BATCHED Operations (Still Using Individual Ecalls)

### Bottom-Up Phase (bottom_up_phase.cpp)
- [ ] Line 70: `.map()` - `ecall_init_metadata_null` + `ecall_transform_set_local_mult_one`
- [ ] Line 93: `.linear_pass()` - `ecall_window_set_original_index`
- [ ] Line 132: `.map()` - `ecall_transform_to_source`
- [ ] Line 139: `.map()` - `ecall_transform_to_start`
- [ ] Line 146: `.map()` - `ecall_transform_to_end`
- [ ] Line 214: `.map()` - `ecall_transform_init_local_temps`
- [ ] Line 236: `.linear_pass()` - `ecall_window_compute_local_sum`
- [ ] Line 246: `.oblivious_sort()` - `ecall_comparator_pairwise`
- [ ] Line 256: `.linear_pass()` - `ecall_window_compute_local_interval`
- [ ] Line 266: `.oblivious_sort()` - `ecall_comparator_end_first`
- [ ] Line 296: `.parallel_pass()` - `ecall_update_target_multiplicity`

### Top-Down Phase (top_down_phase.cpp)
- [ ] Line 69: `.map()` - `ecall_transform_init_final_mult`
- [ ] Line 77: `.map()` - `ecall_transform_init_foreign_temps`
- [ ] Line 110: `.map()` - `ecall_transform_to_source`
- [ ] Line 117: `.map()` - `ecall_transform_to_start`
- [ ] Line 124: `.map()` - `ecall_transform_to_end`
- [ ] Line 178: `.map()` - `ecall_transform_init_foreign_temps`
- [ ] Line 192: `.oblivious_sort()` - `ecall_comparator_join_attr`
- [ ] Line 202: `.linear_pass()` - `ecall_window_compute_foreign_sum`
- [ ] Line 216: `.oblivious_sort()` - `ecall_comparator_pairwise`
- [ ] Line 226: `.linear_pass()` - `ecall_window_compute_foreign_interval`
- [ ] Line 241: `.oblivious_sort()` - `ecall_comparator_end_first`
- [ ] Line 283: `.parallel_pass()` - `ecall_update_target_final_multiplicity`

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

| Operation Type | Count | Batched | Not Batched |
|---------------|-------|---------|-------------|
| `.map()` | 17 | 0 | 17 |
| `.oblivious_sort()` | 8 | 1 | 7 |
| `.linear_pass()` | 10 | 0 | 10 |
| `.parallel_pass()` | 4 | 0 | 4 |
| **TOTAL** | **41** | **1** | **40** |

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