#include "../common/debug_util.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>
#include <cstring>

// Thread-safe output mutex
static std::mutex debug_mutex;

// ANSI color codes for terminal output
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_RESET   "\033[0m"

// Get color for debug level
static const char* get_level_color(uint32_t level) {
    switch(level) {
        case DEBUG_LEVEL_ERROR: return COLOR_RED;
        case DEBUG_LEVEL_WARN:  return COLOR_YELLOW;
        case DEBUG_LEVEL_INFO:  return COLOR_GREEN;
        case DEBUG_LEVEL_DEBUG: return COLOR_BLUE;
        case DEBUG_LEVEL_TRACE: return COLOR_MAGENTA;
        default: return COLOR_RESET;
    }
}

// Get short filename (last component only)
static const char* get_short_filename(const char* file) {
    const char* last_slash = strrchr(file, '/');
    return last_slash ? last_slash + 1 : file;
}

// Main debug print function for app environment
extern "C" void debug_print(uint32_t level, const char* file, int line, const char* fmt, ...) {
    if (level > DEBUG_LEVEL) return;
    
    std::lock_guard<std::mutex> lock(debug_mutex);
    
    // Get current time
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    // Output stream based on level
    FILE* stream = (level <= DEBUG_LEVEL_ERROR) ? stderr : stdout;
    
    // Print header with color
    fprintf(stream, "%s[%s][%s][%s:%d] ", 
            get_level_color(level),
            time_str,
            debug_level_str(level),
            get_short_filename(file),
            line);
    
    // Print the actual message
    va_list args;
    va_start(args, fmt);
    vfprintf(stream, fmt, args);
    va_end(args);
    
    // Reset color and newline
    fprintf(stream, "%s\n", COLOR_RESET);
    fflush(stream);
}

// OCALL handler for enclave debug output
extern "C" void ocall_debug_print(uint32_t level, const char* file, int line, const char* message) {
    // Just forward to the main debug_print with the pre-formatted message
    debug_print(level, file, line, "%s", message);
}