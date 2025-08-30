#include <iostream>
#include "app/io/table_io.h"

int main() {
    try {
        // Test the new interface - no encrypted parameter
        auto tables = TableIO::load_tables_from_directory("../../input/plaintext/data_0_001");
        std::cout << "Loaded " << tables.size() << " tables using new interface" << std::endl;
        
        // Also test load_csv_directory directly
        auto tables2 = TableIO::load_csv_directory("../../input/plaintext/data_0_001");
        std::cout << "Loaded " << tables2.size() << " tables using load_csv_directory" << std::endl;
        
        if (tables.size() == tables2.size()) {
            std::cout << "SUCCESS: Both methods load the same number of tables" << std::endl;
        } else {
            std::cout << "ERROR: Different table counts!" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
