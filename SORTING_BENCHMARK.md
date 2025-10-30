# Sorting Algorithm Benchmark

This directory contains tools to benchmark and compare two sorting implementations:

1. **MergeSortManager** (`app/algorithms/merge_sort_manager.cpp`)
   - Non-oblivious k-way merge sort
   - Sequential execution (single-threaded)
   - Optimized for the Table/Entry data structures

2. **ParallelObliviousSort** (`obligraph/src/par_obl_primitives.cpp`)
   - Oblivious bitonic sort variant
   - Parallel execution with configurable thread count
   - Data-oblivious (constant-time operations)

## Quick Start

### Build the Benchmark

```bash
make benchmark_sorting
```

### Run a Simple Test

```bash
# Test with 10,000 entries using up to 8 threads
./benchmark_sorting --size 10000 --threads 8

# Quick test with small datasets
./scripts/run_sort_benchmark.sh --quick

# Full benchmark with multiple sizes
./scripts/run_sort_benchmark.sh --sizes 1000,10000,50000,100000 --threads 16
```

## Benchmark Program: `benchmark_sorting`

### Usage

```bash
./benchmark_sorting [options]
```

### Options

- `--size <N>` - Single data size (default: 10000)
- `--sizes <N1,N2,...>` - Multiple data sizes (comma-separated)
- `--dist <TYPE>` - Distribution: random, sorted, reverse, nearly (default: random)
- `--threads <N>` - Maximum threads for parallel sort (default: 8)
- `--thread-list <N1,N2,...>` - Specific thread counts to test (comma-separated)
- `--csv` - Output in CSV format
- `--verbose` - Verbose output
- `--help` - Show help message

### Examples

```bash
# Compare with different thread counts
./benchmark_sorting --size 100000 --thread-list 1,2,4,8,16,32

# Test multiple distributions
for dist in random sorted reverse nearly; do
    ./benchmark_sorting --size 50000 --dist $dist --csv > results_${dist}.csv
done

# CSV output for analysis
./benchmark_sorting --sizes 10000,50000,100000 --threads 16 --csv > results.csv
```

## Benchmark Script: `scripts/run_sort_benchmark.sh`

Comprehensive benchmarking script that:
- Tests multiple data sizes and distributions
- Generates CSV files for each distribution
- Creates combined results file
- Produces text report
- Calculates speedup analysis (if Python is available)

### Usage

```bash
./scripts/run_sort_benchmark.sh [options]
```

### Options

- `--sizes <N1,N2,...>` - Data sizes to test (default: 1000,10000,50000,100000)
- `--threads <N>` - Maximum threads (default: 16)
- `--output <DIR>` - Output directory (default: benchmark_results)
- `--quick` - Quick test with small sizes (1000, 10000) and 4 threads
- `--help` - Show help

### Examples

```bash
# Quick test
./scripts/run_sort_benchmark.sh --quick

# Custom sizes and thread counts
./scripts/run_sort_benchmark.sh --sizes 10000,100000,500000 --threads 32

# Save to specific directory
./scripts/run_sort_benchmark.sh --output my_benchmark_results
```

### Output Files

The script creates the following files in the output directory:

- `benchmark_<dist>_<timestamp>.csv` - Individual results for each distribution
- `benchmark_all_<timestamp>.csv` - Combined results from all distributions
- `report_<timestamp>.txt` - Human-readable text report
- `speedup_analysis_<timestamp>.txt` - Speedup analysis (if Python available)

## Data Distributions

The benchmark tests four different data distributions:

1. **Random** - Uniformly random integers
2. **Sorted** - Already sorted in ascending order
3. **Reverse** - Sorted in descending order (worst case for some algorithms)
4. **Nearly Sorted** - Sorted with 5% of elements randomly swapped

## Interpreting Results

### Key Metrics

- **Time_ms**: Execution time in milliseconds
- **Verified**: Whether the output is correctly sorted
- **Speedup**: ParallelObliviousSort time / MergeSortManager time

### Sample Results (from quick test)

For 10,000 entries on random data:

| Algorithm | Threads | Time (ms) | vs MergeSortManager |
|-----------|---------|-----------|---------------------|
| MergeSortManager | 1 | 13.61 | Baseline |
| ParallelObliviousSort | 1 | 65.48 | 4.8x slower |
| ParallelObliviousSort | 2 | 39.40 | 2.9x slower |
| ParallelObliviousSort | 4 | 26.18 | 1.9x slower |

### Key Observations

1. **MergeSortManager is faster for small-medium datasets**
   - Lower constant factors
   - No parallelization overhead
   - Better cache locality

2. **ParallelObliviousSort characteristics**:
   - Data-oblivious (constant-time guarantee for security)
   - Scales well with thread count
   - Higher overhead due to oblivious operations
   - More suitable for large datasets on multi-core systems

3. **Parallel Scaling**:
   - For 10K entries with 4 threads: ~2.5x speedup over 1 thread
   - Speedup increases with larger datasets
   - Diminishing returns beyond optimal thread count

## Extending the Benchmark

### Adding New Data Sizes

Edit the default sizes in the script or use command-line options:

```bash
./scripts/run_sort_benchmark.sh --sizes 1000,5000,10000,50000,100000,500000
```

### Testing Different Thread Configurations

```bash
# Test specific thread counts
./benchmark_sorting --size 100000 --thread-list 1,2,4,8,12,16,24,32
```

### Analyzing Results

The CSV output can be imported into spreadsheet software or analyzed with Python/R:

```python
import pandas as pd
import matplotlib.pyplot as plt

# Load results
df = pd.read_csv('benchmark_results/benchmark_all_20251020_224710.csv')

# Plot parallel scaling
subset = df[(df['Algorithm'] == 'ParallelObliviousSort') & (df['DataSize'] == 100000)]
plt.plot(subset['Threads'], subset['Time_ms'], marker='o')
plt.xlabel('Thread Count')
plt.ylabel('Time (ms)')
plt.title('Parallel Scaling - 100K entries')
plt.grid(True)
plt.show()
```

## Performance Tuning

### For MergeSortManager:

- Adjust `MAX_BATCH_SIZE` in constants.h
- Tune k-way merge parameter (`MERGE_SORT_K`)
- Consider enabling compiler optimizations (`-O3`)

### For ParallelObliviousSort:

- Optimal thread count typically equals CPU core count
- Overhead diminishes with larger datasets
- Consider NUMA effects on multi-socket systems

## Troubleshooting

### Compilation Errors

```bash
# Ensure C++20 support
g++ --version  # Should be GCC 9+ or Clang 10+

# Clean rebuild
make clean
make benchmark_sorting
```

### Verification Failures

If `Verified=No` appears in results:
1. Check that both algorithms use the same comparator
2. Verify Entry structure is correctly initialized
3. Look for size mismatches in debug output

### Performance Issues

- Disable debug output (ensure `DEBUG=0`)
- Use release build optimizations (`-O2` or `-O3`)
- Check system load and CPU frequency scaling
- Consider CPU pinning for more consistent results

## References

- MergeSortManager: `app/algorithms/merge_sort_manager.cpp`
- ParallelObliviousSort: `obligraph/include/obl_building_blocks.h`
- Test program: `tests/performance/benchmark_sorting.cpp`
- Benchmark script: `scripts/run_sort_benchmark.sh`
