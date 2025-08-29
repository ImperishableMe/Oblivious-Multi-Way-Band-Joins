#include <iostream>
#include <vector>
#include "sgx_urts.h"
#include "../app/Enclave_u.h"
#include "../app/data_structures/entry.h"
#include "../app/batch/ecall_batch_collector.h"
#include "../common/types_common.h"
#include "../common/debug_util.h"

int main() {
    std::cout << "=== Test: Individual Decryption After Batch Processing ===" << std::endl;
    
    // Initialize SGX enclave
    sgx_enclave_id_t eid;
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    sgx_launch_token_t token = {0};
    int updated = 0;
    
    ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, &token, &updated, &eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave: " << ret << std::endl;
        return 1;
    }
    std::cout << "Enclave created successfully (ID: " << eid << ")" << std::endl;
    
    // Create test entries with known values
    std::vector<Entry> entries;
    for (int i = 0; i < 5; i++) {
        Entry e;
        e.field_type = SOURCE;
        e.join_attr = i * 100;
        e.original_index = i;
        e.local_mult = 1;
        e.final_mult = i + 1;  // Known values: 1, 2, 3, 4, 5
        e.dst_idx = i * 10;     // Known values: 0, 10, 20, 30, 40
        e.is_encrypted = false;
        entries.push_back(e);
    }
    
    std::cout << "\nOriginal values (unencrypted):" << std::endl;
    for (size_t i = 0; i < entries.size(); i++) {
        std::cout << "  Entry " << i << ": dst_idx=" << entries[i].dst_idx 
                  << ", final_mult=" << entries[i].final_mult << std::endl;
    }
    
    // Encrypt all entries individually first
    std::cout << "\nEncrypting entries individually..." << std::endl;
    for (auto& e : entries) {
        entry_t temp = e.to_entry_t();
        crypto_status_t crypto_status;
        sgx_status_t status = ecall_encrypt_entry(eid, &crypto_status, &temp);
        if (status != SGX_SUCCESS || crypto_status != CRYPTO_SUCCESS) {
            std::cerr << "Failed to encrypt entry: sgx_status=" << status 
                      << ", crypto_status=" << crypto_status << std::endl;
            sgx_destroy_enclave(eid);
            return 1;
        }
        e.from_entry_t(temp);
    }
    std::cout << "All entries encrypted." << std::endl;
    
    // Verify individual decryption works before batching
    std::cout << "\nTesting individual decryption before batch processing:" << std::endl;
    bool all_correct_before = true;
    for (size_t i = 0; i < entries.size(); i++) {
        entry_t temp = entries[i].to_entry_t();
        int32_t output_size = 0;
        sgx_status_t status = ecall_obtain_output_size(eid, &output_size, &temp);
        if (status != SGX_SUCCESS) {
            std::cerr << "Failed to obtain output size: " << status << std::endl;
            all_correct_before = false;
        } else {
            int32_t expected = i * 10 + (i + 1);  // dst_idx + final_mult
            std::cout << "  Entry " << i << ": output_size=" << output_size 
                      << " (expected=" << expected << ")";
            if (output_size == expected) {
                std::cout << " ✓" << std::endl;
            } else {
                std::cout << " ✗ WRONG!" << std::endl;
                all_correct_before = false;
            }
        }
    }
    
    if (all_correct_before) {
        std::cout << "✓ All entries decrypt correctly before batch processing" << std::endl;
    } else {
        std::cout << "✗ Some entries failed before batch processing!" << std::endl;
    }
    
    // Now process through batch collector (this will decrypt and re-encrypt)
    std::cout << "\nProcessing through batch collector (OP_ECALL_WINDOW_COMPUTE_DST_IDX)..." << std::endl;
    EcallBatchCollector collector(eid, OP_ECALL_WINDOW_COMPUTE_DST_IDX);
    
    // Add operations that compute cumulative sum
    for (size_t i = 0; i < entries.size() - 1; i++) {
        collector.add_operation(entries[i], entries[i + 1]);
    }
    collector.flush();
    
    std::cout << "Batch processing complete. Stats:" << std::endl;
    const auto& stats = collector.get_stats();
    std::cout << "  Operations: " << stats.total_operations << std::endl;
    std::cout << "  Flushes: " << stats.total_flushes << std::endl;
    std::cout << "  Entries processed: " << stats.total_entries_processed << std::endl;
    
    // Check encryption status after batching
    std::cout << "\nChecking encryption status after batch:" << std::endl;
    for (size_t i = 0; i < entries.size(); i++) {
        std::cout << "  Entry " << i << ": is_encrypted=" << entries[i].is_encrypted << std::endl;
    }
    
    // Test individual decryption after batch processing
    std::cout << "\nTesting individual decryption AFTER batch processing:" << std::endl;
    bool all_correct_after = true;
    for (size_t i = 0; i < entries.size(); i++) {
        entry_t temp = entries[i].to_entry_t();
        
        // Debug: Show encrypted values (will be garbage if encrypted)
        if (temp.is_encrypted) {
            std::cout << "  Entry " << i << " (encrypted garbage): dst_idx=" << temp.dst_idx 
                      << ", final_mult=" << temp.final_mult << std::endl;
        }
        
        int32_t output_size = 0;
        sgx_status_t status = ecall_obtain_output_size(eid, &output_size, &temp);
        if (status != SGX_SUCCESS) {
            std::cerr << "    Failed to obtain output size: " << status << std::endl;
            all_correct_after = false;
        } else {
            // We know the original values before encryption
            int32_t expected_original = i * 10 + (i + 1);
            std::cout << "    Decrypted output_size=" << output_size
                      << " (expected=" << expected_original << ")";
            
            if (output_size == expected_original) {
                std::cout << " ✓" << std::endl;
            } else {
                std::cout << " ✗ WRONG!" << std::endl;
                all_correct_after = false;
            }
        }
    }
    
    if (all_correct_after) {
        std::cout << "\n✓ SUCCESS: All entries decrypt correctly after batch processing!" << std::endl;
    } else {
        std::cout << "\n✗ FAILURE: Batch processing corrupted decryption!" << std::endl;
    }
    
    // Also test with a fresh ecall to decrypt and check values manually
    std::cout << "\nTesting manual decrypt of last entry:" << std::endl;
    entry_t last = entries.back().to_entry_t();
    std::cout << "  Before decrypt: is_encrypted=" << last.is_encrypted 
              << ", dst_idx=" << last.dst_idx << ", final_mult=" << last.final_mult << std::endl;
    
    if (last.is_encrypted) {
        crypto_status_t crypto_status;
        sgx_status_t status = ecall_decrypt_entry(eid, &crypto_status, &last);
        if (status == SGX_SUCCESS && crypto_status == CRYPTO_SUCCESS) {
            std::cout << "  After decrypt: dst_idx=" << last.dst_idx 
                      << ", final_mult=" << last.final_mult << std::endl;
            std::cout << "  Sum=" << (last.dst_idx + last.final_mult) 
                      << " (expected=" << (4*10 + 5) << ")" << std::endl;
        } else {
            std::cerr << "  Failed to decrypt: sgx_status=" << status 
                      << ", crypto_status=" << crypto_status << std::endl;
        }
    }
    
    // Cleanup
    sgx_destroy_enclave(eid);
    std::cout << "\nTest complete." << std::endl;
    
    return all_correct_after ? 0 : 1;
}