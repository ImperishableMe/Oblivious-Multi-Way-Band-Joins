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