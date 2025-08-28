#include <iostream>
#include "app/data_structures/table.h"
#include "app/io/table_io.h"
#include "app/Enclave_u.h"
#include "sgx_urts.h"

sgx_enclave_id_t global_eid = 0;

bool init_enclave() {
    sgx_status_t ret = SGX_SUCCESS;
    sgx_launch_token_t token = {0};
    int updated = 0;
    
    ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, &token, &updated, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: " << ret << std::endl;
        return false;
    }
    
    std::cout << "Enclave created successfully, EID: " << global_eid << std::endl;
    return true;
}

int main() {
    if (!init_enclave()) {
        return 1;
    }

    // Load a small encrypted table
    std::string table_path = "../../input/encrypted/data_0_001/supplier1.csv";
    Table table = TableIO::load_csv(table_path);
    std::cout << "Loaded table with " << table.size() << " entries" << std::endl;
    
    // Check first entry before any transformation
    if (table.size() > 0) {
        const Entry& e = table.get_entry(0);
        std::cout << "\nFirst entry BEFORE transform:" << std::endl;
        std::cout << "  field_type: " << e.field_type << std::endl;
        std::cout << "  is_encrypted: " << (e.is_encrypted ? "true" : "false") << std::endl;
        std::cout << "  join_attr: " << e.join_attr << std::endl;
        std::cout << "  original_index: " << e.original_index << std::endl;
        
        // Try a simple transform (set index)
        std::cout << "\nApplying set_index transform..." << std::endl;
        Table transformed = table.map(global_eid,
            [](sgx_enclave_id_t eid, entry_t* e) {
                return ecall_transform_set_index(eid, e, 999);
            });
        
        const Entry& e2 = transformed.get_entry(0);
        std::cout << "\nFirst entry AFTER transform:" << std::endl;
        std::cout << "  field_type: " << e2.field_type << std::endl;
        std::cout << "  is_encrypted: " << (e2.is_encrypted ? "true" : "false") << std::endl;
        std::cout << "  join_attr: " << e2.join_attr << std::endl;
        std::cout << "  original_index: " << e2.original_index << std::endl;
    }
    
    sgx_destroy_enclave(global_eid);
    return 0;
}