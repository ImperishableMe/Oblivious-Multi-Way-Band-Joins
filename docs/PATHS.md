# Important Absolute Paths for the Project

## Main Algorithm Implementation
- **Main SGX Application**: `/home/r33wei/omwj/memory_const_public/impl/src/sgx_app`
- **Encryption Tool**: `/home/r33wei/omwj/memory_const_public/impl/src/encrypt_tables`
- **Source Code Directory**: `/home/r33wei/omwj/memory_const_public/impl/src/`
- **Algorithm Implementations**: `/home/r33wei/omwj/memory_const_public/impl/src/app/algorithms/`
  - Bottom-up Phase: `/home/r33wei/omwj/memory_const_public/impl/src/app/algorithms/bottom_up_phase.cpp`
  - Top-down Phase: `/home/r33wei/omwj/memory_const_public/impl/src/app/algorithms/top_down_phase.cpp`
  - Distribute-Expand: `/home/r33wei/omwj/memory_const_public/impl/src/app/algorithms/distribute_expand.cpp`
  - Align-Concat: `/home/r33wei/omwj/memory_const_public/impl/src/app/algorithms/align_concat.cpp`
  - Oblivious Join (main): `/home/r33wei/omwj/memory_const_public/impl/src/app/algorithms/oblivious_join.cpp`

## Test Infrastructure
- **Test Join Comparator**: `/home/r33wei/omwj/memory_const_public/impl/src/test/test_join`
- **SQLite Baseline**: `/home/r33wei/omwj/memory_const_public/impl/src/test/sqlite_baseline`
- **Test Runner Script**: `/home/r33wei/omwj/memory_const_public/impl/src/run_tpch_tests.sh`

## Test Cases
- **Test Case Directory**: `/home/r33wei/omwj/memory_const_public/test_cases/`
- **Test Queries**: `/home/r33wei/omwj/memory_const_public/test_cases/queries/`
  - Two-center chain: `/home/r33wei/omwj/memory_const_public/test_cases/queries/two_center_chain.sql`
  - Three-table chain: `/home/r33wei/omwj/memory_const_public/test_cases/queries/three_table_chain.sql`
  - Two-table basic: `/home/r33wei/omwj/memory_const_public/test_cases/queries/two_table_basic.sql`
- **Test Data**: 
  - Encrypted: `/home/r33wei/omwj/memory_const_public/test_cases/encrypted/`
  - Plaintext: `/home/r33wei/omwj/memory_const_public/test_cases/plaintext/`

## TPC-H Data and Queries
- **TPC-H Queries**: `/home/r33wei/omwj/memory_const_public/input/queries/`
  - TB1 (Two-table equality): `/home/r33wei/omwj/memory_const_public/input/queries/tpch_tb1.sql`
  - TB2 (Two-table inequality): `/home/r33wei/omwj/memory_const_public/input/queries/tpch_tb2.sql`
  - TM1 (Three-table): `/home/r33wei/omwj/memory_const_public/input/queries/tpch_tm1.sql`
- **TPC-H Data**:
  - Encrypted Data: `/home/r33wei/omwj/memory_const_public/input/encrypted/`
    - Scale 0.001: `/home/r33wei/omwj/memory_const_public/input/encrypted/data_0_001/`
    - Scale 0.01: `/home/r33wei/omwj/memory_const_public/input/encrypted/data_0_01/`
    - Scale 0.1: `/home/r33wei/omwj/memory_const_public/input/encrypted/data_0_1/`
    - Scale 1.0: `/home/r33wei/omwj/memory_const_public/input/encrypted/data_1/`
  - Plaintext Data: `/home/r33wei/omwj/memory_const_public/input/plaintext/`
    - Scale 0.001: `/home/r33wei/omwj/memory_const_public/input/plaintext/data_0_001/`
    - Scale 0.01: `/home/r33wei/omwj/memory_const_public/input/plaintext/data_0_01/`
    - Scale 0.1: `/home/r33wei/omwj/memory_const_public/input/plaintext/data_0_1/`
    - Scale 1.0: `/home/r33wei/omwj/memory_const_public/input/plaintext/data_1/`

## Debug and Output
- **Debug Sessions**: `/home/r33wei/omwj/memory_const_public/debug/`
- **Output Directory**: `/home/r33wei/omwj/memory_const_public/output/`
- **Debug Utilities**: `/home/r33wei/omwj/memory_const_public/impl/src/common/debug_util.h`
- **Debug Implementation**: `/home/r33wei/omwj/memory_const_public/impl/src/app/debug_util.cpp`

## Build System
- **Makefile**: `/home/r33wei/omwj/memory_const_public/impl/src/Makefile`
- **Build Script**: `/home/r33wei/omwj/memory_const_public/impl/build.sh` (Note: Currently empty, use `make` directly)

## Usage Examples

**Important**: All commands should be run from `/home/r33wei/omwj/memory_const_public/impl/src/`

```bash
# Change to the source directory first
cd /home/r33wei/omwj/memory_const_public/impl/src

# Run the two-center test case
./test/test_join /home/r33wei/omwj/memory_const_public/test_cases/queries/two_center_chain.sql /home/r33wei/omwj/memory_const_public/test_cases/encrypted

# Run TPC-H TB1 test (two-table equality join)
./test/test_join /home/r33wei/omwj/memory_const_public/input/queries/tpch_tb1.sql /home/r33wei/omwj/memory_const_public/input/encrypted/data_0_001

# Run TPC-H TB2 test (two-table inequality join)
./test/test_join /home/r33wei/omwj/memory_const_public/input/queries/tpch_tb2.sql /home/r33wei/omwj/memory_const_public/input/encrypted/data_0_001

# Run SGX application directly (3 arguments required)
./sgx_app <sql_file> <encrypted_data_dir> <output_file>
# Example:
./sgx_app /home/r33wei/omwj/memory_const_public/input/queries/tpch_tb1.sql /home/r33wei/omwj/memory_const_public/input/encrypted/data_0_001 output.csv

# Run SQLite baseline (3 arguments required)
./test/sqlite_baseline <sql_file> <plaintext_data_dir> <output_file>
# Example:
./test/sqlite_baseline /home/r33wei/omwj/memory_const_public/input/queries/tpch_tb1.sql /home/r33wei/omwj/memory_const_public/input/plaintext/data_0_001 baseline.csv

# Run all TPC-H tests automatically
./run_tpch_tests.sh

# Compile the project
make

# Clean and rebuild
make clean && make

# Enable debug output (compile with debug flag)
DEBUG=1 make
```

## Command Usage Reference

- **test_join**: `./test/test_join <sql_file> <data_dir>`
  - Compares SGX results with SQLite baseline
  - Takes only 2 arguments (no output file parameter)
  - Automatically runs both SGX and SQLite and compares results

- **sgx_app**: `./sgx_app <query_file> <input_dir> <output_file>`
  - Runs the oblivious join algorithm in SGX enclave
  - Requires 3 arguments including output file

- **sqlite_baseline**: `./test/sqlite_baseline <sql_file> <input_dir> <output_file>`
  - Runs standard SQLite for comparison
  - Requires 3 arguments including output file

- **run_tpch_tests.sh**: `./run_tpch_tests.sh`
  - Automatically runs all TPC-H queries in the queries directory
  - No arguments needed (uses hardcoded paths)