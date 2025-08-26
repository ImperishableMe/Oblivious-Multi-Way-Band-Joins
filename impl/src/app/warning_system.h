#ifndef WARNING_SYSTEM_H
#define WARNING_SYSTEM_H

#include <string>
#include <iostream>

/**
 * Warning System - Placeholder for future logging/warning infrastructure
 * 
 * TODO: Future enhancements:
 * - Log to file
 * - Severity levels (DEBUG, INFO, WARN, ERROR, FATAL)
 * - Timestamps
 * - Thread safety
 * - Configurable output destinations
 * - Performance counters
 */
class WarningSystem {
public:
    /**
     * Log a warning message
     * @param message Warning message to log
     */
    static void warn(const std::string& message) {
        std::cerr << "[WARNING] " << message << std::endl;
    }
    
    /**
     * Log an error message
     * @param message Error message to log
     */
    static void error(const std::string& message) {
        std::cerr << "[ERROR] " << message << std::endl;
    }
    
    /**
     * Log an info message
     * @param message Info message to log
     */
    static void info(const std::string& message) {
        std::cout << "[INFO] " << message << std::endl;
    }
    
    /**
     * Log a debug message (only in debug builds)
     * @param message Debug message to log
     */
    static void debug(const std::string& message) {
#ifdef DEBUG
        std::cout << "[DEBUG] " << message << std::endl;
#endif
    }
    
    // TODO: Add these methods in future:
    // static void set_log_file(const std::string& filename);
    // static void set_log_level(LogLevel level);
    // static void flush();
    // static void enable_timestamps(bool enable);
};

#endif // WARNING_SYSTEM_H