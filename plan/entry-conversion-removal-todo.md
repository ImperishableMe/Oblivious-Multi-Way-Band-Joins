# TODO: Entry ↔ entry_t Conversion Removal

Status: Not Started
Branch: feature/remove-entry-conversion
Estimated Duration: 6.5 days

---

## Phase 0: Pre-Migration Preparation ⏳ (0.5 days)

### 0.1 Establish Baseline
- [ ] Build current system: `make clean && make && make tests`
- [ ] Run TPC-H 0.001 tests: `./scripts/run_tpch_tests.sh 0_001 > baseline_tpch_0_001.txt`
- [ ] Run TPC-H 0.01 tests: `./scripts/run_tpch_tests.sh 0_01 > baseline_tpch_0_01.txt`
- [ ] Run unit tests:
  - [ ] `./test_merge_sort > baseline_merge_sort.txt`
  - [ ] `./test_waksman_shuffle > baseline_waksman.txt`
  - [ ] `./test_bottom_up > baseline_bottom_up.txt`
  - [ ] `./test_top_down > baseline_top_down.txt`
  - [ ] `./test_distribute_expand > baseline_distribute.txt`
  - [ ] `./test_join_correctness > baseline_correctness.txt`
- [ ] Save output files: `mkdir -p baseline_outputs && cp output/*.csv baseline_outputs/`
- [ ] Verify all baselines captured successfully

### 0.2 Create Migration Branch
- [ ] Create branch: `git checkout -b feature/remove-entry-conversion`
- [ ] Push branch: `git push -u origin feature/remove-entry-conversion`

### 0.3 Audit Conversion Sites
- [ ] Find to_entry_t calls: `grep -rn "\.to_entry_t()" app/ main/ tests/ > audit_to_entry_t.txt`
- [ ] Find from_entry_t calls: `grep -rn "\.from_entry_t(" app/ main/ tests/ > audit_from_entry_t.txt`
- [ ] Find Entry declarations: `grep -rn "Entry " app/ main/ tests/ | grep -v entry_t > audit_entry_decl.txt`
- [ ] Find vector<Entry>: `grep -rn "vector<Entry>" app/ main/ tests/ > audit_vector_entry.txt`
- [ ] Review audit files for completeness

**Checkpoint**: All baseline outputs captured, audit complete

---

## Phase 1: Create C++ Compatibility Layer ⏳ (0.5 days)

### 1.1 Create entry_utils.h
- [ ] Create file: `common/entry_utils.h`
- [ ] Add include guards
- [ ] Add `entry_clear()` function
- [ ] Add `entry_init()` function
- [ ] Add `entry_to_string()` function
- [ ] Add `entry_equal()` function
- [ ] Add `entry_less_than()` function
- [ ] Add `#ifdef __cplusplus` guards
- [ ] Test compilation: `g++ -c -I. -Icommon test_entry_utils.cpp` (create minimal test)

### 1.2 Update table.h Header
- [ ] Open `app/data_structures/table.h`
- [ ] Replace `#include "entry.h"` with includes for enclave_types.h and entry_utils.h
- [ ] Change `std::vector<Entry> entries;` to `std::vector<entry_t> entries;`
- [ ] Update return types for get_entry() methods
- [ ] Update operator[] return types
- [ ] Add `get_entries_ref()` methods for direct vector access
- [ ] Add `data()` methods for raw pointer access
- [ ] Test compile: `make table.o` (expect linker errors, that's OK)

**Checkpoint**: entry_utils.h compiles cleanly, table.h updated

---

## Phase 2: Update Table Class Implementation ⏳ (1.0 days)

### 2.1 Update table.cpp - Entry Access
- [ ] Open `app/data_structures/table.cpp`
- [ ] Update `get_entry()` implementation
- [ ] Update `set_entry()` implementation
- [ ] Update `operator[]` implementation
- [ ] Update `add_entry()` implementation
- [ ] Update iterator methods (begin(), end())
- [ ] Replace Entry constructor calls with entry_init()

### 2.2 Remove Conversion Methods
- [ ] Delete `Table::to_entry_t_vector()` method
- [ ] Delete `Table::from_entry_t_vector()` method
- [ ] Remove declarations from table.h

### 2.3 Add Direct Vector Access
- [ ] Implement `get_entries_ref()` in table.cpp (if needed)
- [ ] Implement `data()` methods in table.cpp (if needed)
- [ ] Test compile: `make table.o`

### 2.4 Update map/linear_pass/parallel_pass
- [ ] Check `map()` implementation - should already work with entry_t*
- [ ] Check `linear_pass()` implementation
- [ ] Check `parallel_pass()` implementation
- [ ] Check `shuffle_merge_sort()` implementation
- [ ] Verify no Entry conversions remain in these methods

**Checkpoint**: table.cpp compiles cleanly, no conversion methods remain

---

## Phase 3: Update Manager Classes ⏳ (1.0 days)

### 3.1 Update MergeSortManager
- [ ] Open `app/algorithms/merge_sort_manager.h`
- [ ] Change `std::vector<std::vector<Entry>> runs;` to `std::vector<std::vector<entry_t>> runs;`
- [ ] Update method signatures to use entry_t
- [ ] Open `app/algorithms/merge_sort_manager.cpp`
- [ ] Update `sort_run_in_enclave()` - remove conversions
- [ ] Update `k_way_merge()` - remove conversions from heap operations
- [ ] Update `handle_refill_buffer()` - direct entry_t access
- [ ] Update `create_sorted_runs()` - use entry_t vectors
- [ ] Verify no to_entry_t/from_entry_t calls: `grep -n "to_entry_t\|from_entry_t" app/algorithms/merge_sort_manager.cpp`
- [ ] Test compile: `make merge_sort_manager.o`

### 3.2 Update ShuffleManager
- [ ] Open `app/algorithms/shuffle_manager.h`
- [ ] Change `std::vector<std::vector<Entry>> groups;` to `std::vector<std::vector<entry_t>> groups;`
- [ ] Change `std::vector<Entry> output_entries;` to `std::vector<entry_t> output_entries;`
- [ ] Update method signatures to use entry_t
- [ ] Open `app/algorithms/shuffle_manager.cpp`
- [ ] Update `shuffle_small()` - remove conversions
- [ ] Update `shuffle_large()` - remove conversions
- [ ] Update `handle_flush_to_group()` - direct entry_t access
- [ ] Update `handle_refill_from_group()` - direct entry_t access
- [ ] Update `handle_flush_output()` - direct entry_t access
- [ ] Verify no conversions: `grep -n "to_entry_t\|from_entry_t" app/algorithms/shuffle_manager.cpp`
- [ ] Test compile: `make shuffle_manager.o`

**Checkpoint**: Both managers compile, zero conversion calls remain

---

## Phase 4: Update Algorithm Phase Files ⏳ (1.0 days)

### 4.1 Update bottom_up_phase.cpp
- [ ] Open `app/algorithms/bottom_up_phase.cpp`
- [ ] Find direct C function call sites (e.g., transform_set_index_op)
- [ ] Remove entry_t temporary + conversion wrappers
- [ ] Call C functions directly on table entries
- [ ] Update any local Entry variables to entry_t
- [ ] Verify table operations still work (map, linear_pass, etc.)
- [ ] Test compile: `make bottom_up_phase.o`

### 4.2 Update top_down_phase.cpp
- [ ] Open `app/algorithms/top_down_phase.cpp`
- [ ] Remove Entry conversions
- [ ] Update local variables to entry_t
- [ ] Direct C function calls where applicable
- [ ] Test compile: `make top_down_phase.o`

### 4.3 Update distribute_expand.cpp
- [ ] Open `app/algorithms/distribute_expand.cpp`
- [ ] Remove Entry conversions
- [ ] Update local variables to entry_t
- [ ] Direct C function calls where applicable
- [ ] Test compile: `make distribute_expand.o`

### 4.4 Update align_concat.cpp
- [ ] Open `app/algorithms/align_concat.cpp`
- [ ] Remove Entry conversions
- [ ] Update local variables to entry_t
- [ ] Direct C function calls where applicable
- [ ] Test compile: `make align_concat.o`

### 4.5 Update oblivious_join.cpp
- [ ] Open `app/algorithms/oblivious_join.cpp`
- [ ] Update any Entry references
- [ ] Verify orchestration code works with entry_t
- [ ] Test compile: `make oblivious_join.o`

### 4.6 Verify Algorithm Directory Clean
- [ ] Run: `grep -rn "to_entry_t\|from_entry_t" app/algorithms/`
- [ ] Should output: (empty)
- [ ] Compile all: `make app/algorithms/*.o`

**Checkpoint**: All algorithm files compile, no conversions remain

---

## Phase 5: Update Supporting Files ⏳ (0.5 days)

### 5.1 Update JoinAttributeSetter
- [ ] Open `app/join/join_attribute_setter.h`
- [ ] Update method signatures to use entry_t
- [ ] Open `app/join/join_attribute_setter.cpp`
- [ ] Replace Entry with entry_t in implementations
- [ ] Update table access to work with entry_t
- [ ] Test compile: `make join_attribute_setter.o`

### 5.2 Update File I/O
- [ ] Open `app/file_io/table_io.cpp`
- [ ] Replace Entry with entry_t in CSV reading
- [ ] Replace Entry with entry_t in CSV writing
- [ ] Use entry_init() for initialization
- [ ] Open `app/file_io/converters.cpp`
- [ ] Update any Entry references to entry_t
- [ ] Test compile: `make table_io.o converters.o`

### 5.3 Update Main Entry Point
- [ ] Open `main/sgx_join/main.cpp`
- [ ] Replace any Entry references with entry_t
- [ ] Update includes if needed
- [ ] Test compile: `make main/sgx_join/main.o`

### 5.4 Update Other Join Files
- [ ] Check `app/join/join_condition.cpp`
- [ ] Check `app/join/join_constraint.cpp`
- [ ] Check `app/join/join_tree_builder.cpp`
- [ ] Update Entry to entry_t where needed
- [ ] Test compile each file

**Checkpoint**: All supporting files compile with entry_t

---

## Phase 6: Delete Entry Class ⏳ (0.5 days)

### 6.1 Remove Entry Class Files
- [ ] Run: `git rm app/data_structures/entry.h`
- [ ] Run: `git rm app/data_structures/entry.cpp`
- [ ] Verify files deleted: `git status`

### 6.2 Update Includes Throughout Codebase
- [ ] Find all entry.h includes: `grep -rn '#include.*entry\.h"' app/ main/ tests/`
- [ ] For each file found:
  - [ ] Replace `#include "entry.h"` with `#include "../../common/enclave_types.h"` and `#include "../../common/entry_utils.h"`
  - [ ] Adjust path as needed for file location
- [ ] Double-check no entry.h includes remain

### 6.3 First Full Build Attempt
- [ ] Run: `make clean`
- [ ] Run: `make 2>&1 | tee build_phase6.log`
- [ ] Review build_phase6.log for errors
- [ ] Fix compilation errors iteratively:
  - [ ] Missing includes
  - [ ] Type mismatches (Entry vs entry_t)
  - [ ] Method call syntax changes
  - [ ] Repeat build until clean
- [ ] Verify successful build: `ls -lh sgx_app`

### 6.4 Smoke Test
- [ ] Run basic command: `./sgx_app input/queries/tpch_tb1.sql input/plaintext/data_0_001 output.csv`
- [ ] Check exit code: `echo $?`
- [ ] Verify output.csv created
- [ ] Check for segfaults or crashes

**Checkpoint**: Full build succeeds, basic execution works, Entry class deleted

---

## Phase 7: Testing & Validation ⏳ (1.5 days)

### 7.1 Update & Build Unit Tests
- [ ] List all test files needing updates (24 files)
- [ ] For each test file in `tests/unit/`:
  - [ ] Replace Entry with entry_t
  - [ ] Replace Entry constructors with entry_init()
  - [ ] Update any Entry-specific method calls
  - [ ] Update includes
- [ ] Update integration tests in `tests/integration/`
- [ ] Update performance tests in `tests/performance/`
- [ ] Build all tests: `make tests 2>&1 | tee build_tests.log`
- [ ] Fix any compilation errors
- [ ] Verify clean build of all tests

### 7.2 Run Unit Tests vs Baseline
- [ ] `./test_merge_sort > new_merge_sort.txt 2>&1`
  - [ ] Compare: `diff baseline_merge_sort.txt new_merge_sort.txt`
- [ ] `./test_waksman_shuffle > new_waksman.txt 2>&1`
  - [ ] Compare: `diff baseline_waksman.txt new_waksman.txt`
- [ ] `./test_bottom_up > new_bottom_up.txt 2>&1`
  - [ ] Compare: `diff baseline_bottom_up.txt new_bottom_up.txt`
- [ ] `./test_top_down > new_top_down.txt 2>&1`
  - [ ] Compare: `diff baseline_top_down.txt new_top_down.txt`
- [ ] `./test_distribute_expand > new_distribute.txt 2>&1`
  - [ ] Compare: `diff baseline_distribute.txt new_distribute.txt`
- [ ] `./test_join_correctness > new_correctness.txt 2>&1`
  - [ ] Compare: `diff baseline_correctness.txt new_correctness.txt`
- [ ] Run remaining 18 unit tests
- [ ] Document any acceptable differences (timing, debug format)
- [ ] Investigate any unacceptable differences (results, sizes, values)

### 7.3 Run Integration Tests (TPC-H)
- [ ] Run TPC-H 0.001: `./scripts/run_tpch_tests.sh 0_001 > new_tpch_0_001.txt 2>&1`
- [ ] Compare: `diff baseline_tpch_0_001.txt new_tpch_0_001.txt`
- [ ] Run TPC-H 0.01: `./scripts/run_tpch_tests.sh 0_01 > new_tpch_0_01.txt 2>&1`
- [ ] Compare: `diff baseline_tpch_0_01.txt new_tpch_0_01.txt`
- [ ] Byte-for-byte CSV comparison:
  ```bash
  for f in baseline_outputs/*.csv; do
      fname=$(basename "$f")
      diff "$f" "output/$fname" || echo "DIFF FOUND: $fname"
  done
  ```
- [ ] Verify all CSVs are identical
- [ ] Check all queries show "Match: YES"
- [ ] Verify zero crashes or segfaults

### 7.4 Performance Benchmarking
- [ ] Switch to main branch: `git checkout main`
- [ ] Build: `make clean && make`
- [ ] Time large query: `time ./sgx_app input/queries/tpch_tm1.sql input/plaintext/data_0_01 output_before.csv`
- [ ] Record timing (real/user/sys)
- [ ] Switch to feature branch: `git checkout feature/remove-entry-conversion`
- [ ] Build: `make clean && make`
- [ ] Time same query: `time ./sgx_app input/queries/tpch_tm1.sql input/plaintext/data_0_01 output_after.csv`
- [ ] Record timing (real/user/sys)
- [ ] Verify identical output: `diff output_before.csv output_after.csv`
- [ ] Calculate speedup percentage
- [ ] Expected: 5-15% improvement

### 7.5 Obliviousness Verification
- [ ] Manual code inspection:
  - [ ] Verify all .c files unchanged: `git diff main -- app/core_logic/**/*.c`
  - [ ] Check no new data-dependent branches added to C++ code
  - [ ] Verify comparator_*_op functions unchanged
  - [ ] Verify oblivious_swap unchanged
  - [ ] Verify window/transform functions unchanged
- [ ] Access pattern testing (if DEBUG enabled):
  - [ ] Build with DEBUG: `DEBUG=1 make`
  - [ ] Run with dataset1: `./sgx_app query.sql dataset1/ out1.csv 2>&1 | grep "ACCESS:" > accesses1.log`
  - [ ] Run with dataset2: `./sgx_app query.sql dataset2/ out2.csv 2>&1 | grep "ACCESS:" > accesses2.log`
  - [ ] Compare: `diff accesses1.log accesses2.log` (should be identical)
- [ ] Document verification results

### 7.6 Memory Safety Checks
- [ ] Build: `make clean && make`
- [ ] Run Valgrind:
  ```bash
  valgrind --leak-check=full --show-leak-kinds=all \
      ./sgx_app input/queries/tpch_tb1.sql input/plaintext/data_0_001 output.csv \
      2>&1 | tee valgrind_check.log
  ```
- [ ] Review valgrind_check.log for:
  - [ ] Zero memory leaks
  - [ ] Zero invalid reads
  - [ ] Zero invalid writes
  - [ ] Zero use of uninitialized values
- [ ] Address any issues found
- [ ] Re-run Valgrind until clean

**Checkpoint**: All tests pass, outputs identical, performance improved, oblivious properties preserved, memory safe

---

## Phase 8: Cleanup & Documentation ⏳ (0.5 days)

### 8.1 Code Cleanup
- [ ] Search for Entry class remnants: `grep -rn "class Entry" app/ main/ tests/`
- [ ] Remove any dead code found
- [ ] Check for unused includes
- [ ] Update comments referencing "Entry class"
- [ ] Run code formatter (if applicable)
- [ ] Final code review

### 8.2 Update Documentation
- [ ] Update `CLAUDE.md`:
  - [ ] Remove Entry class references
  - [ ] Update data structure documentation
  - [ ] Add entry_utils.h documentation
- [ ] Update `TDX_MIGRATION_SUMMARY.md`:
  - [ ] Add section on conversion layer removal
  - [ ] Document performance improvements
  - [ ] List key changes
- [ ] Update `README.md`:
  - [ ] Update architecture description
  - [ ] Update project structure section
  - [ ] Mention entry type unification
- [ ] Create migration summary document with:
  - [ ] Lines of code removed
  - [ ] Performance improvement metrics
  - [ ] Test results summary

### 8.3 Update Build System
- [ ] Open `Makefile`
- [ ] Update comments about Entry class
- [ ] Remove entry.cpp from build list (if not already done)
- [ ] Verify App_Cpp_Files list is correct
- [ ] Test clean build: `make clean && make && make tests`

### 8.4 Final Git Status
- [ ] Review all changes: `git status`
- [ ] Review diff summary: `git diff --stat main`
- [ ] Check for untracked files that should be added
- [ ] Check for files that should be removed
- [ ] Verify .c files unchanged: `git diff main -- '*.c'`

**Checkpoint**: Code cleaned, documentation updated, ready for merge

---

## Phase 9: Merge & Deploy ⏳ (Final)

### 9.1 Pre-Merge Checklist
- [ ] All .c files unchanged: `git diff main -- app/core_logic/**/*.c`
- [ ] Zero to_entry_t calls: `grep -r "to_entry_t()" app/ main/ tests/`
- [ ] Zero from_entry_t calls: `grep -r "from_entry_t(" app/ main/ tests/`
- [ ] Entry class files deleted: `ls app/data_structures/entry.*` (should error)
- [ ] All 24 tests pass
- [ ] All TPC-H queries produce identical outputs
- [ ] Valgrind clean
- [ ] Performance improved (5-15%)
- [ ] Documentation updated
- [ ] Code review completed (self or peer)

### 9.2 Prepare for Merge
- [ ] Squash commits if needed: `git rebase -i main`
- [ ] Write comprehensive commit message
- [ ] Push final version: `git push origin feature/remove-entry-conversion`
- [ ] Create pull request (if using PR workflow)

### 9.3 Merge to Main
- [ ] Switch to main: `git checkout main`
- [ ] Merge feature branch: `git merge --no-ff --no-commit feature/remove-entry-conversion`
- [ ] Build and test in main: `make clean && make && make tests`
- [ ] Run TPC-H tests: `./scripts/run_tpch_tests.sh 0_001`
- [ ] If all good, commit merge
- [ ] Push to remote: `git push origin main`
- [ ] If issues, abort merge: `git reset --hard HEAD`

### 9.4 Post-Merge Tasks
- [ ] Tag release: `git tag -a v2.0.0-entry-unification -m "Remove Entry conversion layer"`
- [ ] Push tag: `git push origin v2.0.0-entry-unification`
- [ ] Close related GitHub issues
- [ ] Update project README with new version
- [ ] Announce changes to team/users
- [ ] Consider blog post about the optimization

**COMPLETE**: Migration successful, Entry conversion layer eliminated!

---

## Notes & Observations

### Key Metrics:
- **Lines of Code Removed**: ___ (track during Phase 6)
- **Conversion Sites Eliminated**: ~60
- **Performance Improvement**: ___% (measure in Phase 7.4)
- **Build Time**: Before ___, After ___
- **Binary Size**: Before ___, After ___
- **Test Execution Time**: Before ___, After ___

### Issues Encountered:
(Document any issues, workarounds, or lessons learned during migration)

---

### Rollback Instructions (If Needed):

**If in middle of migration**:
```bash
git checkout -- <affected_files>
git status
# Or for full rollback:
git checkout main
```

**If merge failed**:
```bash
git reset --hard HEAD
git checkout feature/remove-entry-conversion  # Preserve work
```

---

**Status Legend**:
- ⏳ Not Started
- 🚧 In Progress
- ✅ Completed
- ❌ Blocked/Failed
- ⏸️ Paused
