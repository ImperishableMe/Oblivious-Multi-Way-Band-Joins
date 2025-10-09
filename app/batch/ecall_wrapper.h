#ifndef ECALL_WRAPPER_H
#define ECALL_WRAPPER_H

#include <atomic>

/*
 * Performance monitoring counters for TDX migration.
 *
 * These counters are kept for performance monitoring even though
 * we no longer have ecall/ocall boundaries with TDX. They can be
 * used to track batch operations and other function calls.
 */

// Global batch operation counter (formerly ecall counter)
extern std::atomic<size_t> g_ecall_count;

// Global callback counter (formerly ocall counter)
extern std::atomic<size_t> g_ocall_count;

// Counter management functions
void reset_ecall_count();
size_t get_ecall_count();

void reset_ocall_count();
size_t get_ocall_count();

#endif // ECALL_WRAPPER_H