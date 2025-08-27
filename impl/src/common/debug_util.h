#ifndef DEBUG_UTIL_H
#define DEBUG_UTIL_H

#include <stdint.h>

// Debug levels - higher level includes all lower levels
#define DEBUG_LEVEL_NONE  0
#define DEBUG_LEVEL_ERROR 1
#define DEBUG_LEVEL_WARN  2
#define DEBUG_LEVEL_INFO  3
#define DEBUG_LEVEL_DEBUG 4
#define DEBUG_LEVEL_TRACE 5

// Set debug level via compilation flag, default to NONE for production
#ifndef DEBUG_LEVEL
    #ifdef DEBUG
        #define DEBUG_LEVEL DEBUG_LEVEL_DEBUG
    #else
        #define DEBUG_LEVEL DEBUG_LEVEL_NONE
    #endif
#endif

// Function declarations based on environment
#ifdef ENCLAVE_BUILD
    // Enclave environment - will use ocall
    void enclave_debug_print(uint32_t level, const char* file, int line, const char* fmt, ...);
    #define debug_print enclave_debug_print
#else
    // App environment - direct implementation
    #ifdef __cplusplus
    extern "C" {
    #endif
    void debug_print(uint32_t level, const char* file, int line, const char* fmt, ...);
    #ifdef __cplusplus
    }
    #endif
#endif

// Macro definitions with zero overhead when disabled
#if DEBUG_LEVEL >= DEBUG_LEVEL_ERROR
    #define DEBUG_ERROR(fmt, ...) \
        debug_print(DEBUG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_ERROR(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_WARN
    #define DEBUG_WARN(fmt, ...) \
        debug_print(DEBUG_LEVEL_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_WARN(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_INFO
    #define DEBUG_INFO(fmt, ...) \
        debug_print(DEBUG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_INFO(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_DEBUG
    #define DEBUG_DEBUG(fmt, ...) \
        debug_print(DEBUG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_DEBUG(fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= DEBUG_LEVEL_TRACE
    #define DEBUG_TRACE(fmt, ...) \
        debug_print(DEBUG_LEVEL_TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
    #define DEBUG_ENTRY() DEBUG_TRACE("Entering %s", __FUNCTION__)
    #define DEBUG_EXIT() DEBUG_TRACE("Exiting %s", __FUNCTION__)
#else
    #define DEBUG_TRACE(fmt, ...) ((void)0)
    #define DEBUG_ENTRY() ((void)0)
    #define DEBUG_EXIT() ((void)0)
#endif

// Convenience macros for common debugging tasks
#if DEBUG_LEVEL > DEBUG_LEVEL_NONE
    #define DEBUG_ASSERT(cond, fmt, ...) \
        do { \
            if (!(cond)) { \
                DEBUG_ERROR("ASSERTION FAILED: " #cond); \
                DEBUG_ERROR(fmt, ##__VA_ARGS__); \
            } \
        } while(0)
    
    #define DEBUG_HEX_DUMP(label, data, len) \
        do { \
            DEBUG_TRACE("%s (%zu bytes):", label, (size_t)(len)); \
            for (size_t i = 0; i < (size_t)(len); i++) { \
                if (i % 16 == 0) DEBUG_TRACE("  %04zx: ", i); \
                DEBUG_TRACE("%02x ", ((uint8_t*)(data))[i]); \
                if (i % 16 == 15) DEBUG_TRACE("\n"); \
            } \
            if ((len) % 16 != 0) DEBUG_TRACE("\n"); \
        } while(0)
#else
    #define DEBUG_ASSERT(cond, fmt, ...) ((void)0)
    #define DEBUG_HEX_DUMP(label, data, len) ((void)0)
#endif

// Level to string conversion for output
static inline const char* debug_level_str(uint32_t level) {
    switch(level) {
        case DEBUG_LEVEL_ERROR: return "ERROR";
        case DEBUG_LEVEL_WARN:  return "WARN ";
        case DEBUG_LEVEL_INFO:  return "INFO ";
        case DEBUG_LEVEL_DEBUG: return "DEBUG";
        case DEBUG_LEVEL_TRACE: return "TRACE";
        default: return "UNKN ";
    }
}

#endif // DEBUG_UTIL_H