#ifndef SGX_COMPAT_URTS_H
#define SGX_COMPAT_URTS_H

#include "sgx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SGX Untrusted Runtime System Compatibility Layer
 * Provides dummy implementations of SGX enclave management functions
 * for non-SGX builds
 */

/**
 * Create enclave (dummy implementation)
 * In this compatibility layer, we don't actually create an enclave.
 * We just return a dummy enclave ID.
 *
 * @param file_name Enclave library path (ignored)
 * @param debug Debug mode flag (ignored)
 * @param launch_token Launch token (ignored)
 * @param launch_token_updated Token updated flag (ignored)
 * @param enclave_id Output: enclave ID (always set to 1)
 * @param misc_attr Misc attributes (ignored)
 * @return SGX_SUCCESS always
 */
sgx_status_t sgx_create_enclave(
    const char *file_name,
    const int debug,
    void *launch_token,
    int *launch_token_updated,
    sgx_enclave_id_t *enclave_id,
    void *misc_attr
);

/**
 * Destroy enclave (dummy implementation)
 * No-op in compatibility layer
 *
 * @param enclave_id Enclave ID (ignored)
 * @return SGX_SUCCESS always
 */
sgx_status_t sgx_destroy_enclave(
    const sgx_enclave_id_t enclave_id
);

#ifdef __cplusplus
}
#endif

#endif /* SGX_COMPAT_URTS_H */
