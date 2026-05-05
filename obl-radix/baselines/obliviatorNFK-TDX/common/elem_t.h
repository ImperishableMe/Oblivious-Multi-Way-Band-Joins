#ifndef __COMMON_NODE_T_H
#define __COMMON_NODE_T_H

#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* DATA_LENGTH — per-element payload budget in bytes.
 * Sized for the widest multi-way workload in docs/workloads.md:
 *   IBM AML-Data W4 5-hop chain carries up to 52 int32 columns as ASCII
 *   (each ≤ 11 chars in [-1,073,741,820, 1,073,741,820]) plus 51 comma
 *   separators ≈ 623 B peak on the intermediate side just before the final
 *   pairwise join. 640 leaves headroom for the null terminator and minor
 *   overcounts. Override with -DDATA_LENGTH=N for future workloads.
 */
#ifndef DATA_LENGTH
#define DATA_LENGTH 640
#endif

typedef int ojoin_int_type;

typedef struct elem {
    char data[DATA_LENGTH];
    bool has_value;
    bool table_0;
    int key;
    int m0;
    int m1;
    int j_order;
} elem_t;

/* Expected layout:
 *   data[DATA_LENGTH] + 2 bools + (padding to int alignment) + 4 ints
 * sizeof = ((DATA_LENGTH + 2 + 3) / 4) * 4 + 16
 *   DATA_LENGTH=14  -> 16 + 16 = 32  (original)
 *   DATA_LENGTH=640 -> 644 + 16 = 660
 */
static_assert(sizeof(elem_t) == (((DATA_LENGTH + 2 + 3) / 4) * 4 + 16),
              "elem_t layout mismatch — check DATA_LENGTH and padding");

#endif /* common/elem_t.h */
