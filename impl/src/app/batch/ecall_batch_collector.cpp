#include "ecall_batch_collector.h"
#include "../Enclave_u.h"
#include "../../common/debug_util.h"
#include <stdexcept>
#include <sstream>

EcallBatchCollector::EcallBatchCollector(sgx_enclave_id_t enclave_id, OpEcall op, size_t max_size)
    : eid(enclave_id), op_type(op), max_batch_size(max_size), stats{0, 0, 0, 0} {
    
    if (max_size > MAX_BATCH_SIZE) {
        throw std::invalid_argument("Batch size exceeds maximum allowed: " + 
                                   std::to_string(MAX_BATCH_SIZE));
    }
    
    // Reserve space to avoid frequent reallocations
    batch_data.reserve(max_batch_size);
    operations.reserve(max_batch_size);
    
    DEBUG_INFO("BatchCollector: Created for operation %d with max batch size %zu", 
               op_type, max_batch_size);
}

EcallBatchCollector::~EcallBatchCollector() {
    // Ensure any pending operations are flushed
    if (!operations.empty()) {
        DEBUG_INFO("BatchCollector: Flushing %zu pending operations in destructor", 
                   operations.size());
        flush();
    }
    
    DEBUG_INFO("BatchCollector: Destroyed. Stats: %zu operations, %zu flushes, %zu entries", 
               stats.total_operations, stats.total_flushes, stats.total_entries_processed);
}

void EcallBatchCollector::check_sgx_status(sgx_status_t status, const std::string& operation) {
    if (status != SGX_SUCCESS) {
        std::stringstream ss;
        ss << "SGX error in " << operation << ": " << status;
        throw std::runtime_error(ss.str());
    }
}

void EcallBatchCollector::add_operation(Entry& e1, Entry& e2, int32_t* params) {
    // Deduplicate e1
    Entry* ptr1 = &e1;
    auto it1 = entry_map.find(ptr1);
    int32_t idx1;
    
    if (it1 == entry_map.end()) {
        // New entry - add to batch data
        idx1 = static_cast<int32_t>(batch_data.size());
        entry_map[ptr1] = idx1;
        batch_data.push_back(e1.to_entry_t());
        DEBUG_TRACE("BatchCollector: Added new entry at index %d", idx1);
    } else {
        // Existing entry - use its index
        idx1 = it1->second;
        DEBUG_TRACE("BatchCollector: Reusing entry at index %d", idx1);
    }
    
    // Deduplicate e2
    Entry* ptr2 = &e2;
    auto it2 = entry_map.find(ptr2);
    int32_t idx2;
    
    if (it2 == entry_map.end()) {
        // New entry - add to batch data
        idx2 = static_cast<int32_t>(batch_data.size());
        entry_map[ptr2] = idx2;
        batch_data.push_back(e2.to_entry_t());
        DEBUG_TRACE("BatchCollector: Added new entry at index %d", idx2);
    } else {
        // Existing entry - use its index
        idx2 = it2->second;
        DEBUG_TRACE("BatchCollector: Reusing entry at index %d", idx2);
    }
    
    // Add operation with parameters
    BatchOperation op;
    op.idx1 = idx1;
    op.idx2 = idx2;
    // Copy parameters if provided
    for (int i = 0; i < MAX_EXTRA_PARAMS; i++) {
        op.extra_params[i] = params ? params[i] : BATCH_NO_PARAM;
    }
    operations.push_back(op);
    stats.total_operations++;
    
    DEBUG_TRACE("BatchCollector: Added operation (%d, %d) - batch size now %zu", 
                idx1, idx2, operations.size());
    
    // Auto-flush if needed
    if (needs_flush()) {
        DEBUG_DEBUG("BatchCollector: Auto-flushing at batch size %zu", operations.size());
        flush();
    }
}

void EcallBatchCollector::add_operation(Entry& e, uint32_t extra_param) {
    // Deduplicate entry
    Entry* ptr = &e;
    auto it = entry_map.find(ptr);
    uint32_t idx;
    
    if (it == entry_map.end()) {
        // New entry - add to batch data
        idx = static_cast<uint32_t>(batch_data.size());
        entry_map[ptr] = idx;
        batch_data.push_back(e.to_entry_t());
        DEBUG_TRACE("BatchCollector: Added new entry at index %d", idx);
    } else {
        // Existing entry - use its index
        idx = it->second;
        DEBUG_TRACE("BatchCollector: Reusing entry at index %d", idx);
    }
    
    // Add single-parameter operation
    BatchOperation op;
    op.idx1 = idx;
    op.idx2 = BATCH_NO_PARAM;
    // Copy parameters if provided
    for (int i = 0; i < MAX_EXTRA_PARAMS; i++) {
        op.extra_params[i] = params ? params[i] : BATCH_NO_PARAM;
    }
    operations.push_back(op);
    stats.total_operations++;
    
    DEBUG_TRACE("BatchCollector: Added single-param operation (%d) - batch size now %zu", 
                idx, operations.size());
    
    // Auto-flush if needed
    if (needs_flush()) {
        DEBUG_DEBUG("BatchCollector: Auto-flushing at batch size %zu", operations.size());
        flush();
    }
}

void EcallBatchCollector::flush() {
    if (operations.empty()) {
        DEBUG_TRACE("BatchCollector: Flush called but no operations pending");
        return;
    }
    
    DEBUG_DEBUG("BatchCollector: Flushing %zu operations with %zu unique entries", 
                operations.size(), batch_data.size());
    
    // Update max batch size statistic
    if (operations.size() > stats.max_batch_size_reached) {
        stats.max_batch_size_reached = operations.size();
    }
    
    // Call the batch dispatcher ecall
    sgx_status_t status = ecall_batch_dispatcher(
        eid,
        batch_data.data(),
        batch_data.size(),
        operations.data(),
        operations.size(),
        op_type
    );
    
    check_sgx_status(status, "BatchDispatcher ecall");
    
    DEBUG_DEBUG("BatchCollector: Batch dispatcher returned successfully");
    
    // Write back results to original Entry objects
    for (const auto& pair : entry_map) {
        Entry* entry_ptr = pair.first;
        uint32_t batch_idx = pair.second;
        
        // Copy modified data back to original Entry
        entry_ptr->from_entry_t(batch_data[batch_idx]);
        
        DEBUG_TRACE("BatchCollector: Wrote back entry at batch index %u", batch_idx);
    }
    
    // Update statistics
    stats.total_flushes++;
    stats.total_entries_processed += batch_data.size();
    
    DEBUG_DEBUG("BatchCollector: Flush complete. Clearing batch for next operations");
    
    // Clear for next batch
    entry_map.clear();
    batch_data.clear();
    operations.clear();
}