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
cd impl/src
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
cd impl/src
./test/test_join input/queries/tpch_tb1.sql input/encrypted/data_0_001
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