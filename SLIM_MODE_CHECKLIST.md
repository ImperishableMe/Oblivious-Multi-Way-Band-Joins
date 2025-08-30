# Slim Mode Migration Checklist

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
- [ ] Locate `load_encrypted_csv()` in table_io.cpp
- [ ] Parse header and call `table.set_schema(columns)`
- [ ] Keep populating Entry::column_names (compatibility)
- [ ] Compile
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]

### Step 2.2: Update CSV Writer
- [ ] Locate `save_encrypted_csv()` in table_io.cpp
- [ ] Use `table.get_schema()` for header
- [ ] Add fallback to Entry[0].column_names if schema empty
- [ ] Compile
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]

### Step 2.3: Update Other Load Functions
- [ ] Check `load_table_from_csv()`
- [ ] Check `load_plaintext_csv()`
- [ ] Ensure all call `table.set_schema()`
- [ ] Compile
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]
- [ ] Commit: "Migrate I/O to use Table schema"

## Phase 3: Migrate Query Processing

### Step 3.1: Update Query Parser
- [ ] Find column validation in query_parser.cpp
- [ ] Use Table schema for validation
- [ ] Store column-to-index mappings
- [ ] Compile
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]

### Step 3.2: Update Join Attribute Setter
- [ ] Locate `SetJoinAttributesForTable()` in join_attribute_setter.cpp
- [ ] Use `table.get_column_index()` instead of Entry methods
- [ ] Update logic to use index-based access
- [ ] Compile
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]

### Step 3.3: Update Join Conditions
- [ ] Check join_condition.cpp for column name usage
- [ ] Check join_constraint.cpp for column name usage
- [ ] Update to use Table schema
- [ ] Compile
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]
- [ ] Commit: "Migrate query processing to Table schema"

## Phase 4: Find and Migrate Remaining Uses

### Step 4.1: Search and Replace Entry Column Methods
- [ ] Search: `grep -r "entry.*column_names" app/`
- [ ] Search: `grep -r "get_attribute.*string" app/`
- [ ] Search: `grep -r "set_attribute.*string" app/`
- [ ] Search: `grep -r "has_attribute" app/`
- [ ] Search: `grep -r "add_attribute.*string" app/`
- [ ] Document each finding in a list
- [ ] For each finding:
  - [ ] Identify containing Table
  - [ ] Replace with Table-based access
  - [ ] Compile
  - [ ] Test

### Step 4.2: Check Enclave Code
- [ ] Search: `grep -r "column_names" enclave/`
- [ ] Document findings (expect window_functions.c)
- [ ] Plan approach for each

### Step 4.3: Migration Summary
- [ ] All Entry column_names uses migrated: YES [ ]
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]
- [ ] Commit: "Migrate remaining Entry column_names uses"

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
- [ ] Remove column_names vector from entry.h
- [ ] Remove get_attribute(string) from entry.h
- [ ] Remove set_attribute(string) from entry.h
- [ ] Remove has_attribute(string) from entry.h
- [ ] Remove add_attribute(string) from entry.h
- [ ] Remove get_attributes_map() from entry.h
- [ ] Remove implementations from entry.cpp
- [ ] Compile - may have errors

### Step 6.2: Update Entry Conversion
- [ ] Update to_entry_t() - remove column_names copying
- [ ] Update from_entry_t() - remove column_names reading
- [ ] Remove table_metadata pointer handling (if present)
- [ ] Compile

### Step 6.3: Remove from entry_t
- [ ] Edit enclave_types.h
- [ ] Remove `column_names[MAX_ATTRIBUTES][MAX_COLUMN_NAME_LEN]`
- [ ] Remove table_metadata pointer (if present)
- [ ] Compile - expect enclave errors
- [ ] Document errors

## Phase 7: Fix Enclave-Side Issues

### Step 7.1: Fix AES Encryption
- [ ] Edit aes_crypto.c
- [ ] Remove column_names_offset calculation
- [ ] Update encryption regions array
- [ ] Update decrypt function similarly
- [ ] Compile

### Step 7.2: Fix concat_attributes
- [ ] Edit window_functions.c
- [ ] Find concat_attributes function
- [ ] Remove column name concatenation logic
- [ ] OR implement alternative solution
- [ ] Compile

### Step 7.3: Final Enclave Check
- [ ] Search for any remaining column_names references
- [ ] Fix any found
- [ ] Compile successfully
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]
- [ ] Commit: "Remove column_names from Entry and entry_t"

## Phase 8: Final Testing and Optimization

### Step 8.1: Comprehensive Testing
- [ ] Test two_center_chain: PASS [ ]
- [ ] Test TB1: PASS [ ]
- [ ] Test TB2: PASS [ ]
- [ ] Test three_table_chain: PASS [ ]
- [ ] Test TM1 (if available): PASS [ ]

### Step 8.2: Performance Verification
- [ ] Check entry size (should be ~208 bytes)
- [ ] Measure memory usage reduction
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