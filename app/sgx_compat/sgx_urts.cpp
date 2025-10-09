#include "sgx_urts.h"
#include <stdio.h>

/* SGX Untrusted Runtime System Compatibility Implementation
 * Provides dummy enclave management for non-SGX builds
 */

sgx_status_t sgx_create_enclave(
    const char *file_name,
    const int debug,
    void *launch_token,
    int *launch_token_updated,
    sgx_enclave_id_t *enclave_id,
    void *misc_attr
) {
    (void)file_name;
    (void)debug;
    (void)launch_token;
    (void)misc_attr;

    /* Mark token as not updated */
    if (launch_token_updated) {
        *launch_token_updated = 0;
    }

    /* Return dummy enclave ID = 1 */
    if (enclave_id) {
        *enclave_id = 1;
    }

    /* Always succeed */
    return SGX_SUCCESS;
}

sgx_status_t sgx_destroy_enclave(
    const sgx_enclave_id_t enclave_id
) {
    (void)enclave_id;

    /* No-op: nothing to destroy in compatibility mode */
    return SGX_SUCCESS;
}
