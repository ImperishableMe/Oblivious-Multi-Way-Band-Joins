# Slim Mode Migration Checklist

## Migration Overview
**Goal**: Reduce entry size from ~2,256 bytes to ~208 bytes by removing column_names array

**Strategy**: 
1. ✅ Phase 1-3: Migrate to Table-based schema management
2. ⏳ Phase 4: Migrate remaining app-side uses (debug & test utilities)
3. Phase 5: Verify all migrations complete
4. Phase 6: Remove column_names from Entry class
5. Phase 7: Remove column_names from enclave entry_t
6. Phase 8: Final testing and optimization

**Current Status**: Ready for Phase 4 - Migrating debug and test utilities

## Pre-Migration Setup
- [ ] Create backup branch: `git branch backup-before-slim`
- [ ] Ensure tests pass: two_center_chain and TB1
- [ ] Review SLIM_MODE_MIGRATION_PLAN.md

## Phase 1: Add Schema Management to Table Class
- [x] Add `schema_column_names` vector to Table class (table.h)
- [x] Add `set_schema()` method declaration (table.h)
- [x] Add `get_schema()` method declaration (table.h)
- [x] Add `get_column_index()` method declaration (table.h)
- [x] Add `get_attribute(row, col_name)` method declaration (table.h)
- [x] Add `set_attribute(row, col_name, value)` method declaration (table.h)
- [x] Add `has_column()` method declaration (table.h)
- [x] Implement all methods in table.cpp
- [x] Compile: `make clean && make -j4`
- [x] Test two_center_chain: PASS [x]
- [x] Test TB1: PASS [x]
- [x] Commit: "Add schema management to Table class"

## Phase 2: Migrate I/O to Use Table Schema

### Step 2.1: Update CSV Reader
- [x] Locate `load_csv()` in table_io.cpp (load_encrypted_csv deprecated)
- [x] Parse header and call `table.set_schema(columns)`
- [x] Keep populating Entry::column_names (compatibility)
- [x] Compile
- [x] Test two_center_chain: PASS [x]
- [x] Test TB1: PASS [x]

### Step 2.2: Update CSV Writer
- [x] Locate `save_csv()` and `save_encrypted_csv()` in table_io.cpp
- [x] Use `table.get_schema()` for header
- [x] Add fallback to Entry[0].column_names if schema empty
- [x] Compile
- [x] Test two_center_chain: PASS [x]
- [x] Test TB1: PASS [x]

### Step 2.3: Update Other Load Functions
- [x] Check `load_tables_from_directory()` - uses load_csv internally
- [x] Check `load_csv_directory()` - uses load_csv internally
- [x] All functions route through load_csv, already updated
- [x] Compile
- [x] Test two_center_chain: PASS [x]
- [x] Test TB1: PASS [x]
- [x] Commit: "Phase 2: Migrate I/O to use Table schema"

## Phase 3: Migrate Query Processing

### Step 3.1: Update Query Parser
- [x] Find column validation in query_parser.cpp - none found
- [x] Query parser doesn't validate columns (defers to join attribute setter)
- [x] No changes needed
- [x] Compile
- [x] Test two_center_chain: PASS [x]
- [x] Test TB1: PASS [x]

### Step 3.2: Update Join Attribute Setter
- [x] Locate `SetJoinAttributesForTable()` in join_attribute_setter.cpp
- [x] Use `table.get_column_index()` instead of Entry methods
- [x] Add fallback to Entry column_names for compatibility
- [x] Compile
- [x] Test two_center_chain: PASS [x]
- [x] Test TB1: PASS [x]

### Step 3.3: Update Join Conditions
- [x] Check join_condition.cpp for column name usage - none found
- [x] Check join_constraint.cpp for column name usage - none found
- [x] No changes needed
- [x] Compile
- [x] Test two_center_chain: PASS [x]
- [x] Test TB1: PASS [x]
- [x] Commit: "Phase 3: Migrate query processing to Table schema"

## Phase 4: Find and Migrate Remaining Uses

### Step 4.1: Migrate debug_util.cpp
- [ ] Line 262: Change `first_entry.column_names` to `table.get_schema()` with fallback
- [ ] Lines 448-449: Replace column search with `table.get_column_index()`
- [ ] May need to pass Table reference to functions
- [ ] Compile
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]
- [ ] Commit: "Migrate debug_util to use Table schema"

### Step 4.2: Migrate Test Utilities - SQLite Ground Truth
- [ ] File: test/utils/sqlite_ground_truth.cpp
- [ ] Line 61: Replace `first.get_attributes_map()`
- [ ] Line 105: Replace `first.get_attributes_map()`
- [ ] Line 125: Replace `entry.get_attributes_map()`
- [ ] Pass Table reference alongside entries if needed
- [ ] Compile
- [ ] Test with test_join
- [ ] Commit: "Migrate sqlite_ground_truth to use Table schema"

### Step 4.3: Migrate Test Utilities - Join Result Comparator
- [ ] File: test/utils/join_result_comparator.cpp
- [ ] Line 20: Replace `entry.get_attributes_map()`
- [ ] Line 44: Replace `e1.get_attributes_map()`
- [ ] Line 49: Replace `e2.get_attributes_map()`
- [ ] Line 79: Replace `table[i].get_attributes_map()`
- [ ] Modify comparison logic to use indices
- [ ] Compile
- [ ] Test with test_join
- [ ] Commit: "Migrate join_result_comparator to use Table schema"

### Step 4.4: Migrate Test Utilities - Simple Join Executor
- [ ] File: test/utils/simple_join_executor.cpp
- [ ] Line 139: Replace `left.get_attributes_map()`
- [ ] Line 147: Replace `right.get_attributes_map()`
- [ ] Line 165: Replace `entry.get_attributes_map()`
- [ ] Use index-based attribute access
- [ ] Compile
- [ ] Test with test_join
- [ ] Commit: "Migrate simple_join_executor to use Table schema"

### Step 4.5: Migrate Test Utilities - Subtree Verifier
- [ ] File: test/utils/subtree_verifier.cpp
- [ ] Line 35: Replace `original.get_attributes_map()`
- [ ] Compile
- [ ] Test with test_join
- [ ] Commit: "Migrate subtree_verifier to use Table schema"

### Step 4.6: Summary
- [ ] All application-side Entry column_names uses migrated
- [ ] All test utilities updated
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]
- [ ] Test TB2: PASS [ ]
- [ ] Commit: "Phase 4: Complete migration of remaining Entry column_names uses"

## Phase 5: Verification Before Removal

### Step 5.1: Comment Out Column Names
- [ ] Edit entry.h: Comment out `column_names` vector
- [ ] Compile - expect errors
- [ ] Document each error location

### Step 5.2: Fix Compilation Errors
- [ ] For each error:
  - [ ] Fix using Table schema
  - [ ] Compile
  - [ ] Add to fixed list

### Step 5.3: Verification Complete
- [ ] All errors fixed
- [ ] Uncomment column_names (temporary)
- [ ] Compile successfully
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]
- [ ] Commit: "Verification: all column_names uses migrated"

## Phase 6: Remove Column Names from Entry

### Step 6.1: Remove from Entry Class
- [ ] Remove from entry.h (line 53): `std::vector<std::string> column_names`
- [ ] Remove from entry.h (line 68): `int32_t get_attribute(const std::string& column_name) const`
- [ ] Remove from entry.h (line 69): `bool has_attribute(const std::string& column_name) const`
- [ ] Remove from entry.h (line 70): `void set_attribute(const std::string& column_name, int32_t value)`
- [ ] Remove from entry.h (line 71): `void add_attribute(const std::string& column_name, int32_t value)`
- [ ] Remove from entry.h (line 72): `std::map<std::string, int32_t> get_attributes_map() const`
- [ ] Remove implementations from entry.cpp (lines 116-166)
- [ ] Compile - expect errors

### Step 6.2: Update Entry Conversion Functions
- [ ] Update to_entry_t() in entry.cpp:
  - [ ] Remove lines 63-67 (column_names copying)
  - [ ] Adjust attribute count logic
- [ ] Update from_entry_t() in entry.cpp:
  - [ ] Remove lines 94-103 (column_names reading)
  - [ ] Use MAX_ATTRIBUTES or pass count separately
- [ ] Compile
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]

### Step 6.3: Fix I/O After Removal
- [ ] Update table_io.cpp line 68: Remove `entry.column_names.push_back(headers[i])`
- [ ] Update table_io.cpp line 143: Remove fallback to `first_entry.column_names`
- [ ] Compile
- [ ] Test
- [ ] Commit: "Phase 6: Remove column_names from Entry class"

## Phase 7: Fix Enclave-Side Issues

### Step 7.1: Remove from entry_t Struct
- [ ] Edit enclave_types.h line 86: Remove `char column_names[MAX_ATTRIBUTES][MAX_COLUMN_NAME_LEN]`
- [ ] Compile - expect enclave errors
- [ ] Document all errors

### Step 7.2: Fix AES Encryption
- [ ] Edit aes_crypto.c:
  - [ ] Line 65: Update comment about excluded fields
  - [ ] Line 68: Remove `column_names_offset` calculation
  - [ ] Line 82: Update regions[2].end to `sizeof(entry_t)`
  - [ ] Line 143: Remove column_names_offset in decrypt
  - [ ] Line 155: Update regions in decrypt function
- [ ] Edit aes_crypto.h line 26: Update comment
- [ ] Compile
- [ ] Test encryption/decryption

### Step 7.3: Fix concat_attributes in window_functions.c
- [ ] Lines 304-307: Remove column_names check for left_attr_count
- [ ] Lines 313-316: Remove column_names check for right_attr_count
- [ ] Lines 330-332: Remove column name copying
- [ ] Options:
  - [ ] Option A: Use a sentinel value in attributes to mark end
  - [ ] Option B: Pass attribute count in metadata field
  - [ ] Option C: Always use MAX_ATTRIBUTES
- [ ] Compile
- [ ] Test align-concat phase specifically

### Step 7.4: Final Enclave Verification
- [ ] Search for any remaining column_names references
- [ ] Compile successfully
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]
- [ ] Test TB2: PASS [ ]
- [ ] Commit: "Phase 7: Remove column_names from enclave"

## Phase 8: Final Testing and Optimization

### Step 8.1: Comprehensive Testing
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]
- [ ] Test TB2: PASS [ ]
- [ ] Test three_table_chain: PASS [ ]
- [ ] Test TM1 (if available): PASS [ ]

### Step 8.2: Performance Verification
- [ ] Check entry size reduction:
  - [ ] Before: ~2,256 bytes (with column_names[20][100])
  - [ ] After: ~208 bytes (without column_names)
  - [ ] Reduction: ~91% memory savings per entry
- [ ] Measure ecall reduction in align-concat phase
- [ ] Document performance improvements

### Step 8.3: Cleanup
- [ ] Update constants.h if needed
- [ ] Update documentation/comments
- [ ] Remove any debug code
- [ ] Final compile and test
- [ ] Commit: "Optimize and cleanup slim mode"

## Final Verification
- [ ] All tests pass
- [ ] Entry size reduced to ~208 bytes
- [ ] No column_names in entry_t or Entry
- [ ] Table manages all schema
- [ ] Create final commit with summary

## Rollback Points
- After Phase 1: Can revert schema additions
- After Phase 2: Can revert I/O changes
- After Phase 3: Can revert query processing
- After Phase 4: Can revert individual migrations
- After Phase 5: Have verification of readiness
- After Phase 6: Major change - backup branch critical
- After Phase 7: Can still revert if needed

## Emergency Rollback
```bash
# If something goes badly wrong:
git status  # See what changed
git diff    # Review changes
git checkout -- .  # Revert all changes
# OR
git checkout backup-before-slim  # Return to backup branch
```

## Notes
- Test after EVERY checkbox
- Commit after each phase
- Keep backup branch until fully complete
- Document any deviations from plan