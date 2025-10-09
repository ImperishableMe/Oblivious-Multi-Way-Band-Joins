#ifndef SGX_COMPAT_TYPES_H
#define SGX_COMPAT_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* SGX Type Compatibility Layer
 * This header provides SGX type definitions for non-SGX builds
 * Allows code to compile without Intel SGX SDK
 */

/* Basic SGX types */
typedef uint64_t sgx_enclave_id_t;
typedef int sgx_status_t;

/* SGX Status codes */
#define SGX_SUCCESS                     0x0000
#define SGX_ERROR_UNEXPECTED            0x0001
#define SGX_ERROR_INVALID_PARAMETER     0x0002
#define SGX_ERROR_OUT_OF_MEMORY         0x0003
#define SGX_ERROR_ENCLAVE_LOST          0x0004
#define SGX_ERROR_INVALID_STATE         0x0005
#define SGX_ERROR_FEATURE_NOT_SUPPORTED 0x0008

/* Additional common error codes */
#define SGX_ERROR_INVALID_FUNCTION      0x1001
#define SGX_ERROR_OUT_OF_TCS            0x1003
#define SGX_ERROR_ENCLAVE_CRASHED       0x1006
#define SGX_ERROR_ECALL_NOT_ALLOWED     0x1007
#define SGX_ERROR_OCALL_NOT_ALLOWED     0x1008
#define SGX_ERROR_STACK_OVERRUN         0x1009

/* Crypto-related error codes */
#define SGX_ERROR_MAC_MISMATCH          0x3001
#define SGX_ERROR_INVALID_ATTRIBUTE     0x4001
#define SGX_ERROR_INVALID_METADATA      0x4009

/* Cast macro for compatibility */
#define SGX_CAST(type, item) ((type)(item))

/* Debug/release mode flag */
#define SGX_DEBUG_FLAG 1

#endif /* SGX_COMPAT_TYPES_H */
