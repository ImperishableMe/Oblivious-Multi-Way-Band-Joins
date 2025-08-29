#ifndef BATCH_DISPATCHER_H
#define BATCH_DISPATCHER_H

#include "../enclave_types.h"
#include "../../common/batch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Batch dispatcher for executing multiple operations in a single ecall
 * This function is called from the untrusted side with arrays of data and operations
 * 
 * @param data_array Array of entry_t data to operate on
 * @param data_count Number of entries in data_array
 * @param ops_array Array of operations to execute
 * @param ops_count Number of operations to execute
 * @param op_type Type of operation to execute
 */
void ecall_batch_dispatcher(entry_t* data_array, size_t data_count,
                           BatchOperation* ops_array, size_t ops_count,
                           OpEcall op_type);

#ifdef __cplusplus
}
#endif

#endif // BATCH_DISPATCHER_H