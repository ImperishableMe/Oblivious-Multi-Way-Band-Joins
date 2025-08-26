# Source-Level Unit Tests (/impl/src/unit_test/)

This directory contains **unit tests** for individual source components.

## Purpose
- Test individual functions and modules in isolation
- Test internal implementation details
- Fast, focused tests that don't require full system setup
- Tests that need direct access to source code internals

## Current Tests
- `test_encryption.cpp` - SGX encryption/decryption functionality tests
- `test_encryption_standalone.c` - Non-SGX encryption logic tests

## When to Add Tests Here
- Testing a specific module (crypto, converters, etc.)
- Testing internal functions not exposed through public APIs
- Tests that need to be compiled with the source code
- Fast unit tests that should run frequently during development

## Running Tests
```bash
make test_encryption
./test_encryption
```