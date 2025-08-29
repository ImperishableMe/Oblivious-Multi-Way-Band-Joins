#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_ATTRIBUTES 10
#define MAX_COLUMN_NAME_LEN 64

typedef struct {
    int32_t field_type;
    int32_t equality_type;
    uint8_t is_encrypted;
    uint64_t nonce;
    int32_t join_attr;
    int32_t original_index;
    int32_t local_mult;
    int32_t final_mult;
    int32_t foreign_sum;
    int32_t local_cumsum;
    int32_t local_interval;
    int32_t foreign_interval;  // Note: foreign_cumsum removed
    int32_t local_weight;
    int32_t copy_index;
    int32_t alignment_key;
    int32_t dst_idx;
    int32_t index;
    int32_t attributes[MAX_ATTRIBUTES];
    char column_names[MAX_ATTRIBUTES][MAX_COLUMN_NAME_LEN];
} entry_t_new;

typedef struct {
    int32_t field_type;
    int32_t equality_type;
    uint8_t is_encrypted;
    uint64_t nonce;
    int32_t join_attr;
    int32_t original_index;
    int32_t local_mult;
    int32_t final_mult;
    int32_t foreign_sum;
    int32_t local_cumsum;
    int32_t local_interval;
    int32_t foreign_cumsum;     // This field was removed
    int32_t foreign_interval;
    int32_t local_weight;
    int32_t copy_index;
    int32_t alignment_key;
    int32_t dst_idx;
    int32_t index;
    int32_t attributes[MAX_ATTRIBUTES];
    char column_names[MAX_ATTRIBUTES][MAX_COLUMN_NAME_LEN];
} entry_t_old;

int main() {
    printf("OLD struct:\n");
    printf("  sizeof(entry_t_old) = %lu\n", sizeof(entry_t_old));
    printf("  offset of attributes = %lu\n", offsetof(entry_t_old, attributes));
    printf("  offset of column_names = %lu\n", offsetof(entry_t_old, column_names));
    
    printf("\nNEW struct:\n");
    printf("  sizeof(entry_t_new) = %lu\n", sizeof(entry_t_new));
    printf("  offset of attributes = %lu\n", offsetof(entry_t_new, attributes));
    printf("  offset of column_names = %lu\n", offsetof(entry_t_new, column_names));
    
    printf("\nDifference:\n");
    printf("  Size difference = %ld bytes\n", sizeof(entry_t_old) - sizeof(entry_t_new));
    printf("  Attributes offset difference = %ld\n", 
           (long)offsetof(entry_t_old, attributes) - (long)offsetof(entry_t_new, attributes));
    
    return 0;
}
