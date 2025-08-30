# Slim Mode Migration Plan: Removing Column Names from Entries

## Objective
Migrate from fat entries (2,256 bytes with embedded column names) to slim entries (~208 bytes without column names) by moving schema management to the Table class.

## Core Principle
- **Security**: Entries should not reveal table membership information
- **Efficiency**: 10x reduction in entry size
- **Gradual Migration**: Each step maintains working tests

## Phase 1: Add Schema Management to Table Class (Safe Addition)

### Step 1.1: Add schema fields to Table class
**File**: `app/data_structures/table.h`
```cpp
private:
    std::vector<std::string> schema_column_names;  // NEW: Table's schema
    // Note: num_columns already exists
```

### Step 1.2: Add schema management methods
**File**: `app/data_structures/table.h`
```cpp
public:
    // Schema management
    void set_schema(const std::vector<std::string>& columns);
    std::vector<std::string> get_schema() const;
    size_t get_column_index(const std::string& col_name) const;
    
    // Named attribute access using schema
    int32_t get_attribute(size_t row, const std::string& col_name) const;
    void set_attribute(size_t row, const std::string& col_name, int32_t value);
    bool has_column(const std::string& col_name) const;
```

### Step 1.3: Implement schema methods
**File**: `app/data_structures/table.cpp`
- Implement all new methods
- Use schema_column_names for lookups
- Map column names to indices

**Test Point**: Compile and run two_center_chain and TB1 - should pass (pure addition)

## Phase 2: Migrate I/O to Use Table Schema

### Step 2.1: Update CSV Reader
**File**: `app/io/table_io.cpp` - `load_encrypted_csv()` function
- Parse header once and store in Table::schema_column_names
- Continue storing in Entry::column_names for backward compatibility
- Both Table and Entry have the schema (redundant but safe)

**Test Point**: Both tests should pass

### Step 2.2: Update CSV Writer
**File**: `app/io/table_io.cpp` - `save_encrypted_csv()` function
- Use Table::get_schema() for header instead of Entry[0].column_names
- If Table schema is empty, fall back to Entry column names (compatibility)

**Test Point**: Both tests should pass

### Step 2.3: Update Table Loading
**File**: `app/io/table_io.cpp` - All load functions
- Ensure Table::set_schema() is called when loading tables
- Keep Entry column_names population for now

**Test Point**: Both tests should pass

## Phase 3: Migrate Query Processing

### Step 3.1: Update Query Parser
**File**: `app/query/query_parser.cpp`
- When parsing SELECT columns, use Table schema for validation
- Store column-to-index mappings

### Step 3.2: Update Join Attribute Setter
**File**: `app/data_structures/join_attribute_setter.cpp`
- Method: `SetJoinAttributesForTable()`
- Use Table::get_column_index() instead of Entry methods
- Process: 
  1. Get column index from Table schema
  2. Set join_attr = attributes[index] for each entry

**Test Point**: Both tests should pass

### Step 3.3: Update Join Condition Processing
**Files**: 
- `app/data_structures/join_condition.cpp`
- `app/data_structures/join_constraint.cpp`
- Use Table schema for column name resolution

**Test Point**: Both tests should pass

## Phase 4: Find and Migrate Remaining Uses

### Step 4.1: Audit Entry column_names usage
**Search for**:
- `entry.column_names`
- `entry.get_attribute(string)`
- `entry.set_attribute(string)`
- `entry.has_attribute(string)`
- `entry.add_attribute(string)`

**For each occurrence**:
1. Identify the containing Table
2. Replace with Table-based access
3. Test immediately

### Step 4.2: Audit entry_t column_names usage
**Search in enclave code**:
- `entry->column_names`
- `entry.column_names`

**Expected findings**:
- `enclave/core/window_functions.c` - concat_attributes function
- Will need special handling for concatenation

### Step 4.3: Handle Special Cases
**Concatenation** (`window_functions.c`):
- Problem: Concatenating tables with different schemas
- Solution: Pass schema information separately or handle at application level
- May need to temporarily keep column info for this operation

**Test Point**: After each file migration

## Phase 5: Verification Before Removal

### Step 5.1: Comment out column_names in Entry class
**File**: `app/data_structures/entry.h`
```cpp
// std::vector<std::string> column_names;  // COMMENTED FOR VERIFICATION
```

### Step 5.2: Compile and fix errors
- Each compilation error shows a remaining dependency
- Fix by using Table schema instead
- Document each fix

### Step 5.3: Uncomment and prepare for removal
- After all errors fixed, uncomment
- Verify tests still pass

## Phase 6: Remove Column Names from Entry

### Step 6.1: Remove from Entry class
**File**: `app/data_structures/entry.h`
- Remove `std::vector<std::string> column_names;`
- Remove all column name-related methods:
  - `get_attribute(string)`
  - `set_attribute(string)`
  - `has_attribute(string)`
  - `add_attribute(string)`
  - `get_attributes_map()`

### Step 6.2: Update Entry conversion methods
**File**: `app/data_structures/entry.cpp`
- Update `to_entry_t()`: Don't copy column_names
- Update `from_entry_t()`: Don't read column_names
- Remove column name processing logic

### Step 6.3: Remove from entry_t struct
**File**: `enclave/enclave_types.h`
- Remove `char column_names[MAX_ATTRIBUTES][MAX_COLUMN_NAME_LEN];`
- Remove the table_metadata pointer we added (if still there)

**Test Point**: Compile and test - may have issues with enclave code

## Phase 7: Fix Enclave-Side Issues

### Step 7.1: Fix AES encryption
**File**: `enclave/crypto/aes_crypto.c`
- Remove column_names from exclusion list
- Simplify encryption regions:
  - Before: Exclude is_encrypted, nonce, column_names
  - After: Exclude only is_encrypted, nonce
- Update both encrypt and decrypt functions

### Step 7.2: Fix concat_attributes
**File**: `enclave/core/window_functions.c`
- Problem: Function concatenates column names from two tables
- Solutions:
  1. Remove column name concatenation (tables track their own schema)
  2. Handle schema concatenation at application level
  3. Pass schema info separately if needed

### Step 7.3: Update any other enclave functions
- Search for any remaining column_names references
- Update or remove as appropriate

**Test Point**: Full test suite

## Phase 8: Optimization and Cleanup

### Step 8.1: Update constants
**File**: `common/constants.h`
- Can potentially reduce MAX_ATTRIBUTES if needed
- Update ENTRY_SIZE calculations

### Step 8.2: Performance testing
- Measure memory usage reduction
- Measure performance improvement
- Document results

### Step 8.3: Update documentation
- Update comments in affected files
- Update any design documents

## Rollback Strategy

At any point if tests fail:
1. `git status` to see what changed
2. `git diff` to review changes
3. `git checkout -- <file>` to revert specific files
4. Or `git stash` to save work and return to last working state

## Success Criteria

After completion:
1. All tests pass (two_center_chain, TB1, TB2, TM1)
2. Entry size reduced from ~2,256 to ~208 bytes
3. No column names in entry_t or Entry class
4. Table class manages all schema information
5. CSV I/O works correctly
6. Query processing works correctly

## Commit Strategy

Each phase should be a separate commit:
- Phase 1: "Add schema management to Table class"
- Phase 2: "Migrate I/O to use Table schema"
- Phase 3: "Migrate query processing to Table schema"
- Phase 4: "Migrate remaining Entry column_names uses"
- Phase 5: "Verification: all column_names uses migrated"
- Phase 6: "Remove column_names from Entry and entry_t"
- Phase 7: "Fix enclave-side encryption and operations"
- Phase 8: "Optimize and cleanup slim mode"

## Notes and Warnings

1. **Critical**: Test after EVERY change, no matter how small
2. **Security**: Ensure mixed tables don't leak table membership
3. **Enclave**: Be extra careful with enclave code changes
4. **Concatenation**: The trickiest part - may need creative solution
5. **Backup**: Consider creating a backup branch before starting

## Current Status

- [x] Planning complete
- [ ] Phase 1: Add schema to Table
- [ ] Phase 2: Migrate I/O
- [ ] Phase 3: Migrate query processing
- [ ] Phase 4: Find and migrate remaining uses
- [ ] Phase 5: Verification
- [ ] Phase 6: Remove column_names
- [ ] Phase 7: Fix enclave
- [ ] Phase 8: Optimization

## Commands for Testing

```bash
# After each change:
make clean && make -j4

# Test two_center_chain
./test/test_join /home/r33wei/omwj/memory_const/test_cases/queries/two_center_chain.sql /home/r33wei/omwj/memory_const/test_cases/encrypted

# Test TB1
./test/test_join /home/r33wei/omwj/memory_const/input/queries/tpch_tb1.sql /home/r33wei/omwj/memory_const/input/encrypted/data_0_001

# If something breaks, check what changed:
git status
git diff

# Revert if needed:
git checkout -- <file>
```