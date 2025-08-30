#include "app/debug_manager.h"
#include "app/data_structures/table.h"
#include "app/data_structures/entry.h"
#include <iostream>
#include <vector>

/**
 * Test program to demonstrate the centralized debug system
 */

void test_basic_logging() {
    std::cout << "\n=== Testing Basic Logging ===" << std::endl;
    
    // Get debug manager instance
    auto& mgr = DebugManager::getInstance();
    
    // Set debug level to INFO
    mgr.setDebugLevel(DEBUG_LEVEL_INFO);
    
    // Start a debug session
    mgr.startSession("test_basic_logging");
    
    // Log at different levels
    DEBUG_LOG(DEBUG_LEVEL_ERROR, "This is an error message");
    DEBUG_LOG(DEBUG_LEVEL_WARN, "This is a warning message");
    DEBUG_LOG(DEBUG_LEVEL_INFO, "This is an info message");
    DEBUG_LOG(DEBUG_LEVEL_DEBUG, "This debug message should not appear at INFO level");
    
    // End session
    mgr.endSession();
    
    std::cout << "Logs written: " << mgr.getLogsWritten() << std::endl;
}

void test_module_specific_logging() {
    std::cout << "\n=== Testing Module-Specific Logging ===" << std::endl;
    
    auto& mgr = DebugManager::getInstance();
    
    // Configure to enable only specific modules
    DebugConfig config = DEBUG_CONFIG_DEFAULT;
    config.level = DEBUG_LEVEL_DEBUG;
    config.phases.bottom_up = true;
    config.phases.top_down = false;
    config.phases.distribute = true;
    mgr.setConfig(config);
    
    mgr.startSession("test_modules");
    
    // Log from different modules
    DEBUG_LOG_MODULE(DEBUG_LEVEL_INFO, "bottom_up", "This should appear (bottom_up enabled)");
    DEBUG_LOG_MODULE(DEBUG_LEVEL_INFO, "top_down", "This should NOT appear (top_down disabled)");
    DEBUG_LOG_MODULE(DEBUG_LEVEL_INFO, "distribute", "This should appear (distribute enabled)");
    
    mgr.endSession();
}

void test_table_dumping() {
    std::cout << "\n=== Testing Table Dumping ===" << std::endl;
    
    auto& mgr = DebugManager::getInstance();
    
    // Configure for table dumping
    DebugConfig config = DEBUG_CONFIG_DEVELOPMENT;
    config.tables.enabled = true;
    config.tables.stages.inputs = true;
    config.tables.stages.after_sort = true;
    config.tables.stages.outputs = true;
    mgr.setConfig(config);
    
    mgr.startSession("test_tables");
    
    // Create a test table
    Table test_table;
    test_table.set_table_name("test_table");
    
    // Add some test entries
    for (int i = 0; i < 5; i++) {
        Entry entry;
        entry.original_index = i;
        entry.local_mult = i * 2;
        entry.join_attr = 100 + i;
        entry.field_type = (i % 2 == 0) ? 1 : 2; // SOURCE or START
        test_table.push_back(entry);
    }
    
    // Test conditional dumping
    DEBUG_DUMP_TABLE_IF("input", test_table, "initial_data", 0);
    DEBUG_DUMP_TABLE_IF("after_sort", test_table, "sorted_data", 0);
    DEBUG_DUMP_TABLE_IF("after_cumsum", test_table, "cumsum_data", 0); // Should not dump
    DEBUG_DUMP_TABLE_IF("output", test_table, "final_data", 0);
    
    mgr.endSession();
    
    std::cout << "Tables dumped: " << mgr.getTablesDumped() << std::endl;
}

void test_performance_tracking() {
    std::cout << "\n=== Testing Performance Tracking ===" << std::endl;
    
    auto& mgr = DebugManager::getInstance();
    
    // Enable performance tracking
    DebugConfig config = DEBUG_CONFIG_DEFAULT;
    config.level = DEBUG_LEVEL_INFO;
    config.perf.enabled = true;
    config.perf.per_phase = true;
    mgr.setConfig(config);
    
    mgr.startSession("test_performance");
    
    // Simulate phases with timing
    DEBUG_PHASE_START("bottom_up");
    // Simulate some work
    for (volatile int i = 0; i < 1000000; i++);
    DEBUG_PHASE_END("bottom_up");
    
    DEBUG_PHASE_START("top_down");
    // Simulate some work
    for (volatile int i = 0; i < 2000000; i++);
    DEBUG_PHASE_END("top_down");
    
    DEBUG_PHASE_START("distribute");
    // Simulate some work
    for (volatile int i = 0; i < 1500000; i++);
    DEBUG_PHASE_END("distribute");
    
    // Log performance summary
    mgr.logPerformanceSummary();
    
    mgr.endSession();
    
    std::cout << "Bottom-up time: " << mgr.getPhaseTime("bottom_up") << " ms" << std::endl;
    std::cout << "Top-down time: " << mgr.getPhaseTime("top_down") << " ms" << std::endl;
    std::cout << "Distribute time: " << mgr.getPhaseTime("distribute") << " ms" << std::endl;
}

void test_config_file_loading() {
    std::cout << "\n=== Testing Config File Loading ===" << std::endl;
    
    auto& mgr = DebugManager::getInstance();
    
    // Try to load from example config file
    mgr.loadConfig("debug.conf.example");
    
    // Verify configuration was loaded
    const auto& config = mgr.getConfig();
    std::cout << "Debug level: " << config.level << std::endl;
    std::cout << "Output mode: " << config.output_mode << std::endl;
    std::cout << "Tables enabled: " << config.tables.enabled << std::endl;
    std::cout << "Bottom-up phase enabled: " << config.phases.bottom_up << std::endl;
}

void test_conditional_execution() {
    std::cout << "\n=== Testing Conditional Execution ===" << std::endl;
    
    auto& mgr = DebugManager::getInstance();
    
    // Enable only specific phases
    mgr.enablePhase("bottom_up", true);
    mgr.enablePhase("top_down", false);
    
    mgr.startSession("test_conditional");
    
    // Conditional code execution
    DEBUG_IF_PHASE("bottom_up", {
        std::cout << "Bottom-up phase code executed" << std::endl;
        DEBUG_LOG(DEBUG_LEVEL_INFO, "Processing bottom-up phase");
    });
    
    DEBUG_IF_PHASE("top_down", {
        std::cout << "This should not be printed (top_down disabled)" << std::endl;
        DEBUG_LOG(DEBUG_LEVEL_INFO, "This log should not appear");
    });
    
    mgr.endSession();
}

int main(int argc, char* argv[]) {
    std::cout << "=== Centralized Debug System Test ===" << std::endl;
    
    // Run all tests
    test_basic_logging();
    test_module_specific_logging();
    test_table_dumping();
    test_performance_tracking();
    test_config_file_loading();
    test_conditional_execution();
    
    std::cout << "\n=== All Tests Complete ===" << std::endl;
    
    return 0;
}