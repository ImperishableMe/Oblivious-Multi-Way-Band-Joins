# SGX to TDX Migration Plan - Detailed Implementation Guide

## Executive Summary

This document outlines the migration from Intel SGX enclave-based architecture to Intel TDX (Trust Domain Extensions) VM-based architecture. The key insight is that **TDX encrypts the entire VM and filesystem**, making application-level encryption redundant. This allows us to simplify the codebase significantly.

## Migration Strategy

### Core Principle
**Remove all application-level encryption** and **eliminate the enclave boundary** since TDX provides transparent VM-level encryption and memory protection.

### What Changes
1. ❌ **Remove**: All encryption/decryption code (app/crypto, enclave/crypto)
2. ❌ **Remove**: Enclave boundary (ecalls, ocalls, EDL files)
3. ❌ **Remove**: Entry encryption fields (nonce, is_encrypted)
4. ❌ **Remove**: SGX SDK dependencies
5. ✅ **Keep**: All oblivious algorithms (join, shuffle, merge)
6. ✅ **Keep**: All data structures and business logic
7. ✅ **Keep**: Test infrastructure (with plaintext data)

### What Stays the Same
- Oblivious join algorithms
- Memory access patterns (still data-oblivious)
- Entry metadata structure
- Join tree construction
- Query parsing
- Test queries and validation

---

## Detailed Phase Breakdown

### Phase 1: Preparation & Analysis (1 hour)

#### Step 1.1: Create Migration Branch
```bash
git checkout -b tdx-migration
git branch --set-upstream-to=origin/main
```

#### Step 1.2: Document Current Encryption Points
Identify all encryption usage:
- `app/crypto/crypto_utils.cpp` - encrypt/decrypt entry functions
- `enclave/trusted/crypto/` - AES encryption implementation
- `app/file_io/table_io.cpp` - save_encrypted_csv, load detection
- `main/tools/encrypt_tables.cpp` - encryption tool
- Tests using encrypted data

#### Step 1.3: Document Current Ecalls
From `enclave/trusted/Enclave.edl`:
- `ecall_encrypt_entry` - REMOVE
- `ecall_decrypt_entry` - REMOVE
- `ecall_obtain_output_size` - Keep logic, remove ecall
- `ecall_batch_dispatcher` - Replace with direct calls
- `ecall_heap_sort` - Replace with direct call
- `ecall_k_way_merge_*` - Replace with direct calls
- `ecall_oblivious_2way_waksman` - Replace with direct call
- `ecall_k_way_shuffle_*` - Replace with direct calls

#### Step 1.4: Document Current Ocalls
- `ocall_debug_print` - Replace with printf/std::cerr
- `ocall_refill_buffer` - Merge into caller
- `ocall_flush_to_group` - Merge into caller
- `ocall_refill_from_group` - Merge into caller
- `ocall_flush_output` - Merge into caller

---

### Phase 2: Remove Encryption Fields from Entry (1 hour)

#### Step 2.1: Update common/enclave_types.h

**Current entry_t structure (lines 52-110):**
```c
typedef struct {
    int32_t field_type;
    int32_t equality_type;
    uint8_t is_encrypted;         // ← REMOVE THIS
    uint64_t nonce;               // ← REMOVE THIS
    int32_t join_attr;
    // ... rest of fields
} entry_t;
```

**New entry_t structure:**
```c
typedef struct {
    int32_t field_type;
    int32_t equality_type;
    // Removed: is_encrypted, nonce
    int32_t join_attr;
    // ... rest of fields unchanged
} entry_t;
```

#### Step 2.2: Update app/data_structures/entry.h

Remove from Entry class:
- `is_encrypted` member variable
- `nonce` member variable
- `setEncrypted()` method
- `isEncrypted()` method
- Any encryption-related methods

#### Step 2.3: Update app/data_structures/entry.cpp

- Remove encryption field initialization in constructors
- Remove `toEntryT()` encryption field copying
- Remove `fromEntryT()` encryption field copying
- Fix any compilation errors

#### Step 2.4: Update common/entry_crypto.h

This file defines crypto_status_t enum - we can delete this entire file or keep it minimal if needed for compatibility during transition.

---

### Phase 3: Merge Enclave Code into App (2 hours)

#### Step 3.1: Create New Directory Structure

```bash
mkdir -p app/core_logic/algorithms
mkdir -p app/core_logic/operations
mkdir -p app/core_logic/batch
```

#### Step 3.2: Copy Enclave Code (Excluding Crypto)

```bash
# Copy algorithms (keep these - they're core logic)
cp enclave/trusted/algorithms/heap_sort.c app/core_logic/algorithms/
cp enclave/trusted/algorithms/k_way_merge.c app/core_logic/algorithms/
cp enclave/trusted/algorithms/k_way_shuffle.c app/core_logic/algorithms/
cp enclave/trusted/algorithms/oblivious_waksman.c app/core_logic/algorithms/
cp enclave/trusted/algorithms/oblivious_waksman.h app/core_logic/algorithms/
cp enclave/trusted/algorithms/min_heap.c app/core_logic/algorithms/
cp enclave/trusted/algorithms/min_heap.h app/core_logic/algorithms/

# Copy operations (keep these - they're core logic)
cp enclave/trusted/operations/comparators.c app/core_logic/operations/
cp enclave/trusted/operations/window_functions.c app/core_logic/operations/
cp enclave/trusted/operations/transform_functions.c app/core_logic/operations/
cp enclave/trusted/operations/distribute_functions.c app/core_logic/operations/
cp enclave/trusted/operations/merge_comparators.c app/core_logic/operations/

# Copy batch dispatcher (will be simplified)
cp enclave/trusted/batch/batch_dispatcher.c app/core_logic/batch/
cp enclave/trusted/batch/batch_dispatcher.h app/core_logic/batch/

# Copy core headers
cp enclave/trusted/core.h app/core_logic/

# Skip crypto entirely - we're deleting it
# Skip debug_wrapper.c - we'll replace with direct calls
# Skip test_crypto_ecalls.c - we're deleting crypto tests
```

#### Step 3.3: Update Include Paths in Copied Files

For each file in `app/core_logic/`:

**Old includes:**
```c
#include "../../common/enclave_types.h"
#include "../crypto/aes_crypto.h"
#include "../../Enclave_t.h"
```

**New includes:**
```c
#include "../../../common/enclave_types.h"
// Remove crypto includes entirely
// Remove Enclave_t.h
```

Use Edit tool to update each file systematically.

#### Step 3.4: Remove ENCLAVE_BUILD Conditionals

Search for `#ifdef ENCLAVE_BUILD` blocks and remove the conditionals (keep the code).

#### Step 3.5: Remove SGX-Specific Code

In `oblivious_waksman.c`, the `get_switch_bit()` function uses SGX crypto for randomness:
- Replace `sgx_aes_ctr_encrypt()` with standard random number generation
- Use `/dev/urandom` or C++ `<random>` for cryptographic randomness
- This is okay because TDX provides secure random via VM isolation

---

### Phase 4: Replace Ecalls with Direct Function Calls (2 hours)

#### Step 4.1: Update Batch Operations

**File: app/batch/ecall_batch_collector.cpp**

**Current code (lines ~100-150):**
```cpp
void ECallBatchCollector::flush() {
    // ... preparation code ...

    // Call ecall_batch_dispatcher
    ecall_batch_dispatcher(global_eid,
                          data_array, data_count,
                          ops_buffer, ops_count, ops_size,
                          operation_type);

    // ... cleanup code ...
}
```

**New code:**
```cpp
void ECallBatchCollector::flush() {
    // ... preparation code ...

    // Direct call to batch_dispatcher (no ecall)
    batch_dispatcher(data_array, data_count,
                    ops_buffer, ops_count, ops_size,
                    operation_type);

    // ... cleanup code ...
}
```

Need to:
1. Include `core_logic/batch/batch_dispatcher.h`
2. Remove `global_eid` parameter
3. Remove all SGX status checking

#### Step 4.2: Update Sort Operations

**File: app/algorithms/merge_sort_manager.cpp**

**Current code:**
```cpp
sgx_status_t status = ecall_heap_sort(global_eid, &retval,
                                      data_array, size,
                                      comparator_type);
if (status != SGX_SUCCESS) {
    // handle error
}
```

**New code:**
```cpp
heap_sort(data_array, size, comparator_type);
```

#### Step 4.3: Update Shuffle Operations

**File: app/algorithms/shuffle_manager.cpp**

Replace:
- `ecall_oblivious_2way_waksman()` → `oblivious_2way_waksman()`
- `ecall_k_way_shuffle_decompose()` → `k_way_shuffle_decompose()`
- `ecall_k_way_shuffle_reconstruct()` → `k_way_shuffle_reconstruct()`

Remove:
- `global_eid` parameter
- SGX status checking
- Error handling for ecall failures

#### Step 4.4: Update K-Way Merge

**File: app/algorithms/merge_sort_manager.cpp**

Replace:
- `ecall_k_way_merge_init()` → `k_way_merge_init()`
- `ecall_k_way_merge_process()` → `k_way_merge_process()`
- `ecall_k_way_merge_cleanup()` → `k_way_merge_cleanup()`

#### Step 4.5: Remove Ecall Wrapper Infrastructure

Delete or gut these files:
- `app/batch/ecall_wrapper.h` - Keep only counter functions, remove ecall macros
- `app/batch/ecall_wrapper.cpp` - Keep counters, remove ecall wrappers
- `app/utils/counted_ecalls.h` - Delete entirely (was just ecall counting)

---

### Phase 5: Remove Encryption Layer (2 hours)

#### Step 5.1: Delete Crypto Files

```bash
# Delete app-side crypto
rm -rf app/crypto/

# Delete enclave crypto (already skipped in copy)
# Just note we won't use enclave/trusted/crypto/

# Delete encryption tool
rm main/tools/encrypt_tables.cpp
```

#### Step 5.2: Remove Encrypt/Decrypt Calls in Table Operations

**File: app/data_structures/table.cpp**

Search for:
- `CryptoUtils::encrypt_entry()`
- `CryptoUtils::decrypt_entry()`
- Any loops encrypting/decrypting entries

Remove all such calls.

#### Step 5.3: Remove Crypto Includes

Throughout codebase:
```cpp
#include "crypto/crypto_utils.h"  // DELETE
#include "entry_crypto.h"          // DELETE or keep minimal
```

Use Grep to find all occurrences:
```bash
grep -r "crypto_utils.h" app/
grep -r "entry_crypto.h" app/
```

Then use Edit tool to remove these includes.

#### Step 5.4: Remove Encryption from Main

**File: main/sgx_join/main.cpp**

Remove any encrypt/decrypt operations on input/output tables.

---

### Phase 6: Simplify I/O Layer (1.5 hours)

#### Step 6.1: Update TableIO - Remove Encrypted CSV Support

**File: app/file_io/table_io.cpp**

**Delete function:**
```cpp
void TableIO::save_encrypted_csv(const Table& table,
                                 const std::string& filepath,
                                 sgx_enclave_id_t eid) {
    // DELETE THIS ENTIRE FUNCTION
}
```

**Update function:**
```cpp
void TableIO::save_csv(const Table& table, const std::string& filepath) {
    // REMOVE THIS CHECK:
    // if (entry.isEncrypted()) throw runtime_error("Cannot save encrypted");

    // Just save plaintext values directly
}
```

#### Step 6.2: Update TableIO - Simplify Load CSV

**File: app/file_io/table_io.cpp**

**Current load_csv():**
- Checks for "nonce" column
- Auto-detects encrypted vs plaintext
- Sets is_encrypted flag

**New load_csv():**
- Just load column values
- No nonce handling
- No encryption detection

Remove lines handling nonce column detection and parsing.

#### Step 6.3: Update TableIO Header

**File: app/file_io/table_io.h**

Remove:
```cpp
static void save_encrypted_csv(const Table& table,
                               const std::string& filepath,
                               sgx_enclave_id_t eid);
```

Remove `sgx_urts.h` include.

#### Step 6.4: Update Test Data Paths

Tests currently use `input/encrypted/data_0_001` - change to `input/plaintext/data_0_001`.

Or verify that encrypted data can be read as plaintext if we just ignore nonce column.

---

### Phase 7: Update Build System (1.5 hours)

#### Step 7.1: Remove SGX SDK Variables

**File: Makefile**

**Delete lines 6-44:**
```makefile
######## SGX SDK Settings ########
SGX_SDK ?= /opt/intel/sgxsdk
SGX_MODE ?= HW
# ... entire SGX section
```

**Keep only:**
```makefile
# Debug configuration
DEBUG ?= 0
SLIM_ENTRY ?= 0
```

#### Step 7.2: Update Source File Lists

**File: Makefile**

**Remove:**
```makefile
Gen_Untrusted_Source := enclave/untrusted/Enclave_u.c
Gen_Untrusted_Object := enclave/untrusted/Enclave_u.o
```

**Add C Files from core_logic:**
```makefile
App_C_Files := app/core_logic/algorithms/heap_sort.c \
               app/core_logic/algorithms/k_way_merge.c \
               app/core_logic/algorithms/k_way_shuffle.c \
               app/core_logic/algorithms/oblivious_waksman.c \
               app/core_logic/algorithms/min_heap.c \
               app/core_logic/operations/comparators.c \
               app/core_logic/operations/window_functions.c \
               app/core_logic/operations/transform_functions.c \
               app/core_logic/operations/distribute_functions.c \
               app/core_logic/operations/merge_comparators.c \
               app/core_logic/batch/batch_dispatcher.c
```

**Update App_Cpp_Files:**
```makefile
App_Cpp_Files := main/sgx_join/main.cpp \
                 app/file_io/converters.cpp \
                 app/file_io/table_io.cpp \
                 # ... keep existing files EXCEPT:
                 # REMOVE: app/crypto/crypto_utils.cpp
```

**Update Objects:**
```makefile
App_C_Objects := $(App_C_Files:.c=.o)
App_Cpp_Objects := $(App_Cpp_Files:.cpp=.o)
App_Objects := $(App_C_Objects) $(App_Cpp_Objects)
```

#### Step 7.3: Update Include Paths

```makefile
App_Include_Paths := -Icommon -Iapp -Iapp/core_logic
```

Remove:
- `-I$(SGX_SDK)/include`
- `-Ienclave/untrusted`

#### Step 7.4: Update Compile Flags

```makefile
App_Compile_CFlags := -fPIC -Wno-attributes $(App_Include_Paths)

ifeq ($(DEBUG), 1)
    App_Compile_CFlags += -DDEBUG -g -O0
else
    App_Compile_CFlags += -DNDEBUG -O2
endif

ifdef SLIM_ENTRY
    App_Compile_CFlags += -DSLIM_ENTRY
endif

App_Compile_CXXFlags := $(App_Compile_CFlags) -std=c++17
```

Remove all SGX-specific flags.

#### Step 7.5: Update Link Flags

```makefile
App_Link_Flags := -lpthread
```

Remove:
- `-L$(SGX_LIBRARY_PATH)`
- `-lsgx_urts`
- `-lsgx_uae_service`

#### Step 7.6: Remove Enclave Build Rules

**Delete entire section (lines 267-318):**
```makefile
######## Enclave Build ########
# ... all enclave compilation and signing rules
```

#### Step 7.7: Remove EDL Processing Rules

**Delete:**
```makefile
$(Gen_Untrusted_Source): $(SGX_EDGER8R) enclave/trusted/Enclave.edl
    # ... edger8r rules
```

#### Step 7.8: Update Main Target

```makefile
.PHONY: all build clean

all: $(App_Name)
    @echo "Build complete!"

build: all
```

Remove enclave.signed.so from dependencies.

#### Step 7.9: Add Compilation Rules for C Files

```makefile
app/core_logic/%.o: app/core_logic/%.c
    @$(CC) $(App_Compile_CFlags) -c $< -o $@
    @echo "CC   <=  $<"

app/core_logic/algorithms/%.o: app/core_logic/algorithms/%.c
    @$(CC) $(App_Compile_CFlags) -c $< -o $@
    @echo "CC   <=  $<"

app/core_logic/operations/%.o: app/core_logic/operations/%.c
    @$(CC) $(App_Compile_CFlags) -c $< -o $@
    @echo "CC   <=  $<"

app/core_logic/batch/%.o: app/core_logic/batch/%.c
    @$(CC) $(App_Compile_CFlags) -c $< -o $@
    @echo "CC   <=  $<"
```

#### Step 7.10: Update Test Program Settings

```makefile
Test_Include_Paths := -I. -Icommon -Iapp -Iapp/core_logic
Test_Compile_CFlags := -fPIC -Wno-attributes $(Test_Include_Paths)
Test_Compile_CXXFlags := $(Test_Compile_CFlags) -std=c++17

# Remove from Test_Common_Objects:
# app/crypto/crypto_utils.o
# $(Gen_Untrusted_Object)

Test_Common_Objects := app/file_io/converters.o \
                       app/file_io/table_io.o \
                       app/data_structures/entry.o \
                       app/data_structures/table.o \
                       app/join/join_condition.o \
                       app/join/join_attribute_setter.o \
                       app/algorithms/merge_sort_manager.o \
                       app/algorithms/shuffle_manager.o \
                       app/batch/ecall_batch_collector.o \
                       app/batch/ecall_wrapper.o \
                       app/debug/debug_util.o \
                       app/debug/debug_manager.o \
                       $(App_C_Objects)
```

#### Step 7.11: Remove encrypt_tables Tool

```makefile
# DELETE:
# Encrypt_Tool := encrypt_tables
# Encrypt_Tool_Objects := ...
# $(Encrypt_Tool): ...
```

---

### Phase 8: Update Main Application (1 hour)

#### Step 8.1: Remove Enclave Initialization

**File: main/sgx_join/main.cpp**

**Delete:**
```cpp
sgx_enclave_id_t global_eid = 0;  // line 19

int initialize_enclave() {        // lines 22-34
    // DELETE ENTIRE FUNCTION
}

void destroy_enclave() {           // lines 37-42
    // DELETE ENTIRE FUNCTION
}
```

**Remove includes:**
```cpp
#include "sgx_urts.h"    // DELETE
#include "Enclave_u.h"   // DELETE
```

#### Step 8.2: Update main() Function

**Current structure:**
```cpp
int main(int argc, char* argv[]) {
    // Initialize enclave
    if (initialize_enclave() < 0) {
        return -1;
    }

    // Load tables
    // Run join
    // Save output

    // Destroy enclave
    destroy_enclave();
}
```

**New structure:**
```cpp
int main(int argc, char* argv[]) {
    // Load tables (plaintext)
    // Run join (direct function calls)
    // Save output (plaintext)
    return 0;
}
```

Remove all enclave initialization/cleanup calls.

#### Step 8.3: Update Error Handling

Remove SGX status checking:
```cpp
// OLD:
sgx_status_t status = ecall_something(global_eid, ...);
if (status != SGX_SUCCESS) { ... }

// NEW:
something(...);  // Just call directly
```

#### Step 8.4: Remove global_eid Usage

Search for `global_eid` throughout main.cpp and remove all references.

---

### Phase 9: Update Tests (1.5 hours)

#### Step 9.1: Update test_join

**File: tests/integration/test_join.cpp**

Remove:
- Enclave initialization
- Encryption/decryption calls
- `global_eid` usage

Update:
- Load plaintext data directly
- Compare plaintext outputs

#### Step 9.2: Update sqlite_baseline

**File: tests/baseline/sqlite_baseline.cpp**

Currently:
1. Decrypts input
2. Runs SQL
3. Re-encrypts output

New:
1. Loads plaintext input
2. Runs SQL
3. Saves plaintext output

Remove encryption steps.

#### Step 9.3: Delete Crypto Tests

```bash
rm tests/unit/test_aes_crypto.cpp
rm tests/unit/test_encryption.cpp
rm tests/unit/test_encryption_comprehensive.cpp
rm tests/unit/test_encryption_standalone.c
rm tests/unit/test_secure_crypto.cpp
rm tests/unit/verify_encryption.cpp
```

Update Makefile to remove these from test targets.

#### Step 9.4: Update Other Unit Tests

For tests that remain:
- `test_merge_sort.cpp`
- `test_waksman_shuffle.cpp`
- `test_waksman_distribution.cpp`
- `test_shuffle_manager.cpp`
- `test_comparators.cpp`
- `test_window.cpp`

Update each to:
- Remove `sgx_urts.h` include
- Remove enclave initialization
- Use plaintext data

#### Step 9.5: Update Test Scripts

**File: scripts/run_tpch_tests.sh**

Change data directory:
```bash
# OLD:
INPUT_DIR="input/encrypted/data_${SCALE}"

# NEW:
INPUT_DIR="input/plaintext/data_${SCALE}"
```

#### Step 9.6: Update Test Makefile Targets

Remove:
```makefile
test_aes_crypto: ...
test_encryption: ...
```

Update:
```makefile
tests: test_join sqlite_baseline test_merge_sort test_waksman_shuffle \
       test_waksman_distribution test_shuffle_manager test_comparators
```

---

### Phase 10: Build & Debug (1.5 hours)

#### Step 10.1: Clean Build

```bash
make clean
rm -f app/**/*.o
rm -f enclave/**/*.o
```

#### Step 10.2: Initial Compilation Attempt

```bash
make 2>&1 | tee build_errors.log
```

Expected errors:
- Missing includes
- Undefined references
- Type mismatches

#### Step 10.3: Fix Include Errors

Common issues:
```cpp
// Error: sgx_status_t not defined
// Fix: Remove SGX status types, use int or void

// Error: entry_t has no member 'nonce'
// Fix: Already removed in Phase 2

// Error: cannot find crypto_utils.h
// Fix: Remove include
```

Go through build_errors.log systematically.

#### Step 10.4: Fix Linker Errors

Common issues:
```
undefined reference to `batch_dispatcher'
```

**Fix:** Ensure C functions are declared with `extern "C"` in headers:

**File: app/core_logic/batch/batch_dispatcher.h**
```c
#ifdef __cplusplus
extern "C" {
#endif

void batch_dispatcher(entry_t* data_array, size_t data_count,
                     void* ops_array, size_t ops_count,
                     size_t ops_size, int32_t op_type);

#ifdef __cplusplus
}
#endif
```

#### Step 10.5: Fix Missing Function Definitions

If functions are called but not defined:
- Check they're in the right .c file
- Check .c file is in Makefile
- Check function isn't `static`

#### Step 10.6: Verify Successful Build

```bash
make
```

Should produce:
- `sgx_app` executable
- No errors, no warnings

```bash
ls -lh sgx_app
file sgx_app
ldd sgx_app
```

---

### Phase 11: Testing & Validation (2 hours)

#### Step 11.1: Test Compilation

```bash
make tests 2>&1 | tee test_build.log
```

Verify all test binaries compile.

#### Step 11.2: Run Unit Tests

```bash
./test_merge_sort
echo "Exit code: $?"
```

Expected: Exit code 0, correct output

```bash
./test_waksman_shuffle
./test_waksman_distribution
./test_shuffle_manager
./test_comparators
```

Document any failures.

#### Step 11.3: Run Integration Tests - Small Scale

```bash
./test_join input/queries/tpch_tb1.sql input/plaintext/data_0_001
```

Expected:
- Loads plaintext data
- Runs join
- Compares with SQLite baseline
- Reports SUCCESS

#### Step 11.4: Run All TPC-H Queries - Scale 0.001

```bash
for query in input/queries/tpch_tb*.sql; do
    echo "Testing: $query"
    ./test_join $query input/plaintext/data_0_001
    if [ $? -ne 0 ]; then
        echo "FAILED: $query"
    fi
done
```

All should pass.

#### Step 11.5: Run All TPC-H Queries - Scale 0.01

```bash
for query in input/queries/tpch_tb*.sql; do
    echo "Testing: $query"
    ./test_join $query input/plaintext/data_0_01
    if [ $? -ne 0 ]; then
        echo "FAILED: $query"
    fi
done
```

All should pass.

#### Step 11.6: Test Main Application

```bash
./sgx_app input/queries/tpch_tb1.sql input/plaintext/data_0_001 output.csv
```

Verify:
- output.csv is created
- Contains correct number of rows
- Values look correct

```bash
head -20 output.csv
wc -l output.csv
```

#### Step 11.7: Baseline Comparison

```bash
./sqlite_baseline input/queries/tpch_tb1.sql input/plaintext/data_0_001 baseline.csv
./sgx_app input/queries/tpch_tb1.sql input/plaintext/data_0_001 output.csv

diff output.csv baseline.csv
```

Should be identical (or only differ in row order if query doesn't specify ORDER BY).

#### Step 11.8: Performance Test

```bash
time ./sgx_app input/queries/tpch_tb1.sql input/plaintext/data_0_001 output.csv
```

Document execution time. Should be faster than SGX version (no ecall overhead).

---

### Phase 12: Documentation (1 hour)

#### Step 12.1: Update CLAUDE.md

**Changes needed:**

**Section: System Architecture**
```markdown
### Core Design
- **Oblivious Multi-Way Join**: Implements data-oblivious join algorithms
- **TDX VM**: Secure execution inside Intel TDX for confidentiality
- **Unified Codebase**: All code runs in trusted VM (no enclave boundary)
- **Plaintext I/O**: Data read/written as plaintext (TDX encrypts VM)
```

**Section: Build System**
```markdown
## Prerequisites
- Intel CPU with TDX support (or any Linux for testing)
- Ubuntu 20.04 or later
- GCC 9+ with C++17 support

## Build Commands
```bash
# Standard build
make clean && make

# Debug build
DEBUG=1 make

# Build test programs
make tests
```

**Remove:**
- All SGX SDK installation instructions
- All references to enclave.signed.so
- All encryption/decryption documentation

**Section: Usage Guide**
```markdown
### Running Joins
```bash
# Run a join query on plaintext data
./sgx_app <query.sql> <data_dir> <output.csv>

# Example
./sgx_app input/queries/tpch_tb1.sql input/plaintext/data_0_001 output.csv
```

# Data is plaintext CSV - TDX encrypts at VM level
```

**Remove:**
- encrypt_tables documentation
- Encrypted data format documentation

#### Step 12.2: Update README.md

**Update title:**
```markdown
# Oblivious Multi-Way Join (TDX-Compatible)
```

**Update overview:**
```markdown
This project implements memory-efficient oblivious multi-way joins
designed to run in Intel TDX (Trust Domain Extensions) environments.
TDX provides VM-level encryption and isolation, allowing us to work
with plaintext data while maintaining confidentiality.
```

**Update prerequisites:**
```markdown
## Prerequisites
- Intel TDX-capable system (or any Linux for development)
- GCC 9+ with C++17 support
- Standard C/C++ libraries
- SQLite3 (for baseline testing)
```

Remove:
- Intel SGX SDK requirements
- Enclave configuration instructions

**Update build:**
```markdown
## Building
```bash
make clean && make
```

This produces:
- `sgx_app` - Main join application
- `test_join` - Integration test tool
- `sqlite_baseline` - Baseline reference implementation
```

**Update usage:**
```markdown
## Usage
```bash
./sgx_app <query_file> <data_directory> <output_file>
```

Example:
```bash
./sgx_app input/queries/tpch_tb1.sql input/plaintext/data_0_001 output.csv
```
```

#### Step 12.3: Create TDX_MIGRATION.md

New file documenting the migration:

```markdown
# SGX to TDX Migration Documentation

## Overview
This document describes the migration from Intel SGX to Intel TDX architecture completed on [DATE].

## Why Migrate?
- **TDX provides VM-level encryption** - application-level encryption redundant
- **Simpler architecture** - no enclave boundary, no ecalls/ocalls
- **Better performance** - direct function calls, no context switching
- **Easier maintenance** - unified codebase, standard C++ compilation

## What Changed

### Removed Components
1. **All encryption code**
   - `app/crypto/crypto_utils.cpp` - deleted
   - `enclave/trusted/crypto/*` - deleted
   - `main/tools/encrypt_tables.cpp` - deleted

2. **Enclave boundary**
   - `enclave/trusted/Enclave.edl` - deleted
   - All ecall/ocall infrastructure - deleted
   - Enclave initialization/signing - deleted

3. **SGX SDK dependencies**
   - No longer requires SGX SDK installation
   - Removed all SGX libraries
   - Removed enclave build system

4. **Entry encryption fields**
   - `is_encrypted` field - removed from entry_t
   - `nonce` field - removed from entry_t

### Kept Components (Unchanged)
- All oblivious join algorithms
- Memory access patterns (still data-oblivious)
- Entry metadata structure
- Join tree construction
- Query parsing
- Test infrastructure

### Modified Components
1. **I/O Layer**
   - Reads/writes plaintext CSV files
   - No encryption/decryption
   - Simpler, faster

2. **Build System**
   - Standard C++ compilation
   - No enclave signing
   - Faster builds

3. **Function Calls**
   - Direct calls instead of ecalls
   - Better performance
   - Simpler debugging

## Performance Impact
- **Ecall elimination**: 10-50x improvement on small operations
- **Direct function calls**: No context switching overhead
- **Simpler code paths**: Better compiler optimization

## Compatibility
- **Test outputs**: Identical results to SGX version
- **Data format**: Plaintext CSV (more portable)
- **Query language**: No changes
- **Algorithms**: Exact same oblivious behavior

## Migration Statistics
- **Files deleted**: ~15 (crypto, ecall infrastructure)
- **Files modified**: ~30 (remove encryption, ecalls)
- **Lines of code removed**: ~2000
- **Build time**: Reduced by ~50%
- **Binary size**: Reduced (no enclave blob)

## Testing Validation
All TPC-H queries tested on both scales (0.001, 0.01):
- ✅ All unit tests pass
- ✅ All integration tests pass
- ✅ Outputs match SQLite baseline
- ✅ Performance improved

## Future Work
- Optimize batch sizes for TDX memory limits
- Add TDX attestation support
- Benchmark on larger datasets (scale 0.1, 1.0)
```

#### Step 12.4: Update PATHS.md (if exists)

Update any file paths referenced in documentation.

---

## Risk Mitigation

### Backup Strategy
```bash
git checkout -b sgx-backup
git checkout -b tdx-migration
```

Keep SGX version in separate branch for reference.

### Rollback Plan
If migration fails:
```bash
git checkout sgx-backup
```

### Validation Checkpoints
After each phase:
1. Commit changes
2. Document what was done
3. Test compilation
4. Run basic tests

### Common Issues & Solutions

**Issue: Compilation errors after removing encryption fields**
- **Cause**: Code still references is_encrypted or nonce
- **Solution**: Search for all references, update or remove

**Issue: Linker errors for batch_dispatcher**
- **Cause**: C/C++ linkage mismatch
- **Solution**: Add `extern "C"` declarations

**Issue: Tests fail with different results**
- **Cause**: Oblivious algorithm changed accidentally
- **Solution**: Compare with SGX version, verify algorithms unchanged

**Issue: Segfault in oblivious_waksman**
- **Cause**: Random number generation changed
- **Solution**: Verify RNG produces valid switch bits

---

## Success Criteria

✅ **Build**: `make clean && make` completes without errors
✅ **Tests**: All unit tests pass
✅ **Integration**: All TPC-H queries pass
✅ **Baseline**: Outputs match SQLite baseline
✅ **Performance**: Faster than SGX version
✅ **Dependencies**: No SGX SDK required
✅ **Documentation**: All docs updated and accurate
✅ **Code Quality**: No warnings, clean compilation

---

## Timeline Estimate

| Phase | Time | Cumulative |
|-------|------|------------|
| 1. Preparation | 1 hour | 1 hour |
| 2. Remove encryption fields | 1 hour | 2 hours |
| 3. Merge enclave code | 2 hours | 4 hours |
| 4. Replace ecalls | 2 hours | 6 hours |
| 5. Remove encryption | 2 hours | 8 hours |
| 6. Simplify I/O | 1.5 hours | 9.5 hours |
| 7. Update build | 1.5 hours | 11 hours |
| 8. Update main | 1 hour | 12 hours |
| 9. Update tests | 1.5 hours | 13.5 hours |
| 10. Build & debug | 1.5 hours | 15 hours |
| 11. Testing | 2 hours | 17 hours |
| 12. Documentation | 1 hour | 18 hours |

**Total: 14-18 hours** (assuming no major blockers)

---

## Post-Migration Tasks

1. **Performance benchmarking** on TDX hardware
2. **Add TDX attestation** (if needed)
3. **Optimize batch sizes** for TDX memory
4. **Test with larger datasets** (scale 0.1, 1.0)
5. **Update CI/CD** to remove SGX dependencies
6. **Archive SGX version** for reference

---

## Contact & Support

For questions during migration:
1. Check this plan
2. Review git history of SGX version
3. Test incrementally
4. Document any deviations

Good luck! 🚀
