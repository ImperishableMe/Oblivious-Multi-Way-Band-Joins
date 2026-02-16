#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>

// App includes
#include "app/data_structures/data_structures.h"
#include "app/algorithms/merge_sort_manager.h"
#include "common/op_types.h"
#include "common/debug_util.h"

// Obligraph includes
#include "obligraph/include/obl_building_blocks.h"

using namespace std;
using namespace obligraph;

// ============================================================================
// Configuration and Data Generation
// ============================================================================

enum DataDistribution {
    RANDOM,
    SORTED,
    REVERSE_SORTED,
    NEARLY_SORTED
};

string distribution_name(DataDistribution dist) {
    switch (dist) {
        case RANDOM: return "Random";
        case SORTED: return "Sorted";
        case REVERSE_SORTED: return "Reverse";
        case NEARLY_SORTED: return "NearlySorted";
        default: return "Unknown";
    }
}

// Create a table with specified distribution
Table create_table_with_distribution(size_t size, DataDistribution dist, unsigned seed = 42) {
    vector<string> schema = {"col1", "col2", "col3"};
    Table table("benchmark_table", schema);

    mt19937 gen(seed);
    uniform_int_distribution<> value_dis(1, 1000000);

    // Generate base data
    vector<int32_t> join_attrs;
    join_attrs.reserve(size);

    for (size_t i = 0; i < size; i++) {
        join_attrs.push_back(value_dis(gen));
    }

    // Apply distribution
    switch (dist) {
        case SORTED:
            sort(join_attrs.begin(), join_attrs.end());
            break;
        case REVERSE_SORTED:
            sort(join_attrs.begin(), join_attrs.end(), greater<int32_t>());
            break;
        case NEARLY_SORTED:
            {
                sort(join_attrs.begin(), join_attrs.end());
                // Swap 5% of elements randomly
                uniform_int_distribution<> swap_dis(0, size - 1);
                for (size_t i = 0; i < size / 20; i++) {
                    size_t idx1 = swap_dis(gen);
                    size_t idx2 = swap_dis(gen);
                    swap(join_attrs[idx1], join_attrs[idx2]);
                }
            }
            break;
        case RANDOM:
        default:
            // Already random
            break;
    }

    // Create entries
    for (size_t i = 0; i < size; i++) {
        Entry entry;
        entry.join_attr = join_attrs[i];
        entry.original_index = i;
        entry.field_type = SOURCE;
        entry.equality_type = EQ;

        for (int j = 0; j < 3; j++) {
            entry.attributes[j] = value_dis(gen);
        }

        table.add_entry(entry);
    }

    return table;
}

// ============================================================================
// Sorting Algorithm Wrappers
// ============================================================================

// Wrapper for MergeSortManager
double benchmark_merge_sort_manager(Table& table) {
    MergeSortManager sorter(OP_ECALL_COMPARATOR_JOIN_ATTR);

    auto start = chrono::high_resolution_clock::now();
    sorter.sort(table);
    auto end = chrono::high_resolution_clock::now();

    return chrono::duration<double, milli>(end - start).count();
}

// Wrapper for parallel oblivious sort
double benchmark_parallel_oblivious_sort(Table& table, size_t num_threads) {
    // Create thread pool
    ThreadPool pool(num_threads);

    // Extract join_attr values into a vector for sorting
    vector<Entry> entries;
    entries.reserve(table.size());
    for (size_t i = 0; i < table.size(); i++) {
        entries.push_back(table[i]);
    }

    // Define comparator
    auto comparator = [](const Entry& a, const Entry& b) {
        return a.join_attr < b.join_attr;
    };

    auto start = chrono::high_resolution_clock::now();
    parallel_sort(entries.begin(), entries.end(), pool, comparator, num_threads);
    auto end = chrono::high_resolution_clock::now();

    // Copy back to table
    table.clear();
    for (const auto& entry : entries) {
        table.add_entry(entry);
    }

    return chrono::duration<double, milli>(end - start).count();
}

// ============================================================================
// Verification
// ============================================================================

bool verify_sorted(const Table& table) {
    for (size_t i = 1; i < table.size(); i++) {
        if (table[i-1].join_attr > table[i].join_attr) {
            cerr << "Verification failed at index " << i << ": "
                 << table[i-1].join_attr << " > " << table[i].join_attr << endl;
            return false;
        }
    }
    return true;
}

// ============================================================================
// Benchmarking Framework
// ============================================================================

struct BenchmarkResult {
    string algorithm;
    size_t data_size;
    DataDistribution distribution;
    size_t num_threads;
    double time_ms;
    bool verified;
};

void print_csv_header() {
    cout << "Algorithm,DataSize,Distribution,Threads,Time_ms,Verified" << endl;
}

void print_result_csv(const BenchmarkResult& result) {
    cout << result.algorithm << ","
         << result.data_size << ","
         << distribution_name(result.distribution) << ","
         << result.num_threads << ","
         << fixed << setprecision(3) << result.time_ms << ","
         << (result.verified ? "Yes" : "No") << endl;
}

void print_result_human(const BenchmarkResult& result) {
    cout << "[" << result.algorithm << "] "
         << "Size=" << result.data_size << ", "
         << "Dist=" << distribution_name(result.distribution) << ", "
         << "Threads=" << result.num_threads << ", "
         << "Time=" << fixed << setprecision(2) << result.time_ms << "ms, "
         << "Verified=" << (result.verified ? "YES" : "NO") << endl;
}

// ============================================================================
// Main Benchmark Runner
// ============================================================================

void run_benchmark(size_t data_size,
                   DataDistribution dist,
                   const vector<size_t>& thread_counts,
                   bool csv_output,
                   bool verbose) {

    if (verbose && !csv_output) {
        cout << "\n========================================" << endl;
        cout << "Benchmark: Size=" << data_size
             << ", Distribution=" << distribution_name(dist) << endl;
        cout << "========================================" << endl;
    }

    // Test MergeSortManager (sequential)
    {
        Table table = create_table_with_distribution(data_size, dist);
        size_t original_size = table.size();

        double time_ms = benchmark_merge_sort_manager(table);
        bool verified = verify_sorted(table) && (table.size() == original_size);

        BenchmarkResult result = {
            "MergeSortManager",
            data_size,
            dist,
            1,  // Sequential (single-threaded)
            time_ms,
            verified
        };

        if (csv_output) {
            print_result_csv(result);
        } else {
            print_result_human(result);
        }
    }

    // Test ParallelObliviousSort with different thread counts
    for (size_t num_threads : thread_counts) {
        Table table = create_table_with_distribution(data_size, dist);
        size_t original_size = table.size();

        double time_ms = benchmark_parallel_oblivious_sort(table, num_threads);
        bool verified = verify_sorted(table) && (table.size() == original_size);

        BenchmarkResult result = {
            "ParallelObliviousSort",
            data_size,
            dist,
            num_threads,
            time_ms,
            verified
        };

        if (csv_output) {
            print_result_csv(result);
        } else {
            print_result_human(result);
        }
    }
}

void print_usage(const char* prog_name) {
    cout << "Usage: " << prog_name << " [options]\n"
         << "\nOptions:\n"
         << "  --size <N>        Data size (default: 10000)\n"
         << "  --sizes <N1,N2..> Multiple data sizes (comma-separated)\n"
         << "  --dist <TYPE>     Distribution: random, sorted, reverse, nearly (default: random)\n"
         << "  --threads <N>     Max threads for parallel sort (default: 8)\n"
         << "  --thread-list <N1,N2..> Specific thread counts (comma-separated)\n"
         << "  --csv             Output in CSV format\n"
         << "  --verbose         Verbose output\n"
         << "  --help            Show this help\n"
         << "\nExamples:\n"
         << "  " << prog_name << " --size 100000 --threads 16\n"
         << "  " << prog_name << " --sizes 1000,10000,100000 --dist random --csv\n"
         << "  " << prog_name << " --size 50000 --thread-list 1,2,4,8,16 --csv > results.csv\n"
         << endl;
}

DataDistribution parse_distribution(const string& str) {
    string lower = str;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "random") return RANDOM;
    if (lower == "sorted") return SORTED;
    if (lower == "reverse") return REVERSE_SORTED;
    if (lower == "nearly") return NEARLY_SORTED;

    cerr << "Unknown distribution: " << str << ", using RANDOM" << endl;
    return RANDOM;
}

vector<size_t> parse_list(const string& str) {
    vector<size_t> result;
    stringstream ss(str);
    string item;
    while (getline(ss, item, ',')) {
        result.push_back(stoul(item));
    }
    return result;
}

int main(int argc, char* argv[]) {
    // Default parameters
    vector<size_t> data_sizes = {10000};
    DataDistribution dist = RANDOM;
    vector<size_t> thread_counts;
    size_t max_threads = 8;
    bool csv_output = false;
    bool verbose = false;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "--size" && i + 1 < argc) {
            data_sizes = {stoul(argv[++i])};
        }
        else if (arg == "--sizes" && i + 1 < argc) {
            data_sizes = parse_list(argv[++i]);
        }
        else if (arg == "--dist" && i + 1 < argc) {
            dist = parse_distribution(argv[++i]);
        }
        else if (arg == "--threads" && i + 1 < argc) {
            max_threads = stoul(argv[++i]);
        }
        else if (arg == "--thread-list" && i + 1 < argc) {
            thread_counts = parse_list(argv[++i]);
        }
        else if (arg == "--csv") {
            csv_output = true;
        }
        else if (arg == "--verbose") {
            verbose = true;
        }
        else {
            cerr << "Unknown option: " << arg << endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Generate default thread counts if not specified
    if (thread_counts.empty()) {
        for (size_t t = 1; t <= max_threads; t *= 2) {
            thread_counts.push_back(t);
        }
        if (thread_counts.back() != max_threads) {
            thread_counts.push_back(max_threads);
        }
    }

    // Print header
    if (csv_output) {
        print_csv_header();
    } else if (!verbose) {
        cout << "Sorting Algorithm Benchmark" << endl;
        cout << "============================" << endl;
    }

    // Run benchmarks
    for (size_t size : data_sizes) {
        run_benchmark(size, dist, thread_counts, csv_output, verbose);
    }

    if (!csv_output) {
        cout << "\nBenchmark complete!" << endl;
    }

    return 0;
}
