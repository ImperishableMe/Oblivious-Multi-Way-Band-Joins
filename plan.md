# SGX to TDX Migration Plan - Option C (Full OpenSSL Replacement)

## Strategy: Virtual Enclave with OpenSSL Crypto

**Approach:** Emulate SGX ecall/ocall interface as direct function calls. Move enclave code to `app/enclave_logic/`. Replace `sgx_tcrypto` with OpenSSL. **Zero test logic changes.**

---

## Summary

| Metric | Value |
|--------|-------|
| **Approach** | Option C - Full OpenSSL replacement |
| **Total Steps** | 43 tasks across 13 phases |
| **Estimated Time** | 12-16 hours (2-3 days) |
| **Files Created** | ~60 new files (copied from enclave/) + 5 compat files |
| **Files Modified** | ~25 files (includes, Makefile) |
| **Test Changes** | Include paths only - no logic changes |
| **Risk Level** | Medium (crypto replacement requires validation) |

---

## Todo List

### Phase 1: SGX Compatibility Layer (1 hour)

- [ ] **STEP 1.1:** Create app/sgx_compat/sgx_types.h
  - Define `sgx_enclave_id_t` (uint64_t), `sgx_status_t` (int)
  - Status codes: `SGX_SUCCESS=0`, `SGX_ERROR_*`
  - `SGX_CAST` macro
  - Copy from detailed plan in conversation

- [ ] **STEP 1.2:** Create app/sgx_compat/sgx_urts.h and .cpp
  - Implement `sgx_create_enclave()` - returns dummy eid=1
  - Implement `sgx_destroy_enclave()` - no-op
  - Define `SGX_DEBUG_FLAG`
  - Copy implementation from detailed plan

### Phase 2: Code Migration (1 hour)

- [ ] **STEP 2.1:** Create directory structure
  ```bash
  mkdir -p app/sgx_compat app/enclave_logic/{crypto,algorithms,operations,batch}
  ```

- [ ] **STEP 2.2:** Copy enclave code to app/enclave_logic
  ```bash
  cp enclave/trusted/crypto/*.{c,h} app/enclave_logic/crypto/
  cp enclave/trusted/algorithms/*.{c,h} app/enclave_logic/algorithms/
  cp enclave/trusted/operations/*.{c,h} app/enclave_logic/operations/
  cp enclave/trusted/batch/*.{c,h} app/enclave_logic/batch/
  cp enclave/trusted/core.h app/enclave_logic/
  cp enclave/trusted/secure_key.h app/enclave_logic/
  cp -r enclave/trusted/test app/enclave_logic/
  ```

- [ ] **STEP 2.3:** Update include paths in copied files
  - Change `../crypto/` → `crypto/`
  - Change `../Enclave_t.h` → `../../sgx_compat/Enclave_u.h`
  - Change `../../common/` → `../../../common/`
  - Use find/replace carefully

### Phase 3: Ecall Emulation Layer (3-4 hours)

- [ ] **STEP 3.1:** Create app/sgx_compat/Enclave_u.h
  - Declare all 23 ecalls with exact signatures from `enclave/untrusted/Enclave_u.h` lines 63-86
  - Declare 5 ocalls (debug_print, refill_buffer, flush_to_group, etc)
  - Include: `sgx_types.h`, `enclave_types.h`, `entry_crypto.h`

- [ ] **STEP 3.2:** Create app/sgx_compat/sgx_ecalls.cpp - Part 1: Crypto ecalls
  - `ecall_encrypt_entry()` → calls `aes_encrypt_entry()`
  - `ecall_decrypt_entry()` → calls `aes_decrypt_entry()`
  - `ecall_obtain_output_size()` → reads `entry->attributes[0]`

- [ ] **STEP 3.3:** Create app/sgx_compat/sgx_ecalls.cpp - Part 2: Batch/sort ecalls
  - `ecall_batch_dispatcher()` → calls `batch_dispatcher()`
  - `ecall_heap_sort()` → calls `heap_sort()`
  - `ecall_k_way_merge_init/process/cleanup()` → call corresponding functions

- [ ] **STEP 3.4:** Create app/sgx_compat/sgx_ecalls.cpp - Part 3: Shuffle ecalls
  - `ecall_oblivious_2way_waksman()` → calls `oblivious_2way_waksman()`
  - `ecall_k_way_shuffle_decompose()` → calls `k_way_shuffle_decompose()`
  - `ecall_k_way_shuffle_reconstruct()` → calls `k_way_shuffle_reconstruct()`

- [ ] **STEP 3.5:** Create app/sgx_compat/sgx_ecalls.cpp - Part 4: Test ecalls
  - Implement 13 test ecalls: `ecall_test_noop*`, `ecall_test_*_entries`
  - Most are no-ops or call test functions in `enclave_logic/test/`

### Phase 4: OpenSSL Migration (2-3 hours)

- [ ] **STEP 4.1:** Update app/enclave_logic/crypto/aes_crypto.c - Add OpenSSL helper
  - Replace `#include <sgx_tcrypto.h>` with `#include <openssl/evp.h>`
  - Add helper function:
    ```c
    static int openssl_aes_ctr_encrypt(
        const uint8_t* key,       // 16-byte key
        const uint8_t* plaintext,
        uint32_t plaintext_len,
        const uint8_t* ctr,       // 16-byte counter
        uint8_t* ciphertext
    ) {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return -1;

        if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), NULL, key, ctr) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return -1;
        }

        int len;
        if (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return -1;
        }

        int final_len;
        EVP_EncryptFinal_ex(ctx, ciphertext + len, &final_len);
        EVP_CIPHER_CTX_free(ctx);

        return 0;  // Success
    }
    ```

- [ ] **STEP 4.2:** Update `aes_encrypt_entry()` function
  - Replace `sgx_aes_ctr_encrypt()` call (line ~87) with `openssl_aes_ctr_encrypt()`
  - Update error handling: `sgx_status_t` → `int` (0=success, -1=failure)
  - Keep same encryption regions logic

- [ ] **STEP 4.3:** Update `aes_decrypt_entry()` function
  - Replace `sgx_aes_ctr_decrypt()` call (line ~158) with `openssl_aes_ctr_encrypt()`
  - Note: CTR mode decrypt = encrypt
  - Update error handling
  - Keep same decryption regions logic

- [ ] **STEP 4.4:** Update app/enclave_logic/algorithms/oblivious_waksman.c
  - In `get_switch_bit()` function (line ~40-70)
  - Replace `sgx_aes_ctr_encrypt()` with `openssl_aes_ctr_encrypt()`
  - Keep same counter block construction and bit extraction
  - Preserve fallback hash logic

- [ ] **STEP 4.5:** Update key management in aes_crypto.c
  - Keep `init_aes_key()` mostly same
  - Document that `SECURE_ENCRYPTION_KEY` is now just a constant (not SGX-sealed)
  - Add TODO for production: proper key derivation (PBKDF2) or config file
  - Keep nonce generation unchanged

### Phase 5: Build System Updates (1-2 hours)

- [ ] **STEP 5.1:** Update Makefile - Remove SGX variables
  - Delete: `SGX_SDK`, `SGX_MODE`, `SGX_ARCH`, `SGX_DEBUG`
  - Delete: `SGX_EDGER8R`, `SGX_ENCLAVE_SIGNER`
  - Delete: SGX library paths and names
  - Keep: `DEBUG`, `SLIM_ENTRY` flags

- [ ] **STEP 5.2:** Update Makefile - Source file lists
  - Add `App_C_Files`:
    ```makefile
    App_C_Files := app/enclave_logic/crypto/aes_crypto.c \
                   app/enclave_logic/crypto/crypto_helpers.c \
                   app/enclave_logic/algorithms/k_way_merge.c \
                   app/enclave_logic/algorithms/k_way_shuffle.c \
                   app/enclave_logic/algorithms/heap_sort.c \
                   app/enclave_logic/algorithms/oblivious_waksman.c \
                   app/enclave_logic/operations/comparators.c \
                   app/enclave_logic/operations/window_functions.c \
                   app/enclave_logic/operations/transform_functions.c \
                   app/enclave_logic/operations/distribute_functions.c \
                   app/enclave_logic/operations/merge_comparators.c \
                   app/enclave_logic/batch/batch_dispatcher.c \
                   app/enclave_logic/debug_wrapper.c
    ```
  - Add to `App_Cpp_Files`:
    ```makefile
    app/sgx_compat/sgx_urts.cpp \
    app/sgx_compat/sgx_ecalls.cpp
    ```
  - Remove `Gen_Untrusted_Source`

- [ ] **STEP 5.3:** Update Makefile - Include paths
  ```makefile
  App_Include_Paths := -Icommon -Iapp -Iapp/sgx_compat -Iapp/enclave_logic
  ```
  - Remove: `-I$(SGX_SDK)/include`, `-Ienclave/untrusted`

- [ ] **STEP 5.4:** Update Makefile - Link flags
  ```makefile
  App_Link_Flags := -lpthread -lssl -lcrypto
  ```
  - Remove: `-lsgx_urts`, `-lsgx_uae_service`, etc.

- [ ] **STEP 5.5:** Update Makefile - Compile flags
  ```makefile
  App_Compile_CFlags := -fPIC -Wno-attributes $(App_Include_Paths) -DENCLAVE_BUILD
  App_Compile_CXXFlags := $(App_Compile_CFlags) -std=c++11
  ```
  - Remove SGX-specific flags

- [ ] **STEP 5.6:** Update Makefile - Remove EDL processing
  - Delete rules for: `$(Gen_Untrusted_Source)`
  - Delete rules for: `Enclave.signed.so`
  - Delete: enclave signing steps
  - Delete: references to `sgx_edger8r`

### Phase 6: Update Include Directives (30 min)

- [ ] **STEP 6.1:** Update main/sgx_join/main.cpp
  - Change `#include "sgx_urts.h"` → `#include "sgx_compat/sgx_urts.h"`
  - Change `#include "Enclave_u.h"` → `#include "sgx_compat/Enclave_u.h"`
  - No other changes needed

- [ ] **STEP 6.2:** Update app/crypto/crypto_utils.cpp
  - Change `#include "sgx_urts.h"` → `#include "sgx_compat/sgx_urts.h"`
  - Update via `counted_ecalls.h` to use compat version

- [ ] **STEP 6.3:** Update app/utils/counted_ecalls.h
  - Change `#include "Enclave_u.h"` → `#include "sgx_compat/Enclave_u.h"`
  - This file wraps ecalls for counting - no logic changes

- [ ] **STEP 6.4:** Update all test files (15+ files)
  ```bash
  # In tests/unit/, tests/integration/, tests/performance/
  find tests/ -name '*.cpp' -exec sed -i 's|"sgx_urts.h"|"sgx_compat/sgx_urts.h"|g' {} \;
  find tests/ -name '*.cpp' -exec sed -i 's|"Enclave_u.h"|"sgx_compat/Enclave_u.h"|g' {} \;
  ```
  - Verify with `git diff` before committing

### Phase 7: Build and Debug (1-2 hours)

- [ ] **STEP 7.1:** Initial build attempt
  ```bash
  make clean && make 2>&1 | tee build.log
  ```
  - Expected: many compilation errors
  - Create error log for analysis
  - This validates our setup is being compiled

- [ ] **STEP 7.2:** Fix compilation errors
  - Based on `build.log`, fix:
    - Missing function declarations
    - Wrong return types
    - Include path issues
  - May need forward declarations or adjust ecall signatures

- [ ] **STEP 7.3:** Fix linker errors
  - Verify: 1) .c files added to Makefile
  - Verify: 2) functions not static
  - Verify: 3) correct `extern "C"` usage
  - Verify: 4) OpenSSL linked (`-lssl -lcrypto`)
  - Check for undefined references

- [ ] **STEP 7.4:** Successful build
  ```bash
  make
  ```
  - Output: `./sgx_app` executable
  - Check file size reasonable (~5-10MB)
  - Verify: `ldd sgx_app | grep -E 'libssl|libcrypto'`

### Phase 8: Unit Testing (1 hour)

- [ ] **STEP 8.1:** Build test binaries
  ```bash
  make tests
  ```
  - Expected targets: `test_aes_crypto`, `test_waksman_shuffle`, `test_merge_sort`, `test_comparators`, `test_join`, `sqlite_baseline`
  - Verify all compile successfully

- [ ] **STEP 8.2:** Test crypto
  ```bash
  ./test_aes_crypto
  ```
  - Verify: 1) Encryption/decryption succeeds
  - Verify: 2) Encrypted data differs from plaintext
  - Verify: 3) `Decrypt(Encrypt(x)) = x`
  - Verify: 4) No crashes
  - Check output matches expected

- [ ] **STEP 8.3:** Test Waksman shuffle
  ```bash
  ./test_waksman_shuffle
  ```
  - Verify: 1) Shuffle completes without crash
  - Verify: 2) Output is permutation (no duplicates/missing)
  - Verify: 3) Different runs produce different permutations

- [ ] **STEP 8.4:** Test merge sort
  ```bash
  ./test_merge_sort
  ```
  - Verify: 1) Sorting produces correct order
  - Verify: 2) All entries preserved (count unchanged)
  - Verify: 3) Comparator logic works
  - Test small (n=10) and large (n=1000) cases

- [ ] **STEP 8.5:** Test comparators and operations
  ```bash
  ./test_comparators
  ```
  - Verify window functions work
  - Verify transform operations work
  - Verify distribute operations work
  - Check batch dispatcher calls operations properly

### Phase 9: Integration Testing (2 hours)

- [ ] **STEP 9.1:** Integration test - small query
  ```bash
  ./test_join input/queries/tpch_tb1.sql input/encrypted/data_0_001
  ```
  - Verify: 1) Completes without crash
  - Verify: 2) Produces output
  - Verify: 3) No assertion failures
  - Save output for comparison

- [ ] **STEP 9.2:** Compare with SQLite baseline
  ```bash
  ./sqlite_baseline input/queries/tpch_tb1.sql input/encrypted/data_0_001 baseline.csv
  diff output.csv baseline.csv
  ```
  - Should be identical (both use same encryption)
  - Investigate any differences

- [ ] **STEP 9.3:** Integration test - all TPC-H queries
  - Run `test_join` on all queries: `tpch_tb1.sql` through `tpch_tb10.sql`
  - Use both `data_0_001` and `data_0_01` scales
  - All should pass and match baseline

- [ ] **STEP 9.4:** Main application test
  ```bash
  ./sgx_app input/queries/tpch_tb1.sql input/encrypted/data_0_001 output.csv
  ```
  - Verify output.csv is valid, encrypted
  - Verify matches expected results
  - Try with `data_0_01` scale as well

- [ ] **STEP 9.5:** Test encrypt_tables tool
  ```bash
  ./encrypt_tables input/plaintext/data_0_001 /tmp/test_encrypted
  ```
  - Verify: 1) Creates encrypted files
  - Verify: 2) Can decrypt and read data
  - Verify: 3) `sgx_app` can process encrypted data
  - Clean up: `rm -rf /tmp/test_encrypted`

### Phase 10: Performance Measurement (1 hour)

- [ ] **STEP 10.1:** Performance measurement
  ```bash
  ./overhead_measurement > tdx_perf.txt
  ```
  - Compare with previous SGX results if available
  - Expect 10-50x improvement on ecall overhead sections
  - Document results

- [ ] **STEP 10.2:** Crypto performance
  ```bash
  ./overhead_crypto_breakdown > tdx_crypto.txt
  ```
  - Verify OpenSSL crypto performance reasonable
  - Should be similar to or better than `sgx_tcrypto`
  - Document encrypt/decrypt times per entry

- [ ] **STEP 10.3:** Optional - Increase MAX_BATCH_SIZE
  - Edit `common/batch_types.h`: change 2000 → 5000
  - Rebuild and test
  - Measure if performance improves
  - If yes, benchmark 10000, 20000
  - Find optimal value for TDX memory

### Phase 11: Documentation (1 hour)

- [ ] **STEP 11.1:** Update CLAUDE.md
  - Change: `SGX enclave` → `TDX VM`
  - Change: `ecall/ocall` → `direct function calls (emulated)`
  - Change: `SGX SDK` → `OpenSSL`
  - Change: `enclave.signed.so` → `sgx_app`
  - Keep algorithm descriptions unchanged
  - Add "TDX Migration Notes" section

- [ ] **STEP 11.2:** Update README.md
  - Title: `Oblivious Multi-Way Join (TDX-Compatible)`
  - Prerequisites: Remove Intel SGX SDK/PSW, add OpenSSL 1.1+
  - Build: Update to `make` (no enclave signing)
  - Usage: same but note runs on any Linux
  - Add migration history note

- [ ] **STEP 11.3:** Create MIGRATION.md
  - Document: 1) What was changed (SGX → TDX)
  - Document: 2) Why (no SGX hardware)
  - Document: 3) What's compatible (all tests, same outputs)
  - Document: 4) What's different (OpenSSL, no enclave)
  - Document: 5) Performance differences
  - Include before/after comparison

### Phase 12: Final Validation (30 min)

- [ ] **STEP 12:** Final validation
  ```bash
  make tests
  # Run all test_* binaries
  ./test_aes_crypto
  ./test_waksman_shuffle
  ./test_merge_sort
  ./test_comparators
  ./test_join input/queries/tpch_tb1.sql input/encrypted/data_0_001
  # ... run all queries on both scales
  ```
  - Verify all pass
  - Document any failures
  - Get confirmation all outputs match expected/baseline

### Phase 13: Cleanup (30 min)

- [ ] **STEP 13:** Optional cleanup and commit
  - Consider deleting old `enclave/` directory (keep in git history)
  - Remove unused EDL files
  - Clean up temporary files
  - Update `.gitignore` if needed
  - Commit:
    ```bash
    git add -A
    git commit -m "Complete SGX to TDX migration (Option C - OpenSSL)"
    ```

---

## Critical References

### Key Enclave Functions to Implement (in sgx_ecalls.cpp)

From `enclave/untrusted/Enclave_u.h`:
```c
// Lines 63-86: All 23 ecalls
ecall_encrypt_entry()
ecall_decrypt_entry()
ecall_obtain_output_size()
ecall_batch_dispatcher()
ecall_heap_sort()
ecall_k_way_merge_init()
ecall_k_way_merge_process()
ecall_k_way_merge_cleanup()
ecall_oblivious_2way_waksman()
ecall_k_way_shuffle_decompose()
ecall_k_way_shuffle_reconstruct()
// + 13 test ecalls
```

### OpenSSL API Mapping

| SGX Function | OpenSSL Equivalent |
|--------------|-------------------|
| `sgx_aes_ctr_encrypt()` | `EVP_EncryptInit_ex()` + `EVP_EncryptUpdate()` + `EVP_EncryptFinal_ex()` |
| `sgx_aes_ctr_decrypt()` | Same (CTR mode: decrypt = encrypt) |
| `sgx_aes_ctr_128bit_key_t` | `uint8_t[16]` |

### File Structure After Migration

```
app/
├── sgx_compat/              # NEW: SGX emulation
│   ├── sgx_types.h
│   ├── sgx_urts.h/cpp
│   ├── Enclave_u.h
│   └── sgx_ecalls.cpp
├── enclave_logic/           # NEW: Moved from enclave/trusted/
│   ├── crypto/
│   │   ├── aes_crypto.c     # MODIFIED: OpenSSL
│   │   └── crypto_helpers.c
│   ├── algorithms/
│   │   ├── k_way_merge.c
│   │   ├── k_way_shuffle.c
│   │   ├── heap_sort.c
│   │   └── oblivious_waksman.c  # MODIFIED: OpenSSL RNG
│   ├── operations/
│   ├── batch/
│   └── test/
└── (existing app/ structure)
```

---

## Success Criteria

✅ `make clean && make` completes without errors
✅ All unit tests pass
✅ All integration tests pass
✅ Output matches SQLite baseline byte-for-byte
✅ No SGX SDK dependency (only OpenSSL)
✅ `ldd sgx_app` shows libssl.so and libcrypto.so
✅ Performance improves 10-50x on ecall-heavy operations
✅ Documentation updated and accurate

---

## Troubleshooting

### Common Issues

**Build fails with "sgx_tcrypto.h not found"**
- Check that `aes_crypto.c` was updated with `#include <openssl/evp.h>`
- Verify OpenSSL is installed: `dpkg -l | grep libssl-dev`

**Linker error: undefined reference to `batch_dispatcher`**
- Check `app/enclave_logic/batch/batch_dispatcher.c` added to `App_C_Files` in Makefile
- Verify function is not `static` in the .c file
- Add `extern "C"` wrapper if needed in header

**Test fails with different encryption output**
- OpenSSL and SGX crypto should produce identical outputs
- Check nonce generation hasn't changed
- Verify AES-CTR mode parameters (128-bit counter)
- Compare key derivation in `init_aes_key()`

**Performance is worse than expected**
- Verify Debug mode is off: `DEBUG=0 make`
- Check batch size: increase if RAM available
- Profile with `perf` to find bottlenecks

---

## Estimated Timeline

| Day | Tasks | Hours |
|-----|-------|-------|
| **Day 1** | Steps 1-6 (setup, migration, ecalls, includes) | 6-8h |
| **Day 2** | Steps 7-9 (build, debug, testing) | 4-6h |
| **Day 3** | Steps 10-13 (performance, docs, validation) | 2-4h |
| **Total** | All 43 steps | **12-18 hours** |

---

## Next Steps

1. Review this plan thoroughly
2. Ask any questions about specific steps
3. When ready, start with **STEP 1.1**: Create `app/sgx_compat/sgx_types.h`
4. Work through steps sequentially
5. Mark off completed tasks
6. Report any blockers immediately

Good luck with the migration! 🚀
