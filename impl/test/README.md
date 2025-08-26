# System-Level Tests (/impl/test/)

This directory contains **all high-level tests** for the complete system.

## Purpose
- Complete system testing from external perspective
- Performance benchmarks
- Test data and resources

## Directory Structure
- `data/` - Test datasets, CSV files, sample inputs, expected outputs
- `integration/` - Integration tests (full workflow, multi-component tests)
- `lib/` - Test utilities, helpers, common test infrastructure

## Note
Unit tests are located in `/impl/src/unit_test/` near the source code for better organization.

## When to Add Tests Here
- Testing complete SGX app with real queries
- End-to-end workflow tests
- Performance/memory benchmarks  
- Tests requiring test data files
- System-level regression tests

## Examples (To Be Implemented)
- Full band join algorithm tests with test queries
- Performance regression tests
- Memory usage tests
- SGX attestation tests
- Multi-threaded operation tests

## Running Tests
```bash
# To be implemented
make integration_test
./run_all_tests.sh
```