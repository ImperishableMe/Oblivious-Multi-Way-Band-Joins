#ifndef JOIN_KEY_OPS_H
#define JOIN_KEY_OPS_H

#include "enclave_types.h"
#include <stdint.h>

/**
 * Composite join key helpers, shared by the comparators and window functions.
 *
 * An entry's join key is the tuple
 *   (join_eq[0], ..., join_eq[MAX_JOIN_EQ-1], join_attr)
 * ordered lexicographically: equality components first, the range component
 * last. Rows that agree on every equality component are therefore contiguous
 * and ordered by the range component, which is what lets "equalities plus one
 * range" be evaluated as a single band join over this order.
 *
 * Both helpers execute a fixed number of steps (MAX_JOIN_EQ + 1) with no
 * data-dependent branch. They compare with (a > b) - (a < b) rather than a
 * subtraction, so they cannot overflow.
 */

static inline int32_t join_key_three_way(int32_t a, int32_t b) {
    return (a > b) - (a < b);
}

/** Returns -1, 0 or 1 as e1's join key sorts before, equal to or after e2's. */
static inline int32_t join_key_cmp(const entry_t* e1, const entry_t* e2) {
    int32_t result = 0;
    for (int i = 0; i < MAX_JOIN_EQ; i++) {
        // Only the first differing component decides the order.
        result += (result == 0) * join_key_three_way(e1->join_eq[i], e2->join_eq[i]);
    }
    result += (result == 0) * join_key_three_way(e1->join_attr, e2->join_attr);
    return result;
}

/** Returns 1 if both entries carry the same join key, 0 otherwise. */
static inline int32_t join_key_equal(const entry_t* e1, const entry_t* e2) {
    return join_key_cmp(e1, e2) == 0;
}

#endif // JOIN_KEY_OPS_H
