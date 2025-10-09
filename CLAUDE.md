# Claude AI Assistant Instructions - Oblivious Multi-Way Join Project

This document contains comprehensive instructions and context for AI assistants working with this codebase.

## ⚠️ IMPORTANT: TDX MIGRATION COMPLETED (October 2025)

**This codebase has been migrated from Intel SGX to Intel TDX architecture.**

Key changes:
- ❌ **No application-level encryption** - TDX encrypts entire VM
- ❌ **No enclave boundary** - Direct function calls instead of ecalls
- ✅ **Oblivious algorithms preserved** - All security properties maintained
- ✅ **Simplified architecture** - No crypto layer, unified codebase

See `TDX_MIGRATION_SUMMARY.md` for complete migration details.

# ============== CRITICAL RULES (MUST FOLLOW) ==============

## Code Modification Rules
- **NEVER modify code with scripts** - Always edit code manually using the Edit tool. No sed, awk, perl, or any script-based modifications.
- **NO temporary fixes or workarounds** - All issues must be addressed with proper, permanent solutions.
- **ALL compiler warnings must be fixed** - Treat warnings as errors. No code should be committed with warnings.

## Command Execution Rules
- **NEVER use pipes (|) in ANY command**:
  - NO pipes with make commands (no `make | grep`)
  - NO pipes when running tests (no `./test | head`)
  - NO grep, head, tail, sed, awk, or any other pipe operations
  - This is REQUIRED to avoid permission prompts and ensure all output is captured

## Testing Rules
- **NO PIPELINES IN TESTS**: Run test commands WITHOUT pipelines for proper debugging
- **All tests must go to test folder**, isolated from the main implementation

## Compilation Rules
- **ALWAYS compile using separate commands from the project root**:
  - Main code: `make`
  - Test utilities: `make tests`
- **NEVER use combined commands** like `cd test && make && cd ..` 
- **Use absolute paths** when referencing files outside current directory

# ============== PROJECT OVERVIEW ==============

## System Architecture

### Core Design
- **Oblivious Multi-Way Join**: Implements data-oblivious join algorithms with constant memory overhead
- **TDX VM Protection**: Secure execution inside Intel TDX trusted VM (migrated from SGX)
- **Unified Processing**:
  - Single codebase - no enclave boundary
  - Direct function calls - no ecalls/ocalls
  - VM-level encryption - no application crypto
- **Memory Access Patterns**: All access patterns are data-independent to prevent side-channel attacks

### TDX Migration (October 2025)
- **Architecture**: Moved from SGX enclaves to TDX trusted VMs
- **Encryption**: Removed application-level encryption (TDX handles transparently)
- **Code Organization**: Merged `enclave/trusted/` → `app/core_logic/`
- **Performance**: Eliminated ecall overhead, faster execution
- **Security**: Maintained data-oblivious properties, VM-level protection

### Key Components
1. **Join Algorithms** (`app/algorithms/`):
   - Bottom-up phase: Builds join tree from leaves
   - Top-down phase: Propagates results down the tree
   - Distribute-Expand: Core oblivious distribution mechanism
   - Align-Concat: Oblivious data alignment and concatenation

2. **Batch Processing System** (`app/batch/`):
   - Reduces SGX ecall overhead by batching operations
   - Deduplicates entries using hash maps
   - Converts Entry pointers to indices for oblivious tracking

3. **Data Structures** (`app/data_structures/`):
   - Entry: Core data structure (fat mode: ~2256 bytes, slim mode: ~260 bytes)
   - Table: Collection of entries with schema
   - JoinAttributeSetter: Manages join attribute extraction

4. **Encryption** (`app/crypto/`):
   - AES-based encryption for data at rest
   - Secure key management inside enclave

## System Data Bounds and Constraints

**Core Design Principle:**
"We use int32_t throughout our system for simplicity. For attributes we define the bounds to [-1,073,741,820, 1,073,741,820], and we define -INF and INF to be -1,073,741,821 and 1,073,741,821 to handle join_attr±INF without overflow."

### Defined Constants (from enclave_types.h):
- **Valid Attribute Range**: `[JOIN_ATTR_MIN, JOIN_ATTR_MAX]` = `[-1,073,741,820, 1,073,741,820]`
- **Negative Infinity**: `JOIN_ATTR_NEG_INF = -1,073,741,821`
- **Positive Infinity**: `JOIN_ATTR_POS_INF = 1,073,741,821`
- **NULL Value**: `NULL_VALUE = INT32_MAX = 2,147,483,647`

### Important Implications:
- **ALL data values must be integers** within the valid range
- String values in CSV files will be parsed as 0 (with warnings)
- The system cannot handle actual string data
- Test data must be prepared with these bounds in mind

# ============== BUILD SYSTEM ==============

## Prerequisites
- Intel CPU with SGX support
- Ubuntu 20.04 or later
- Intel SGX SDK and PSW
- GCC 9+ with C++17 support

## Build Commands

```bash
# Standard build (TDX - no SGX SDK needed)
make clean && make

# Debug build (enables debug output to files)
DEBUG=1 make

# Slim entry mode (reduces memory overhead)
make SLIM_ENTRY=1

# Build test programs
make test_join        # Integration test (works)
# Note: sqlite_baseline requires libsqlite3-dev (optional)

# Run the application
./sgx_app <query.sql> <input_dir> <output.csv>
```

### TDX Build Notes
- ✅ No SGX SDK installation required
- ✅ No enclave signing needed
- ✅ Standard GCC/G++ compilation
- ✅ Direct linking of all components

## Build Modes
- **Fat Entry Mode** (default): Full entry structure (~2256 bytes per entry)
- **Slim Entry Mode**: Reduced entry size (~260 bytes) by moving metadata to shared structures
- **Debug Mode**: Enables detailed logging to debug files

**IMPORTANT**: Fat and slim modes are incompatible - data encrypted in one mode cannot be decrypted in the other

# ============== USAGE GUIDE ==============

## Basic Usage

### Running Joins
```bash
# Run a join query on encrypted data
./sgx_app <query.sql> <encrypted_data_dir> <output.csv>

# Example
./sgx_app input/queries/tpch_tb1.sql input/encrypted/data_0_001 output.csv
```

### Data Format (TDX)
**Note**: After TDX migration, no encryption tool is needed. Data files are used directly as CSV.
- Input: Plaintext CSV files
- Output: Plaintext CSV files
- Protection: TDX VM encrypts filesystem transparently

### Testing
```bash
# Compare SGX output with SQLite baseline
./test_join <query.sql> <encrypted_data_dir>

# Run all TPC-H tests
./scripts/run_tpch_tests.sh
```

## SQL Query Format
Standard SQL SELECT statements with joins:
```sql
-- Two-way join
SELECT * FROM T1, T2 WHERE T1.attr < T2.attr;

-- Three-way join
SELECT * FROM T1, T2, T3 
WHERE T1.attr = T2.attr AND T2.attr = T3.attr;
```

## Data Format Requirements
- CSV files with header row containing column names
- Integer values only (system limitation)
- Values must be within [-1,073,741,820, 1,073,741,820]
- Last row should contain sentinel values (-10000)

# ============== TEST INFRASTRUCTURE ==============

## Test Tools

### test_join
- **Purpose**: Compares SGX output with SQLite baseline
- **Usage**: `./test_join <sql_file> <encrypted_data_dir>`
- **Build**: `make test_join` or `make tests` to build all tests
- **Note**: Both SGX and SQLite must be compiled in same mode (fat/slim)

### sqlite_baseline
- **Purpose**: Reference implementation using SQLite
- **Process**: Decrypts input → Runs SQL → Re-encrypts output
- **Usage**: `./sqlite_baseline <sql_file> <encrypted_data_dir> <output_file>`
- **Build**: `make sqlite_baseline` or `make tests` to build all tests

### Performance Tests
- `overhead_measurement`: Measures SGX overhead
- `overhead_crypto_breakdown`: Analyzes encryption costs

## Building and Running Tests

### Build all tests
```bash
make tests
```

### Build individual test programs
```bash
make test_join        # Build comparison test
make sqlite_baseline  # Build SQLite baseline
```

### Run tests
```bash
# Compare SGX with SQLite baseline
./test_join input/queries/tpch_tb1.sql input/encrypted/data_0_001

# Run SQLite baseline directly
./sqlite_baseline input/queries/tpch_tb1.sql input/encrypted/data_0_001 output.csv

# Run SGX join directly
./sgx_app input/queries/tpch_tb1.sql input/encrypted/data_0_001 output.csv

# Run all TPC-H tests with script
./scripts/run_tpch_tests.sh [scale]  # scale: 0_001 (default) or 0_01
```

## Test Data
- Scale 0.001: ~150 rows per table (included)
- Scale 0.01: ~1,500 rows per table (included)
- Scale 0.1: ~15,000 rows per table
- Scale 1.0: ~150,000 rows per table

# ============== DEBUG INFORMATION ==============

## Debug Output
- Debug output goes to files, not console
- Location: `debug/{date}_{time}_{test}/`
- Enable with: `DEBUG=1 make`
- Contains table dumps, operation traces, and execution logs

## Common Debug Scenarios
1. **Join result mismatch**: Check debug dumps for intermediate results
2. **Memory issues**: Enable Valgrind or use operation tracing
3. **Performance issues**: Use overhead measurement tools
4. **Encryption issues**: Verify data format matches compilation mode

# ============== RECENT DEVELOPMENT ==============

## Ecall Reduction (Completed)
- Reduced from 40+ individual ecalls to 4 essential ecalls
- Implemented batch processing system for operation bundling
- Significant performance improvement (>10x for some workloads)

## Memory Access Pattern Verification
- Added operation tracing to verify oblivious behavior
- Can compile with `TRACE_OPS=1` to enable operation logging
- Traces stored as JSON for analysis
- Verified identical operation sequences across different datasets

## Slim Mode Migration (In Progress)
- Goal: Reduce entry size from ~2256 to ~260 bytes
- Status: Core functionality complete, testing ongoing
- Benefits: Reduced memory usage and ecall overhead

# ============== PROJECT STRUCTURE ==============

```
.
├── app/                    # Main application code (non-enclave)
│   ├── algorithms/         # Join algorithm implementations
│   │   ├── oblivious_join.cpp    # Main orchestrator
│   │   ├── bottom_up_phase.cpp   # Build join tree
│   │   ├── top_down_phase.cpp    # Propagate results
│   │   ├── distribute_expand.cpp # Core oblivious operations
│   │   └── align_concat.cpp      # Data alignment
│   ├── batch/              # Ecall batching system
│   ├── core/               # Core data structures (Entry, Table, etc.)
│   ├── crypto/             # Encryption utilities
│   ├── debug/              # Debug utilities
│   ├── io/                 # File I/O operations
│   ├── query/              # SQL query parsing
│   └── utils/              # Helper utilities
├── enclave/                # SGX enclave code
│   ├── trusted/            # Trusted enclave code
│   └── untrusted/          # Generated untrusted edge routines
├── common/                 # Shared headers between app and enclave
│   ├── types_common.h      # Shared type definitions
│   ├── enclave_types.h     # Entry structure definition
│   ├── batch_types.h       # Batch operation types
│   └── debug_util.h        # Debug utilities
├── main/                   # Entry point programs
│   ├── sgx_join/           # Main SGX join application
│   └── tools/              # Standalone tools (encrypt_tables, etc.)
├── tests/                  # Test suite
│   ├── integration/        # Integration tests (test_join)
│   ├── baseline/           # SQLite baseline implementation
│   ├── performance/        # Performance tests
│   └── unit/               # Unit tests (organized by module)
├── scripts/                # Build and test scripts
├── input/                  # Test data
│   ├── queries/            # SQL test queries
│   ├── plaintext/          # Unencrypted test data
│   │   ├── data_0_001/    # Scale 0.001
│   │   └── data_0_01/     # Scale 0.01
│   └── encrypted/          # Encrypted test data
│       ├── data_0_001/    # Scale 0.001
│       └── data_0_01/     # Scale 0.01
└── output/                 # Test outputs and results
```

# ============== KEY ALGORITHMS ==============

## Oblivious Join Algorithm
1. **Input Processing**: Load and encrypt tables
2. **Bottom-Up Phase**: Build join tree from leaf nodes
3. **Distribute-Expand**: Obliviously distribute tuples
4. **Align-Concat**: Align data structures obliviously
5. **Top-Down Phase**: Propagate results to output
6. **Output Generation**: Decrypt and write results

## Batch Processing Flow
1. Operations added to batch collector with Entry pointers
2. Collector deduplicates entries, assigns indices
3. When batch full, flush to enclave
4. Enclave processes batch, returns results
5. Results written back to original Entry objects

## Memory Management
- Pre-allocated pools to avoid dynamic allocation
- Constant memory overhead: O(N) for N input tuples
- No memory allocation during join execution

# ============== PERFORMANCE NOTES ==============

## Optimization Strategies
1. **Batch Size**: Larger batches reduce ecall overhead
2. **Entry Mode**: Slim mode reduces memory transfer
3. **Debug Mode**: Disable for production (10x speedup)
4. **Data Locality**: Keep related data together

## Known Bottlenecks
1. SGX ecall transitions (mitigated by batching)
2. Encryption/decryption overhead
3. Oblivious operations add ~2-3x overhead
4. Memory bandwidth for large datasets

# ============== TROUBLESHOOTING ==============

## Common Issues

### "File not found" errors
- Check PATHS.md for correct file locations
- Use absolute paths when in doubt

### Compilation errors
- Ensure SGX SDK is properly installed
- Check that all dependencies are met
- Verify correct compilation mode (fat/slim)

### Test failures
- Verify data format matches compilation mode
- Check that test data is within valid bounds
- Ensure both tools compiled with same settings

### Performance issues
- Disable debug mode for production
- Increase batch size for large datasets
- Use slim mode for memory-constrained systems

# ============== SECURITY CONSIDERATIONS ==============

## Threat Model
- Untrusted cloud provider with full system access
- Adversary can observe all memory accesses
- Side-channel attacks through access patterns

## Security Properties
1. **Data Confidentiality**: All data encrypted outside enclave
2. **Oblivious Execution**: Access patterns independent of data
3. **Integrity**: SGX attestation ensures code integrity
4. **Constant Time**: Operations take same time regardless of data

## Security Guidelines
- Never log sensitive data values
- Maintain oblivious access patterns
- Use secure random number generation
- Verify all input bounds

# ============== ADDITIONAL NOTES ==============

## Git Branches
- `master`: Stable release
- `efficiency-test-working`: Performance testing branch
- `memory-trace-working`: Memory pattern verification
- `public-release`: Clean version for public sharing

## Contact and Support
- Report issues via GitHub issues
- Include debug logs when reporting problems
- Specify exact commands and data used

## Future Work
- Support for additional data types (strings, floats)
- GPU acceleration for larger datasets
- Distributed execution across multiple nodes
- Dynamic memory management