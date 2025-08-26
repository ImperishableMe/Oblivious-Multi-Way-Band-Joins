#ifndef XOR_CRYPTO_H
#define XOR_CRYPTO_H

#include <stdint.h>

/**
 * Simple XOR encryption utilities
 * These are inline functions for maximum performance
 * XOR has the property that encrypt(encrypt(x)) = x
 */

// XOR for 32-bit unsigned integers
static inline uint32_t xor_uint32(uint32_t value, uint32_t key) {
    return value ^ key;
}

// XOR for 64-bit unsigned integers
static inline uint64_t xor_uint64(uint64_t value, uint64_t key) {
    return value ^ key;
}

// XOR for double values (treating as 64-bit)
static inline double xor_double(double value, uint64_t key) {
    uint64_t* ptr = (uint64_t*)&value;
    uint64_t encrypted = *ptr ^ key;
    return *(double*)&encrypted;
}

#endif // XOR_CRYPTO_H