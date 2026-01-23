/**
 * Oblivious Hashmap Example
 *
 * This example demonstrates how to use the oblivious hashmap implementations
 * extracted from the H2O2RAM project.
 *
 * Key Classes:
 * - OTwoTierHash: Two-tier oblivious hash table (recommended for most use cases)
 * - OCuckooHash: Oblivious cuckoo hashing
 * - OHashBucket: Bucket-based oblivious hash
 * - OLinearScan: Linear scan for small datasets
 */

#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <cassert>

#include "types.hpp"
#include "hash_planner.hpp"  // Must include to get determine_hash template definition
#include "ohash_tiers.hpp"
#include "olinear_scan.hpp"
#include "timer.hpp"

void example_basic_usage() {
    std::cout << "=== Basic Oblivious Hash Table Usage ===" << std::endl;

    std::random_device rd;
    std::mt19937 gen(rd());

    constexpr uint32_t n = 1024;  // Number of elements (must be power of 2)
    constexpr size_t BlockSize = 512;  // Block size in bytes

    // Create data blocks
    std::vector<ORAM::Block<uint32_t, BlockSize>> data(n);
    for (uint32_t i = 0; i < n; i++) {
        data[i].id = i;
        // Optionally set value data
        // data[i].value can store up to (BlockSize - sizeof(uint32_t)) bytes
    }

    // Shuffle the data (simulating random order)
    std::shuffle(data.begin(), data.end(), gen);

    // Create oblivious hash table
    ORAM::OTwoTierHash<uint32_t, BlockSize> hash_table(n);

    // Build the hash table
    Timer timer;
    hash_table.build(data.data());
    auto build_time = timer.get_interval_time();
    std::cout << "Build time: " << build_time << " seconds" << std::endl;

    // Perform oblivious lookups
    for (uint32_t i = 0; i < n; i++) {
        auto result = hash_table[i];
        assert(result.id == i);  // Verify lookup correctness
    }
    auto lookup_time = timer.get_interval_time();
    std::cout << "Lookup time for " << n << " queries: " << lookup_time << " seconds" << std::endl;

    // Dummy lookups (for non-existent keys)
    auto dummy_result = hash_table[static_cast<uint32_t>(-1)];
    assert(dummy_result.dummy() == true);  // Returns dummy block
    std::cout << "Dummy lookup works correctly" << std::endl;

    // Extract all data (sorted by id)
    auto extracted = hash_table.extract();
    std::cout << "Extracted " << extracted.size() << " elements" << std::endl;

    std::cout << std::endl;
}

void example_linear_scan() {
    std::cout << "=== OLinearScan (For Small Datasets) ===" << std::endl;

    std::random_device rd;
    std::mt19937 gen(rd());

    constexpr uint32_t n = 64;  // Small dataset
    constexpr size_t BlockSize = 512;

    std::vector<ORAM::Block<uint32_t, BlockSize>> data(n);
    for (uint32_t i = 0; i < n; i++) {
        data[i].id = i;
    }
    std::shuffle(data.begin(), data.end(), gen);

    // OLinearScan is efficient for very small datasets (< LINEAR_SCAN_THRESHOLD)
    ORAM::OLinearScan<uint32_t, BlockSize> linear_scan(n);
    linear_scan.build(data.data());

    // Perform lookups
    for (uint32_t i = 0; i < n; i++) {
        auto result = linear_scan[i];
        assert(result.id == i);
    }

    std::cout << "OLinearScan test passed with " << n << " elements" << std::endl;
    std::cout << std::endl;
}

void example_large_scale() {
    std::cout << "=== Large Scale Test ===" << std::endl;

    std::random_device rd;
    std::mt19937 gen(rd());

    // Using a dataset that uses the two-tier hash (must be power of 2)
    constexpr uint32_t n = 4096;  // Power of 2
    constexpr size_t BlockSize = 512;

    std::cout << "Creating " << n << " elements with block size " << BlockSize << std::endl;

    std::vector<ORAM::Block<uint32_t, BlockSize>> data(n);
    for (uint32_t i = 0; i < n; i++) {
        data[i].id = i;
    }
    std::shuffle(data.begin(), data.end(), gen);

    Timer timer;
    ORAM::OTwoTierHash<uint32_t, BlockSize> hash_table(n);
    hash_table.build(data.data());
    std::cout << "Build time: " << timer.get_interval_time() << " seconds" << std::endl;

    // Lookup each key exactly once (oblivious hash semantics)
    // Note: Each key can only be looked up once before extract()
    for (uint32_t i = 0; i < n; i++) {
        auto result = hash_table[i];
        assert(result.id == i);
    }
    std::cout << "Lookup time for " << n << " sequential queries: "
              << timer.get_interval_time() << " seconds" << std::endl;

    // After all lookups, all data is returned as dummy
    // Use extract() to get remaining data sorted by id

    // Re-build for extraction test
    for (uint32_t i = 0; i < n; i++) {
        data[i].id = i;
    }
    std::shuffle(data.begin(), data.end(), gen);
    hash_table.build(data.data());

    auto extracted = hash_table.extract();
    std::cout << "Extract time: " << timer.get_interval_time() << " seconds" << std::endl;

    // Verify extraction
    std::sort(extracted.begin(), extracted.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    for (uint32_t i = 0; i < n; i++) {
        assert(extracted[i].id == i);
    }
    std::cout << "Extraction verified successfully" << std::endl;
    std::cout << std::endl;
}

void print_usage_summary() {
    std::cout << "=== API Summary ===" << std::endl;
    std::cout << R"(
Key Types:
    ORAM::Block<KeyType, BlockSize>  - Basic data block with id and value

Hash Table Implementations:
    ORAM::OTwoTierHash<KeyType, BlockSize>  - Two-tier hash (recommended for most cases)
    ORAM::OCuckooHash<KeyType, BlockSize>   - Cuckoo hashing
    ORAM::OHashBucket<KeyType, BlockSize>   - Bucket-based hash
    ORAM::OLinearScan<KeyType, BlockSize>   - Linear scan (for small datasets)

Common Methods:
    hash_table.build(data_ptr)     - Build hash table from array
    hash_table[key]                - Oblivious lookup (returns Block)
    hash_table.extract()           - Extract all data (returns vector)
    block.dummy()                  - Check if block is a dummy

Constants (from types.hpp):
    LINEAR_SCAN_THRESHOLD = 128
    MAJOR_BIN_SIZE = 65536
    EPSILON_INV = 8
    DELTA_INV_LOG2 = 64  (security parameter: 2^-64 failure probability)
)" << std::endl;
}

int main() {
    std::cout << "Oblivious Hashmap Library Example" << std::endl;
    std::cout << "==================================" << std::endl << std::endl;

    example_basic_usage();
    example_linear_scan();
    example_large_scale();
    print_usage_summary();

    std::cout << "All examples completed successfully!" << std::endl;
    return 0;
}
