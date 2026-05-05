#ifndef DISTRIBUTED_SGX_ENCLAVE_SCALABLE_H
#define DISTRIBUTED_SGX_ENCLAVE_SCALABLE_H

#include <stddef.h>
#include "common/elem_t.h"
#include "common/ocalls.h"

int scalable_oblivious_join_init(int nthreads);

void scalable_oblivious_join_free();

/* Original text-emitting entry point. Writes matched pairs as ASCII into
 * output_path. sort_time_out (if non-NULL) receives the bitonic sort time
 * in seconds; the return value is the total execution time. */
double scalable_oblivious_join(elem_t *arr, int length1, int length2,
                                char* output_path, double *sort_time_out);

/* Multi-way variant: runs the same pipeline but hands back the matched pairs
 * as two elem_t[] arrays instead of rendering to ASCII.
 *
 *   arr          (in)  caller-owned buffer of length length1+length2,
 *                      with arr[0..length1)   table_0=true  and
 *                           arr[length1..)    table_0=false.
 *                      Contents are clobbered by the call.
 *   *arr1_out    (out) freshly-malloc'd array of length *result_len holding
 *                      matching table_0=true rows.  CALLER MUST FREE.
 *   *arr2_out    (out) freshly-malloc'd array of length *result_len holding
 *                      matching table_0=false rows. CALLER MUST FREE.
 *   *result_len  (out) number of matched pairs (full equi-join cross-product).
 *   *sort_time_out (out) bitonic sort time in seconds.
 *
 * Return value: total execution time in seconds.
 */
double scalable_oblivious_join_to_array(elem_t *arr, int length1, int length2,
                                         elem_t **arr1_out, elem_t **arr2_out,
                                         int *result_len,
                                         double *sort_time_out);

#endif /* distributed-sgx-sort/enclave/ojoin.h */
