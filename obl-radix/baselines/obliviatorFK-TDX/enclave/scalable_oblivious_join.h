#ifndef DISTRIBUTED_SGX_ENCLAVE_SCALABLE_H
#define DISTRIBUTED_SGX_ENCLAVE_SCALABLE_H

#include <stddef.h>
#include "common/elem_t.h"
#include "common/ocalls.h"

struct tree_node_op2 {
    volatile long long key_first;
    volatile long long key_last;
    volatile long long key_prefix;
    volatile bool table0_fisrt;
    volatile bool table0_last;
    volatile bool table0_prefix;
    volatile bool complete1;
    volatile bool complete2;
};

struct args_op2 {
    long long index_thread_start;
    long long index_thread_end;
    elem_t* arr;
    elem_t* arr_;
    long long thread_order;
};

int scalable_oblivious_join_init(int nthreads);
long long o_strcmp(char* str1, char* str2);
void scalable_oblivious_join_free();

double scalable_oblivious_join(elem_t *arr, long long length1, long long length2, char* output_path, double *sort_time_out);

/* Multi-way variant: same as scalable_oblivious_join, but hands back the
 * matched pairs as two elem_t[] arrays instead of rendering to ASCII.
 *
 *   arr              (in/out) caller-owned buffer of length length1+length2.
 *                    On return, [0..*result_len) holds the compacted
 *                    table_0=false (probe-side) rows.
 *   *arr_out         (out)    freshly-malloc'd array of length length1+length2.
 *                    On return, [0..*result_len) holds the compacted
 *                    table_0=true (index-side) rows. CALLER MUST FREE.
 *   *result_len      (out)    number of matched pairs.
 *   *sort_time_out   (out)    bitonic sort time in seconds (same as original).
 *
 * Return value: total execution time in seconds.
 */
double scalable_oblivious_join_to_array(elem_t *arr, long long length1, long long length2,
                                         elem_t **arr_out, long long *result_len,
                                         double *sort_time_out);

#endif /* distributed-sgx-sort/enclave/ojoin.h */
