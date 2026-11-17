#ifndef CONSTANTS_H
#define CONSTANTS_H

// Maximum number of attributes per entry.
// Overridable at compile time (-DMAX_ATTRIBUTES=N) so a memory-reduced build
// (e.g. the slim HI-Large E2 run via `make sgx_app_slim`) can shrink the fixed
// per-entry attribute array. The minimum safe value is the widest intermediate
// the query plan produces; default is 64.
#ifndef MAX_ATTRIBUTES
#define MAX_ATTRIBUTES 64
#endif

// Maximum number of extra equality columns in a composite join key.
// Two tables may be joined on up to MAX_JOIN_EQ equality conditions plus one
// more condition (an equality or a single range). The equality columns are
// stored in entry_t::join_eq, the last condition in entry_t::join_attr.
// Each unit adds 4 bytes to every entry. Overridable at compile time
// (-DMAX_JOIN_EQ=N, or `make MAX_JOIN_EQ=N`); must be at least 1.
#ifndef MAX_JOIN_EQ
#define MAX_JOIN_EQ 2
#endif
#if MAX_JOIN_EQ < 1
#error "MAX_JOIN_EQ must be at least 1"
#endif

// Parameter layout of OP_ECALL_TRANSFORM_SET_JOIN_ATTR (the int32_t array
// handed to transform_set_join_key_op). All of it is derived from the query.
#define JOIN_KEY_PARAM_RANGE_COLUMN 0                   // attribute index copied into join_attr
#define JOIN_KEY_PARAM_NUM_EQ       1                   // equality columns in use (<= MAX_JOIN_EQ)
#define JOIN_KEY_PARAM_EQ_COLUMNS   2                   // MAX_JOIN_EQ attribute indices for join_eq
#define JOIN_KEY_PARAM_EQ_OFFSETS   (2 + MAX_JOIN_EQ)   // MAX_JOIN_EQ constants added to them
#define JOIN_KEY_PARAM_COUNT        (2 + 2 * MAX_JOIN_EQ)

// Window size for linear pass operations
#define WINDOW_SIZE 2

// AES encryption parameters
#define AES_KEY_SIZE 32
#define AES_BLOCK_SIZE 16

// Maximum table size for testing
#define MAX_TABLE_SIZE 10000

// Batch processing parameters
#define MAX_BATCH_SIZE 2000               // Maximum batch size for operations

// Merge sort parameters
#define MERGE_SORT_K 8                    // Number of ways for k-way merge
#define MERGE_BUFFER_SIZE (MAX_BATCH_SIZE / MERGE_SORT_K)  // Buffer size per run

#endif // CONSTANTS_H