#include "ecall_wrapper.h"

// Global ecall counter
std::atomic<size_t> g_ecall_count(0);

void reset_ecall_count() {
    g_ecall_count.store(0, std::memory_order_relaxed);
}

size_t get_ecall_count() {
    return g_ecall_count.load(std::memory_order_relaxed);
}