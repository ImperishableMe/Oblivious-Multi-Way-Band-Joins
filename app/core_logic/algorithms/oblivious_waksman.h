#ifndef OBLIVIOUS_WAKSMAN_H
#define OBLIVIOUS_WAKSMAN_H

#include <stddef.h>
#include "../../../common/enclave_types.h"
#include "../../../common/op_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Oblivious 2-way Waksman shuffle
 *
 * Performs data-oblivious shuffle of n entries using Waksman permutation network.
 * All memory accesses are independent of data values to prevent side-channel attacks.
 *
 * @param data Array of entries to shuffle (in-place)
 * @param n Number of entries (must be <= MAX_BATCH_SIZE)
 * @return 0 on success, -1 on error
 */
int oblivious_2way_waksman(entry_t* data, size_t n);

#ifdef __cplusplus
}
#endif

#endif // OBLIVIOUS_WAKSMAN_H