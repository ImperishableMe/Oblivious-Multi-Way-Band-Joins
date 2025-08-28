#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t field_type;
    int32_t equality_type;
    uint8_t is_encrypted;
    uint64_t nonce;
    int32_t join_attr;
} entry_t;

int main() {
    printf("field_type offset: %zu\n", offsetof(entry_t, field_type));
    printf("equality_type offset: %zu\n", offsetof(entry_t, equality_type));
    printf("is_encrypted offset: %zu\n", offsetof(entry_t, is_encrypted));
    printf("nonce offset: %zu\n", offsetof(entry_t, nonce));
    printf("join_attr offset: %zu\n", offsetof(entry_t, join_attr));
    printf("sizeof(int32_t): %zu\n", sizeof(int32_t));
    printf("sizeof(uint8_t): %zu\n", sizeof(uint8_t));
    printf("sizeof(uint64_t): %zu\n", sizeof(uint64_t));
    return 0;
}
