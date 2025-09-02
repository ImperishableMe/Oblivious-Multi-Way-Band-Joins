# File Structure Reorganization Plan

## Current State
The project currently has a complex nested structure under `impl/src/` that makes navigation and building difficult. This document outlines a future reorganization plan.

## Immediate Fixes Applied
1. Created `scripts/` directory with unified build and test scripts
2. Created `docs/` directory for documentation
3. Scripts now work from any directory using relative paths

## Future Reorganization Plan

### Proposed Structure
```
memory_const_public/
├── src/                    # Core library code
│   ├── core/              # Data structures
│   ├── algorithms/        # Join algorithms
│   ├── crypto/           # Cryptography
│   ├── io/               # I/O operations
│   ├── query/            # SQL parsing
│   └── batch/            # Batch processing
├── enclave/               # SGX enclave code
│   ├── trusted/          # Runs in enclave
│   └── untrusted/        # Runs outside
├── apps/                  # Applications
│   ├── sgx_join/         # Main app
│   └── tools/            # Utilities
├── tests/                 # All tests
│   ├── unit/
│   ├── integration/
│   └── performance/
└── build/                 # Build artifacts
```

### Benefits
- Clear separation of concerns
- Easier navigation
- Standard C++ project layout
- Build from anywhere
- All tests in one place

### Migration Strategy
1. Create new structure alongside old
2. Update Makefiles and paths
3. Test thoroughly
4. Remove old structure

### Current Workarounds
- Use scripts in `scripts/` directory for building and testing
- Paths are resolved relative to script location
- Documentation consolidated in `docs/`

## Why Not Done Now
The reorganization is a major change that requires:
- Updating all include paths
- Rewriting Makefiles
- Fixing hardcoded paths
- Extensive testing

This should be done when there's dedicated time for thorough testing and validation.