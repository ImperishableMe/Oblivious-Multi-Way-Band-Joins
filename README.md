# Oblivious Multi-Way Join with Constant Memory Overhead

An SGX-based implementation of oblivious multi-way join algorithms that maintain constant memory overhead while ensuring data-oblivious execution patterns for secure computation on untrusted cloud platforms.

## Overview

This project implements an oblivious multi-way join system that:
- Executes SQL joins inside Intel SGX enclaves for confidentiality
- Maintains data-oblivious memory access patterns to prevent side-channel leaks
- Achieves constant memory overhead regardless of join selectivity
- Supports arbitrary multi-way joins with complex predicates

Based on the research paper: "Oblivious Multi-Way Joins with Constant Memory Overhead" (to appear)

## Features

- **Secure Execution**: Runs inside Intel SGX enclaves to protect data confidentiality
- **Oblivious Algorithms**: Memory access patterns are independent of data values
- **Constant Memory**: Uses only O(N) memory for N input tuples
- **Multi-Way Joins**: Supports chains, stars, cycles, and cliques
- **Batch Processing**: Optimized ecall batching to reduce SGX overhead
- **Encrypted I/O**: Supports AES-encrypted input/output data

## Prerequisites

- Intel CPU with SGX support
- Ubuntu 20.04 or later
- Intel SGX SDK and PSW
- GCC 9+ with C++17 support
- Make build system

## Building

```bash
make clean
make
```

For debug builds:
```bash
DEBUG=1 make
```

## Usage

### Basic Join Execution

```bash
# Run a join query on encrypted data
./sgx_app <query.sql> <encrypted_data_dir> <output.csv>

# Example with included test data
./sgx_app input/queries/tpch_tb1.sql input/encrypted/data_0_001 output.csv
```

### Data Encryption

```bash
# Encrypt plaintext CSV files for use with SGX
./encrypt_tables <plaintext_dir> <encrypted_output_dir>

# Example
./encrypt_tables input/plaintext/data_0_001 /tmp/encrypted_data
```

### Test Execution

```bash
# Run comparison tests against SQLite baseline
./tests/integration/test_join input/queries/tpch_tb1.sql input/encrypted/data_0_001
```

## SQL Query Format

Queries should be standard SQL SELECT statements with joins:

```sql
-- Two-way join
SELECT *
FROM supplier1, supplier2
WHERE supplier1.S1_S_ACCTBAL < supplier2.S2_S_ACCTBAL;

-- Three-way join
SELECT *
FROM T1, T2, T3
WHERE T1.attr = T2.attr AND T2.attr = T3.attr;
```

## Data Format

Input data should be CSV files with:
- First row containing column names
- Integer values only (system limitation)
- Values within range [-1073741820, 1073741820]

## Project Structure

```
impl/src/
├── app/                 # Main application code
│   ├── algorithms/      # Join algorithms
│   ├── batch/          # Ecall batching system
│   ├── crypto/         # Encryption utilities
│   └── data_structures/ # Core data structures
├── enclave/            # SGX enclave code
├── common/             # Shared utilities
└── test/               # Test suite
```

## Performance Optimizations

- **Ecall Batching**: Reduces SGX transition overhead by batching operations
- **Memory Pool**: Pre-allocated memory to avoid dynamic allocation
- **Vectorized Operations**: SIMD optimizations where applicable

## Security Considerations

- All data processing occurs inside SGX enclaves
- Memory access patterns are data-independent (oblivious)
- Input/output data is AES-encrypted
- No sensitive data in debug outputs

## Testing

The project includes comprehensive tests:

```bash
# Unit tests
cd impl/src/test
make
./test_join <query> <data>

# Performance benchmarks
./overhead_measurement
```

## Sample Data

Small TPC-H datasets are included for testing:
- `input/plaintext/data_0_001/` - Scale factor 0.001 (plaintext)
- `input/encrypted/data_0_001/` - Scale factor 0.001 (encrypted)
- `input/plaintext/data_0_01/` - Scale factor 0.01 (plaintext)
- `input/encrypted/data_0_01/` - Scale factor 0.01 (encrypted)

## Generating Account Datasets

The project includes a parameterized generator for creating banking datasets of varying sizes:

```bash
# Basic usage
python3 scripts/generate_banking_scaled.py <num_accounts> <output_dir>

# Examples with different sizes
python3 scripts/generate_banking_scaled.py 1000 input/plaintext/banking_1000
python3 scripts/generate_banking_scaled.py 5000 input/plaintext/banking_5000
python3 scripts/generate_banking_scaled.py 10000 input/plaintext/banking_10000
python3 scripts/generate_banking_scaled.py 50000 input/plaintext/banking_50000
```

### Custom Seeds for Reproducibility

Use the `--seed` option to generate reproducible datasets:

```bash
# Same seed produces identical data
python3 scripts/generate_banking_scaled.py 5000 output1 --seed 12345
python3 scripts/generate_banking_scaled.py 5000 output2 --seed 12345  # Identical to output1

# Different seeds produce different data
python3 scripts/generate_banking_scaled.py 5000 output3 --seed 99999  # Different data
```

Default seed is `42 + num_accounts` (e.g., 5042 for 5000 accounts).

### Generated Tables

| Table | Rows | Columns |
|-------|------|---------|
| owner.csv | num_accounts / 5 | ow_id, name_placeholder |
| account.csv | num_accounts | account_id, balance, owner_id |
| txn.csv | 5 × num_accounts | acc_from, acc_to, amount, txn_time |

### Pre-generated Banking Datasets

- `input/plaintext/banking/` - Default dataset
- `input/plaintext/banking_1000/` - 1,000 accounts
- `input/plaintext/banking_2000/` - 2,000 accounts
- `input/plaintext/banking_5000/` - 5,000 accounts
- `input/plaintext/banking_10000/` - 10,000 accounts
- `input/plaintext/banking_20000/` - 20,000 accounts
- `input/plaintext/banking_50000/` - 50,000 accounts

## Limitations

- Currently supports only integer data types
- Maximum value range: [-1073741820, 1073741820]
- Requires Intel SGX hardware for secure execution

## License

[MIT License](LICENSE)

## Citation

If you use this code in your research, please cite:

```bibtex
@inproceedings{oblivious-join-2024,
  title={Oblivious Multi-Way Joins with Constant Memory Overhead},
  author={[Authors]},
  booktitle={[Conference]},
  year={2024}
}
```

## Contact

For questions or issues, please open a GitHub issue or contact the authors.