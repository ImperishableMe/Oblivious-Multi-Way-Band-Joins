#include "../../common/debug_util.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
// #include "sgx_trts.h" - Removed, will use compat types

// Forward declaration of the ocall function (to be implemented in Phase 3)
typedef int sgx_status_t;
#define SGX_SUCCESS 0
extern sgx_status_t ocall_debug_print(uint32_t level, const char* file, int line, const char* message);

// Enclave-side debug print implementation
void enclave_debug_print(uint32_t level, const char* file, int line, const char* fmt, ...) {
    if (level > DEBUG_LEVEL) return;
    
    // Format the message inside the enclave
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    // Make sure string is null-terminated
    buffer[sizeof(buffer) - 1] = '\0';
    
    // Call out to untrusted app for actual printing
    sgx_status_t status = ocall_debug_print(level, file, line, buffer);
    
    // Silently ignore ocall failures (can't do much about them)
    (void)status;
}