/**
 * Benchmark: OrShuffle vs Waksman Shuffle
 *
 * Compares the performance of the new OrShuffle algorithm against
 * the old Waksman permutation network-based shuffle.
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <cstring>

#include "../../common/enclave_types.h"
#include "../../common/constants.h"
#include "../../app/algorithms/or_shuffle.hpp"

// Forward declaration for Waksman (will link against old implementation)
extern "C" {
    int oblivious_2way_waksman(entry_t* data, size_t n);
}

// Helper to generate random entries
void generate_random_entries(std::vector<entry_t>& entries, size_t n, std::mt19937& rng) {
    entries.resize(n);
    std::uniform_int_distribution<int32_t> dist(-1000000, 1000000);

    for (size_t i = 0; i < n; i++) {
        entries[i].join_attr = dist(rng);
        entries[i].original_index = static_cast<int32_t>(i);
        entries[i].field_type = 1;  // SOURCE
        entries[i].local_mult = 1;
        entries[i].final_mult = 1;
        for (int j = 0; j < MAX_ATTRIBUTES; j++) {
            entries[i].attributes[j] = dist(rng);
        }
    }
}

// Verify shuffle correctness (all original elements present)
bool verify_shuffle(const std::vector<entry_t>& original, const std::vector<entry_t>& shuffled) {
    if (original.size() != shuffled.size()) return false;

    std::vector<bool> found(original.size(), false);
    for (size_t i = 0; i < shuffled.size(); i++) {
        int32_t idx = shuffled[i].original_index;
        if (idx < 0 || idx >= static_cast<int32_t>(original.size())) return false;
        if (found[idx]) return false;  // Duplicate
        found[idx] = true;
    }

    for (bool f : found) {
        if (!f) return false;
    }
    return true;
}

// Check if shuffle actually changed the order (not identity permutation)
double compute_displacement(const std::vector<entry_t>& shuffled) {
    size_t displaced = 0;
    for (size_t i = 0; i < shuffled.size(); i++) {
        if (shuffled[i].original_index != static_cast<int32_t>(i)) {
            displaced++;
        }
    }
    return 100.0 * displaced / shuffled.size();
}

// Get next power of 2
size_t next_power_of_2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

struct BenchmarkResult {
    size_t size;
    double waksman_time_ms;
    double orshuffle_time_ms;
    double speedup;
    bool waksman_valid;
    bool orshuffle_valid;
    double waksman_displacement;
    double orshuffle_displacement;
};

int main(int argc, char* argv[]) {
    std::cout << "=== Shuffle Benchmark: OrShuffle vs Waksman ===" << std::endl;
    std::cout << "entry_t size: " << sizeof(entry_t) << " bytes" << std::endl;
    std::cout << std::endl;

    // Test sizes (powers of 2 for Waksman compatibility)
    std::vector<size_t> sizes = {64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};

    // Filter sizes that exceed MAX_BATCH_SIZE
    std::vector<size_t> valid_sizes;
    for (size_t s : sizes) {
        if (s <= MAX_BATCH_SIZE) {
            valid_sizes.push_back(s);
        }
    }

    std::cout << "MAX_BATCH_SIZE: " << MAX_BATCH_SIZE << std::endl;
    std::cout << std::endl;

    const int NUM_ITERATIONS = 5;  // Iterations per size for averaging
    const int WARMUP_ITERATIONS = 2;

    std::mt19937 rng(42);  // Fixed seed for reproducibility

    std::vector<BenchmarkResult> results;

    std::cout << std::setw(10) << "Size"
              << std::setw(15) << "Waksman(ms)"
              << std::setw(15) << "OrShuffle(ms)"
              << std::setw(12) << "Speedup"
              << std::setw(10) << "W_Valid"
              << std::setw(10) << "O_Valid"
              << std::setw(12) << "W_Disp%"
              << std::setw(12) << "O_Disp%"
              << std::endl;
    std::cout << std::string(96, '-') << std::endl;

    for (size_t n : valid_sizes) {
        BenchmarkResult result;
        result.size = n;

        // Generate test data
        std::vector<entry_t> original;
        generate_random_entries(original, n, rng);

        // Warmup
        for (int i = 0; i < WARMUP_ITERATIONS; i++) {
            std::vector<entry_t> test_data = original;
            oblivious_2way_waksman(test_data.data(), n);

            test_data = original;
            OrShuffle::or_shuffle(test_data.data(), n);
        }

        // Benchmark Waksman
        double waksman_total = 0.0;
        bool waksman_valid = true;
        double waksman_disp = 0.0;

        for (int i = 0; i < NUM_ITERATIONS; i++) {
            std::vector<entry_t> test_data = original;

            auto start = std::chrono::high_resolution_clock::now();
            int ret = oblivious_2way_waksman(test_data.data(), n);
            auto end = std::chrono::high_resolution_clock::now();

            if (ret != 0) waksman_valid = false;
            if (!verify_shuffle(original, test_data)) waksman_valid = false;
            waksman_disp += compute_displacement(test_data);

            waksman_total += std::chrono::duration<double, std::milli>(end - start).count();
        }
        result.waksman_time_ms = waksman_total / NUM_ITERATIONS;
        result.waksman_valid = waksman_valid;
        result.waksman_displacement = waksman_disp / NUM_ITERATIONS;

        // Benchmark OrShuffle
        double orshuffle_total = 0.0;
        bool orshuffle_valid = true;
        double orshuffle_disp = 0.0;

        for (int i = 0; i < NUM_ITERATIONS; i++) {
            std::vector<entry_t> test_data = original;

            auto start = std::chrono::high_resolution_clock::now();
            int ret = OrShuffle::or_shuffle(test_data.data(), n);
            auto end = std::chrono::high_resolution_clock::now();

            if (ret != 0) orshuffle_valid = false;
            if (!verify_shuffle(original, test_data)) orshuffle_valid = false;
            orshuffle_disp += compute_displacement(test_data);

            orshuffle_total += std::chrono::duration<double, std::milli>(end - start).count();
        }
        result.orshuffle_time_ms = orshuffle_total / NUM_ITERATIONS;
        result.orshuffle_valid = orshuffle_valid;
        result.orshuffle_displacement = orshuffle_disp / NUM_ITERATIONS;

        result.speedup = result.waksman_time_ms / result.orshuffle_time_ms;

        results.push_back(result);

        std::cout << std::setw(10) << n
                  << std::setw(15) << std::fixed << std::setprecision(3) << result.waksman_time_ms
                  << std::setw(15) << std::fixed << std::setprecision(3) << result.orshuffle_time_ms
                  << std::setw(12) << std::fixed << std::setprecision(2) << result.speedup << "x"
                  << std::setw(10) << (result.waksman_valid ? "YES" : "NO")
                  << std::setw(10) << (result.orshuffle_valid ? "YES" : "NO")
                  << std::setw(12) << std::fixed << std::setprecision(1) << result.waksman_displacement
                  << std::setw(12) << std::fixed << std::setprecision(1) << result.orshuffle_displacement
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Summary ===" << std::endl;

    double total_waksman = 0, total_orshuffle = 0;
    for (const auto& r : results) {
        total_waksman += r.waksman_time_ms;
        total_orshuffle += r.orshuffle_time_ms;
    }

    std::cout << "Total Waksman time: " << std::fixed << std::setprecision(3) << total_waksman << " ms" << std::endl;
    std::cout << "Total OrShuffle time: " << std::fixed << std::setprecision(3) << total_orshuffle << " ms" << std::endl;
    std::cout << "Overall speedup: " << std::fixed << std::setprecision(2) << (total_waksman / total_orshuffle) << "x" << std::endl;

    // Test non-power-of-2 sizes (OrShuffle only)
    std::cout << std::endl;
    std::cout << "=== OrShuffle Non-Power-of-2 Sizes (OrShuffle only) ===" << std::endl;
    std::vector<size_t> non_pow2_sizes = {100, 500, 1000, 1500, 3000, 5000, 7500, 10000};

    std::cout << std::setw(10) << "Size"
              << std::setw(15) << "OrShuffle(ms)"
              << std::setw(10) << "Valid"
              << std::setw(12) << "Disp%"
              << std::endl;
    std::cout << std::string(47, '-') << std::endl;

    for (size_t n : non_pow2_sizes) {
        if (n > MAX_BATCH_SIZE) continue;

        std::vector<entry_t> original;
        generate_random_entries(original, n, rng);

        // Warmup
        for (int i = 0; i < WARMUP_ITERATIONS; i++) {
            std::vector<entry_t> test_data = original;
            OrShuffle::or_shuffle(test_data.data(), n);
        }

        double total = 0.0;
        bool valid = true;
        double disp = 0.0;

        for (int i = 0; i < NUM_ITERATIONS; i++) {
            std::vector<entry_t> test_data = original;

            auto start = std::chrono::high_resolution_clock::now();
            int ret = OrShuffle::or_shuffle(test_data.data(), n);
            auto end = std::chrono::high_resolution_clock::now();

            if (ret != 0) valid = false;
            if (!verify_shuffle(original, test_data)) valid = false;
            disp += compute_displacement(test_data);

            total += std::chrono::duration<double, std::milli>(end - start).count();
        }

        double avg_time = total / NUM_ITERATIONS;
        double avg_disp = disp / NUM_ITERATIONS;

        std::cout << std::setw(10) << n
                  << std::setw(15) << std::fixed << std::setprecision(3) << avg_time
                  << std::setw(10) << (valid ? "YES" : "NO")
                  << std::setw(12) << std::fixed << std::setprecision(1) << avg_disp
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Legend:" << std::endl;
    std::cout << "  W_Valid/O_Valid: Shuffle correctness (all elements preserved)" << std::endl;
    std::cout << "  Disp%: Percentage of elements displaced from original position" << std::endl;
    std::cout << "         (Higher is better - indicates effective shuffling)" << std::endl;

    return 0;
}
