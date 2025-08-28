#include "subtree_verifier.h"
#include <iostream>
#include <iomanip>

int32_t SubtreeVerifier::GetLocalMult(Entry entry, sgx_enclave_id_t eid) {
    // Decrypt if needed (test code can decrypt)
    if (entry.is_encrypted) {
        crypto_status_t status = CryptoUtils::decrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            throw std::runtime_error("Failed to decrypt entry for verification");
        }
    }
    
    return entry.local_mult;
}

int32_t SubtreeVerifier::GetOriginalIndex(Entry entry, sgx_enclave_id_t eid) {
    // Decrypt if needed
    if (entry.is_encrypted) {
        crypto_status_t status = CryptoUtils::decrypt_entry(entry, eid);
        if (status != CRYPTO_SUCCESS) {
            throw std::runtime_error("Failed to decrypt entry for verification");
        }
    }
    
    return entry.original_index;
}

bool SubtreeVerifier::RowMatchesOriginal(
    const Entry& result_row,
    const Entry& original,
    const std::string& table_name) {
    
    // Get all attributes from original
    auto orig_attrs = original.get_attributes_map();
    
    // Check if result row contains all original attributes
    for (const auto& [col_name, value] : orig_attrs) {
        // Try with and without table prefix
        if (result_row.has_attribute(col_name)) {
            if (result_row.get_attribute(col_name) != value) {
                return false;
            }
        } else if (result_row.has_attribute(table_name + "." + col_name)) {
            if (result_row.get_attribute(table_name + "." + col_name) != value) {
                return false;
            }
        } else {
            // Column not found in result
            return false;
        }
    }
    
    return true;
}

std::map<int32_t, int32_t> SubtreeVerifier::ComputeExpectedMultiplicities(
    JoinTreeNodePtr node,
    sgx_enclave_id_t eid) {
    
    std::map<int32_t, int32_t> expected;
    
    // Initialize counts to 0 for all original indices
    Table& node_table = node->get_table();
    for (size_t i = 0; i < node_table.size(); i++) {
        int32_t orig_idx = GetOriginalIndex(node_table[i], eid);
        expected[orig_idx] = 0;
    }
    
    // Use SimpleJoinExecutor to compute subtree join result
    SimpleJoinExecutor executor(eid);
    executor.set_decrypt_mode(true);  // Decrypt for testing
    Table subtree_result = executor.join_subtree(node);
    
    // Count how many times each original tuple appears in the result
    for (const auto& result_row : subtree_result) {
        // Find which original tuple this result row came from
        for (size_t i = 0; i < node_table.size(); i++) {
            Entry orig = node_table[i];
            
            // Decrypt original to compare attributes
            if (orig.is_encrypted) {
                CryptoUtils::decrypt_entry(orig, eid);
            }
            
            // Check if this result row contains the original tuple's data
            if (RowMatchesOriginal(result_row, orig, node->get_table_name())) {
                int32_t orig_idx = GetOriginalIndex(node_table[i], eid);
                expected[orig_idx]++;
                break;  // Found the match, move to next result row
            }
        }
    }
    
    return expected;
}

bool SubtreeVerifier::VerifyLocalMultiplicities(
    JoinTreeNodePtr node,
    const std::map<int32_t, int32_t>& expected,
    sgx_enclave_id_t eid,
    bool verbose) {
    
    Table& table = node->get_table();
    bool all_match = true;
    int mismatches = 0;
    int total_checked = 0;
    
    if (verbose) {
        std::cout << "\n  Verifying " << node->get_table_name() 
                  << " (" << table.size() << " rows):" << std::endl;
    }
    
    for (size_t i = 0; i < table.size(); i++) {
        Entry entry = table[i];
        
        // Get actual local_mult (requires decryption)
        int32_t actual = GetLocalMult(entry, eid);
        
        // Get original index to look up expected value
        int32_t orig_idx = GetOriginalIndex(entry, eid);
        
        // Get expected value
        auto it = expected.find(orig_idx);
        if (it == expected.end()) {
            std::cerr << "    ERROR: Original index " << orig_idx << " not found in expected map" << std::endl;
            all_match = false;
            continue;
        }
        
        int32_t expected_mult = it->second;
        total_checked++;
        
        if (actual != expected_mult) {
            mismatches++;
            all_match = false;
            if (verbose && mismatches <= 10) {  // Show first 10 mismatches
                std::cout << "    Row " << std::setw(4) << orig_idx 
                         << ": actual=" << std::setw(4) << actual 
                         << ", expected=" << std::setw(4) << expected_mult 
                         << " ✗" << std::endl;
            }
        } else if (verbose && (i < 5 || actual > 0)) {  
            // Show first few rows and any with non-zero multiplicity
            if (i < 5) {
                std::cout << "    Row " << std::setw(4) << orig_idx 
                         << ": " << std::setw(4) << actual << " ✓" << std::endl;
            }
        }
    }
    
    if (verbose) {
        if (all_match) {
            std::cout << "    All " << total_checked << " multiplicities correct ✓" << std::endl;
        } else {
            std::cout << "    " << mismatches << "/" << total_checked 
                     << " multiplicities incorrect ✗" << std::endl;
        }
    }
    
    return all_match;
}

bool SubtreeVerifier::VerifyFullTree(JoinTreeNodePtr node, sgx_enclave_id_t eid) {
    bool success = true;
    
    // Compute expected multiplicities for this node
    std::cout << "Computing expected multiplicities for " << node->get_table_name() << "..." << std::endl;
    auto expected = ComputeExpectedMultiplicities(node, eid);
    
    // Verify this node
    if (!VerifyLocalMultiplicities(node, expected, eid, true)) {
        success = false;
    }
    
    // Recursively verify children
    for (auto& child : node->get_children()) {
        if (!VerifyFullTree(child, eid)) {
            success = false;
        }
    }
    
    return success;
}