#ifndef BATCH_DISPATCHER_H
#define BATCH_DISPATCHER_H

#include <stddef.h>
#include "../../../common/enclave_types.h"
#include "../../../common/batch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void batch_dispatcher(entry_t* data_array, size_t data_count,
                     void* ops_array, size_t ops_count, size_t ops_size,
                     int32_t op_type);

#ifdef __cplusplus
}
#endif

#endif // BATCH_DISPATCHER_H