# TDX Migration TODO List

## Phase 1: Preparation & Analysis (1 hour)

- [x] **1.1** Create migration branch `tdx-migration`
- [x] **1.2** Document all current encryption points in codebase
- [x] **1.3** List all ecalls from Enclave.edl (23 total)
- [x] **1.4** List all ocalls (5 total: debug, refill_buffer, flush_to_group, etc.)
- [x] **1.5** Create backup of current working state

---

## Phase 2: Remove Encryption Fields from Entry (1 hour)

- [x] **2.1** Update `common/enclave_types.h` - remove `is_encrypted` and `nonce` from entry_t struct
- [x] **2.2** Update `app/data_structures/entry.h` - remove encryption members and methods
- [x] **2.3** Update `app/data_structures/entry.cpp` - remove encryption field initialization
- [x] **2.4** Fix `toEntryT()` and `fromEntryT()` methods
- [x] **2.5** Compile and fix immediate errors from removed fields
- [x] **2.6** Commit: "Phase 2: Remove encryption fields from Entry structure"

---

## Phase 3: Merge Enclave Code into App (2 hours)

- [x] **3.1** Create directory `app/core_logic/algorithms`
- [x] **3.2** Create directory `app/core_logic/operations`
- [x] **3.3** Create directory `app/core_logic/batch`
- [x] **3.4** Copy algorithms: heap_sort.c, k_way_merge.c, k_way_shuffle.c, oblivious_waksman.c/h, min_heap.c/h
- [x] **3.5** Copy operations: comparators.c, window_functions.c, transform_functions.c, distribute_functions.c, merge_comparators.c
- [x] **3.6** Copy batch: batch_dispatcher.c/h
- [x] **3.7** Copy core.h to app/core_logic/
- [x] **3.8** Update batch_dispatcher - remove crypto includes and encryption logic
- [ ] **3.9** Update include paths in all copied files (../../common → ../../../common)
- [ ] **3.10** Remove `#include "../crypto/..."` lines from all copied files
- [ ] **3.11** Remove `#include "Enclave_t.h"` from all copied files
- [ ] **3.12** Remove `#ifdef ENCLAVE_BUILD` conditionals (keep the code)
- [ ] **3.13** Update `oblivious_waksman.c` - replace SGX RNG with standard RNG (/dev/urandom)
- [ ] **3.14** Commit: "Phase 3: Merge enclave code into app/core_logic"

---

## Phase 4: Replace Ecalls with Direct Function Calls (2 hours)

- [ ] **4.1** Update `app/batch/ecall_batch_collector.cpp` - replace `ecall_batch_dispatcher()` with direct call
- [ ] **4.2** Add include for `core_logic/batch/batch_dispatcher.h`
- [ ] **4.3** Remove `global_eid` parameter from batch calls
- [ ] **4.4** Update `app/algorithms/merge_sort_manager.cpp` - replace `ecall_heap_sort()` with `heap_sort()`
- [ ] **4.5** Update k_way_merge calls: `ecall_k_way_merge_init/process/cleanup()` → direct calls
- [ ] **4.6** Update `app/algorithms/shuffle_manager.cpp` - replace all shuffle ecalls with direct calls
- [ ] **4.7** Replace `ecall_oblivious_2way_waksman()` with `oblivious_2way_waksman()`
- [ ] **4.8** Replace `ecall_k_way_shuffle_decompose/reconstruct()` with direct calls
- [ ] **4.9** Remove all SGX status checking (sgx_status_t, SGX_SUCCESS, etc.)
- [ ] **4.10** Update `app/batch/ecall_wrapper.h` - keep counters, remove ecall macros
- [ ] **4.11** Delete `app/utils/counted_ecalls.h` (no longer needed)
- [ ] **4.12** Commit: "Phase 4: Replace ecalls with direct function calls"

---

## Phase 5: Remove Encryption Layer (2 hours)

- [ ] **5.1** Delete directory `app/crypto/` entirely
- [ ] **5.2** Delete `main/tools/encrypt_tables.cpp`
- [ ] **5.3** Search for `CryptoUtils::encrypt_entry()` calls - remove all
- [ ] **5.4** Search for `CryptoUtils::decrypt_entry()` calls - remove all
- [ ] **5.5** Update `app/data_structures/table.cpp` - remove encryption operations
- [ ] **5.6** Grep for `#include "crypto/crypto_utils.h"` - remove from all files
- [ ] **5.7** Grep for `#include "entry_crypto.h"` - remove or keep minimal
- [ ] **5.8** Update `main/sgx_join/main.cpp` - remove any encrypt/decrypt on input/output
- [ ] **5.9** Commit: "Phase 5: Remove encryption layer completely"

---

## Phase 6: Simplify I/O Layer (1.5 hours)

- [ ] **6.1** Delete function `TableIO::save_encrypted_csv()` from table_io.cpp
- [ ] **6.2** Update `TableIO::save_csv()` - remove is_encrypted check
- [ ] **6.3** Update `TableIO::load_csv()` - remove nonce column detection
- [ ] **6.4** Remove nonce column parsing logic
- [ ] **6.5** Update `app/file_io/table_io.h` - remove save_encrypted_csv declaration
- [ ] **6.6** Remove `#include "sgx_urts.h"` from table_io.h
- [ ] **6.7** Update test data paths from `encrypted/` to `plaintext/` where needed
- [ ] **6.8** Commit: "Phase 6: Simplify I/O layer to plaintext only"

---

## Phase 7: Update Build System (1.5 hours)

- [ ] **7.1** Delete SGX SDK variables section from Makefile (lines 6-44)
- [ ] **7.2** Keep only DEBUG and SLIM_ENTRY flags
- [ ] **7.3** Remove `Gen_Untrusted_Source` and `Gen_Untrusted_Object` variables
- [ ] **7.4** Add `App_C_Files` list with all core_logic .c files
- [ ] **7.5** Update `App_Cpp_Files` - remove crypto_utils.cpp
- [ ] **7.6** Update `App_Objects` to include both C and C++ objects
- [ ] **7.7** Update `App_Include_Paths` to `-Icommon -Iapp -Iapp/core_logic`
- [ ] **7.8** Remove SGX SDK include paths
- [ ] **7.9** Update `App_Link_Flags` to just `-lpthread`
- [ ] **7.10** Remove all SGX library linking flags
- [ ] **7.11** Update compile flags - remove SGX-specific flags
- [ ] **7.12** Delete entire "Enclave Build" section (lines 267-318)
- [ ] **7.13** Delete EDL processing rules
- [ ] **7.14** Update main target - remove enclave.signed.so dependency
- [ ] **7.15** Add compilation rules for app/core_logic/**/*.c files
- [ ] **7.16** Update `Test_Include_Paths`
- [ ] **7.17** Update `Test_Common_Objects` - remove crypto, remove Gen_Untrusted_Object
- [ ] **7.18** Delete encrypt_tables tool target and objects
- [ ] **7.19** Commit: "Phase 7: Update build system for TDX"

---

## Phase 8: Update Main Application (1 hour)

- [ ] **8.1** Delete `sgx_enclave_id_t global_eid` variable from main.cpp
- [ ] **8.2** Delete `initialize_enclave()` function
- [ ] **8.3** Delete `destroy_enclave()` function
- [ ] **8.4** Remove `#include "sgx_urts.h"` from main.cpp
- [ ] **8.5** Remove `#include "Enclave_u.h"` from main.cpp
- [ ] **8.6** Remove enclave initialization call from main()
- [ ] **8.7** Remove enclave cleanup call from main()
- [ ] **8.8** Remove all `global_eid` usage
- [ ] **8.9** Remove SGX status checking
- [ ] **8.10** Commit: "Phase 8: Remove enclave from main application"

---

## Phase 9: Update Tests (1.5 hours)

- [ ] **9.1** Update `tests/integration/test_join.cpp` - remove enclave init, use plaintext
- [ ] **9.2** Update `tests/baseline/sqlite_baseline.cpp` - remove encryption steps
- [ ] **9.3** Delete `tests/unit/test_aes_crypto.cpp`
- [ ] **9.4** Delete `tests/unit/test_encryption.cpp`
- [ ] **9.5** Delete `tests/unit/test_encryption_comprehensive.cpp`
- [ ] **9.6** Delete `tests/unit/test_encryption_standalone.c`
- [ ] **9.7** Delete `tests/unit/test_secure_crypto.cpp`
- [ ] **9.8** Delete `tests/unit/verify_encryption.cpp`
- [ ] **9.9** Update `test_merge_sort.cpp` - remove SGX includes, use plaintext
- [ ] **9.10** Update `test_waksman_shuffle.cpp` - remove SGX includes
- [ ] **9.11** Update `test_waksman_distribution.cpp` - remove SGX includes
- [ ] **9.12** Update `test_shuffle_manager.cpp` - remove SGX includes
- [ ] **9.13** Update `test_comparators.cpp` - remove SGX includes
- [ ] **9.14** Update `test_window.cpp` - remove SGX includes
- [ ] **9.15** Update `scripts/run_tpch_tests.sh` - change to plaintext data directory
- [ ] **9.16** Update test targets in Makefile - remove crypto tests
- [ ] **9.17** Commit: "Phase 9: Update tests for TDX (plaintext)"

---

## Phase 10: Build & Debug (1.5 hours)

- [ ] **10.1** Run `make clean`
- [ ] **10.2** Run `make 2>&1 | tee build_errors.log`
- [ ] **10.3** Fix include errors (missing headers)
- [ ] **10.4** Fix undefined type errors (sgx_status_t, etc.)
- [ ] **10.5** Fix member access errors (removed fields)
- [ ] **10.6** Add `extern "C"` declarations for C functions called from C++
- [ ] **10.7** Fix linker errors (undefined references)
- [ ] **10.8** Verify all .c files are in Makefile
- [ ] **10.9** Verify functions are not static
- [ ] **10.10** Run `make` again - should succeed
- [ ] **10.11** Verify `sgx_app` executable is created
- [ ] **10.12** Run `ldd sgx_app` - verify no SGX libraries
- [ ] **10.13** Commit: "Phase 10: Build fixes and successful compilation"

---

## Phase 11: Testing & Validation (2 hours)

- [ ] **11.1** Build tests: `make tests 2>&1 | tee test_build.log`
- [ ] **11.2** Fix any test compilation errors
- [ ] **11.3** Run `./test_merge_sort` - verify passes
- [ ] **11.4** Run `./test_waksman_shuffle` - verify passes
- [ ] **11.5** Run `./test_waksman_distribution` - verify passes
- [ ] **11.6** Run `./test_shuffle_manager` - verify passes
- [ ] **11.7** Run `./test_comparators` - verify passes
- [ ] **11.8** Run `./test_join input/queries/tpch_tb1.sql input/plaintext/data_0_001`
- [ ] **11.9** Verify test_join completes successfully
- [ ] **11.10** Test all TPC-H queries on scale 0.001 (loop through all .sql files)
- [ ] **11.11** Document any failures
- [ ] **11.12** Test all TPC-H queries on scale 0.01
- [ ] **11.13** Run `./sgx_app input/queries/tpch_tb1.sql input/plaintext/data_0_001 output.csv`
- [ ] **11.14** Verify output.csv is created and looks correct
- [ ] **11.15** Run `./sqlite_baseline` on same query
- [ ] **11.16** Compare outputs: `diff output.csv baseline.csv`
- [ ] **11.17** Test performance: `time ./sgx_app ...`
- [ ] **11.18** Document all test results
- [ ] **11.19** Commit: "Phase 11: All tests passing"

---

## Phase 12: Documentation (1 hour)

- [ ] **12.1** Update CLAUDE.md - change SGX to TDX throughout
- [ ] **12.2** Update CLAUDE.md - remove encryption documentation
- [ ] **12.3** Update CLAUDE.md - remove enclave build instructions
- [ ] **12.4** Update CLAUDE.md - simplify prerequisites (remove SGX SDK)
- [ ] **12.5** Update README.md - title to "TDX-Compatible"
- [ ] **12.6** Update README.md - remove SGX prerequisites
- [ ] **12.7** Update README.md - simplify build instructions
- [ ] **12.8** Update README.md - update usage examples (plaintext data)
- [ ] **12.9** Create TDX_MIGRATION.md documenting the migration
- [ ] **12.10** Document what changed, why, and impact
- [ ] **12.11** Document performance improvements
- [ ] **12.12** Update PATHS.md if it exists
- [ ] **12.13** Commit: "Phase 12: Update documentation for TDX"

---

## Phase 13: Final Validation & Cleanup

- [ ] **13.1** Run full test suite one more time
- [ ] **13.2** Verify all unit tests pass
- [ ] **13.3** Verify all integration tests pass
- [ ] **13.4** Verify all TPC-H queries pass on both scales
- [ ] **13.5** Check for any compiler warnings - fix them
- [ ] **13.6** Run `make clean && make` - verify clean build
- [ ] **13.7** Review all changed files in git
- [ ] **13.8** Consider deleting `enclave/` directory (keep in git history)
- [ ] **13.9** Update `.gitignore` if needed
- [ ] **13.10** Final commit: "TDX migration complete - all tests passing"
- [ ] **13.11** Create git tag: `git tag v2.0-tdx`
- [ ] **13.12** Document migration completion date and results

---

## Success Criteria Checklist

- [ ] ✅ `make clean && make` completes without errors
- [ ] ✅ No compiler warnings
- [ ] ✅ All unit tests pass
- [ ] ✅ All integration tests pass (test_join)
- [ ] ✅ All TPC-H queries work on scale 0.001
- [ ] ✅ All TPC-H queries work on scale 0.01
- [ ] ✅ Outputs match SQLite baseline
- [ ] ✅ No SGX SDK dependencies (`ldd sgx_app` shows no SGX libs)
- [ ] ✅ Performance improved (faster than SGX version)
- [ ] ✅ Documentation updated and accurate
- [ ] ✅ Code is cleaner and simpler
- [ ] ✅ Git history is clean with good commit messages

---

## Notes & Blockers

### Known Issues:
(Document any issues encountered during migration)

### Performance Measurements:
(Document before/after performance)

### Test Results:
(Document test pass/fail status)

---

**Last Updated:** [Will be updated as migration progresses]
**Current Phase:** Phase 1 - Preparation
**Completion:** 0/13 phases complete
