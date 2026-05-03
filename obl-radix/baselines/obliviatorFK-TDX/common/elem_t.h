#ifndef __COMMON_NODE_T_H
#define __COMMON_NODE_T_H

#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* DATA_LENGTH is the per-element payload budget in bytes.
 *
 * Sized for the widest multi-way workload in docs/workloads.md:
 *   IBM AML-Data W4 5-hop chain carries up to 52 int32 columns as ASCII
 *   (each ≤ 11 chars in the bounded range [-1,073,741,820, 1,073,741,820])
 *   plus 51 comma separators ≈ 623 B peak on the intermediate side
 *   just before the final pairwise join. 640 leaves headroom for the
 *   null terminator and minor overcounts.
 *
 * Override at compile time with -DDATA_LENGTH=N for future workloads.
 */
#ifndef DATA_LENGTH
#define DATA_LENGTH 64
#endif

typedef long long ojoin_int_type;

typedef struct elem {
    char data[DATA_LENGTH];
    bool table_0;
    long long key;
} elem_t;

/* Expected layout: data[N] + bool (+padding to 8) + long long.
 * sizeof = ((N + 1 + 7) / 8) * 8 + 8. Defaults: N=640 -> 656; N=63 -> 72. */
static_assert(sizeof(elem_t) == (((DATA_LENGTH + 1 + 7) / 8) * 8 + 8),
              "elem_t layout mismatch — check DATA_LENGTH and padding");

#endif /* common/elem_t.h */
