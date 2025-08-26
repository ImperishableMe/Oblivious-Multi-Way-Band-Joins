# SGX Oblivious Multi-Way Band Join Implementation

This implementation provides an oblivious multi-way band join algorithm using Intel SGX for secure computation.

## Important Design Decisions

### 1. Integer Range Limitation for Overflow Prevention

**Issue**: Band joins with inequality constraints require handling infinity values for unbounded ranges. Adding deviations to join attributes near the integer limits could cause overflow.

**Solution**: We limit the valid range of join attributes to **approximately half the int32_t range**:
- Valid range: `-1,073,741,820` to `1,073,741,820`
- Negative infinity: `-1,073,741,821` (just outside valid range)
- Positive infinity: `1,073,741,821` (just outside valid range)

**Rationale**: 
- For any valid `join_attr` and `deviation` within the valid range, `join_attr + deviation` will never overflow
- Infinity values are outside the valid range and are handled specially (never added to)
- We never perform `inf + inf` operations, only `finite + finite` or direct infinity assignments
- This provides over 2 billion valid values, which is sufficient for most applications

### 2. XOR Encryption (NOT Cryptographically Secure)

**⚠️ WARNING**: This implementation uses simple XOR encryption for demonstration purposes only.

**What we use**: 
- Simple XOR with a 32-bit key
- Same operation for encryption and decryption
- Encrypts entire struct except `is_encrypted` flag and `column_names`

**Why XOR**:
- Simple and fast for proof-of-concept
- Preserves oblivious access patterns
- Easy to verify correctness

**For Production**: Replace XOR with proper encryption such as:
- AES-GCM for authenticated encryption
- AES-CTR for stream cipher
- Format-preserving encryption if needed

## Architecture

### Trusted (Enclave) Components
- **Window Functions**: Compute multiplicities with sliding window operations
- **Comparators**: Oblivious sorting comparators
- **Crypto**: XOR encryption/decryption

### Untrusted (App) Components
- **Main Application**: Orchestrates the algorithm phases
- **Data Management**: Entry/Table classes
- **Type Conversion**: Entry ↔ entry_t conversion

## Building and Running

```bash
make clean
make
./sgx_app test
```

## Security Guarantees

1. **Data Obliviousness**: Memory access patterns reveal no information about data values
2. **Constant-Time Operations**: All operations use arithmetic instead of branching
3. **SGX Protection**: Sensitive computations occur within the secure enclave

## Limitations

1. Join attributes must be within `[JOIN_ATTR_MIN, JOIN_ATTR_MAX]`
2. XOR encryption is for demonstration only - not secure against cryptanalysis
3. Maximum table sizes limited by SGX enclave heap size (128MB configured)