#!/bin/bash
#
# Comprehensive Sorting Benchmark Script
# Compares MergeSortManager vs ParallelObliviousSort
#

# Default parameters
SIZES="1000,10000,50000,100000"
DISTRIBUTIONS="random sorted reverse nearly"
MAX_THREADS=16
OUTPUT_DIR="benchmark_results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --sizes)
            SIZES="$2"
            shift 2
            ;;
        --threads)
            MAX_THREADS="$2"
            shift 2
            ;;
        --output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --quick)
            SIZES="1000,10000"
            MAX_THREADS=4
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --sizes <N1,N2,...>   Data sizes to test (default: 1000,10000,50000,100000)"
            echo "  --threads <N>         Maximum threads (default: 16)"
            echo "  --output <DIR>        Output directory (default: benchmark_results)"
            echo "  --quick               Quick test with small sizes"
            echo "  --help                Show this help"
            echo ""
            echo "Examples:"
            echo "  $0 --quick"
            echo "  $0 --sizes 10000,100000,500000 --threads 32"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if benchmark_sorting exists
if [[ ! -x "./benchmark_sorting" ]]; then
    echo "Error: benchmark_sorting executable not found"
    echo "Please build it first with: make benchmark_sorting"
    exit 1
fi

echo "======================================"
echo "Sorting Algorithm Benchmark"
echo "======================================"
echo "Timestamp: $TIMESTAMP"
echo "Data sizes: $SIZES"
echo "Distributions: $DISTRIBUTIONS"
echo "Max threads: $MAX_THREADS"
echo "Output directory: $OUTPUT_DIR"
echo ""

# Run benchmarks for each distribution
for dist in $DISTRIBUTIONS; do
    echo "Running benchmark for distribution: $dist"

    output_file="$OUTPUT_DIR/benchmark_${dist}_${TIMESTAMP}.csv"

    ./benchmark_sorting \
        --sizes "$SIZES" \
        --dist "$dist" \
        --threads "$MAX_THREADS" \
        --csv > "$output_file"

    if [[ $? -eq 0 ]]; then
        echo "  ✓ Results saved to: $output_file"
    else
        echo "  ✗ Benchmark failed for distribution: $dist"
    fi
done

# Create combined results file
combined_file="$OUTPUT_DIR/benchmark_all_${TIMESTAMP}.csv"
echo "Combining results into: $combined_file"

# Write header
echo "Algorithm,DataSize,Distribution,Threads,Time_ms,Verified" > "$combined_file"

# Combine all CSV files (skip headers)
for dist in $DISTRIBUTIONS; do
    csv_file="$OUTPUT_DIR/benchmark_${dist}_${TIMESTAMP}.csv"
    if [[ -f "$csv_file" ]]; then
        tail -n +2 "$csv_file" >> "$combined_file"
    fi
done

echo ""
echo "======================================"
echo "Benchmark Complete!"
echo "======================================"
echo ""
echo "Results summary:"
echo "  - Individual distribution results: $OUTPUT_DIR/benchmark_*_${TIMESTAMP}.csv"
echo "  - Combined results: $combined_file"
echo ""

# Generate simple text report
report_file="$OUTPUT_DIR/report_${TIMESTAMP}.txt"
echo "Sorting Benchmark Report - $TIMESTAMP" > "$report_file"
echo "========================================" >> "$report_file"
echo "" >> "$report_file"

for dist in $DISTRIBUTIONS; do
    csv_file="$OUTPUT_DIR/benchmark_${dist}_${TIMESTAMP}.csv"
    if [[ -f "$csv_file" ]]; then
        echo "Distribution: $dist" >> "$report_file"
        echo "----------------------------" >> "$report_file"

        # Extract MergeSortManager times
        echo "MergeSortManager:" >> "$report_file"
        grep "MergeSortManager" "$csv_file" | while IFS=, read -r algo size dist threads time verified; do
            printf "  Size=%s: %.2f ms\n" "$size" "$time" >> "$report_file"
        done

        echo "" >> "$report_file"
        echo "ParallelObliviousSort (by thread count):" >> "$report_file"

        # Extract parallel sort times for each size
        for size in $(echo "$SIZES" | tr ',' ' '); do
            echo "  Size=$size:" >> "$report_file"
            grep "ParallelObliviousSort,$size" "$csv_file" | while IFS=, read -r algo sz dist threads time verified; do
                printf "    %2d threads: %.2f ms\n" "$threads" "$time" >> "$report_file"
            done
        done

        echo "" >> "$report_file"
    fi
done

echo "Text report saved to: $report_file"
echo ""

# Optional: Try to generate plots if Python is available
if command -v python3 &> /dev/null; then
    echo "Attempting to generate plots with Python..."

    plot_script="$OUTPUT_DIR/plot_results.py"
    cat > "$plot_script" << 'EOF'
#!/usr/bin/env python3
import sys
import csv
from collections import defaultdict

def load_data(filename):
    data = defaultdict(lambda: defaultdict(list))
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            algo = row['Algorithm']
            size = int(row['DataSize'])
            dist = row['Distribution']
            threads = int(row['Threads'])
            time = float(row['Time_ms'])

            key = (dist, size)
            data[key][(algo, threads)] = time
    return data

def print_speedup_table(data):
    print("\nSpeedup Analysis (ParallelObliviousSort vs MergeSortManager)")
    print("=" * 80)

    for (dist, size), times in sorted(data.items()):
        merge_time = times.get(('MergeSortManager', 1), None)
        if merge_time is None:
            continue

        print(f"\n{dist.upper()} distribution, Size={size}:")
        print(f"  MergeSortManager baseline: {merge_time:.2f} ms")

        parallel_times = [(threads, time) for (algo, threads), time in times.items()
                          if algo == 'ParallelObliviousSort']

        if parallel_times:
            print("  ParallelObliviousSort:")
            for threads, time in sorted(parallel_times):
                speedup = merge_time / time
                print(f"    {threads:2d} threads: {time:8.2f} ms (speedup: {speedup:5.2f}x)")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 plot_results.py <combined_csv_file>")
        sys.exit(1)

    data = load_data(sys.argv[1])
    print_speedup_table(data)
EOF

    chmod +x "$plot_script"
    python3 "$plot_script" "$combined_file" | tee "$OUTPUT_DIR/speedup_analysis_${TIMESTAMP}.txt"
fi

echo ""
echo "All benchmark tasks complete!"
