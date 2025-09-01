# Memory Access Pattern Verification for Oblivious Algorithms

This framework verifies that algorithms maintain oblivious memory access patterns - i.e., their memory access sequences are independent of input data values.

## Overview

The verification process:
1. Generate two datasets (A and B) with different data distributions but identical join output sizes
2. Collect memory access traces using Valgrind's Lackey tool
3. Compare traces to verify they are statistically identical
4. Report whether the algorithm is oblivious

## Directory Structure

```
test_access_patterns/
├── dataset_A/           # Uniform distribution datasets
├── dataset_B/           # Skewed distribution datasets  
├── scripts/             # Analysis scripts
│   ├── collect_traces.py      # Trace collection with Valgrind
│   ├── compare_traces.py      # Statistical comparison
│   └── test_trace_collection.py # Environment verification
├── traces/              # Collected memory traces
│   ├── raw/            # Raw Valgrind output
│   └── processed/      # Processed JSON format
└── reports/            # Analysis reports
    ├── trace_comparison_report.json
    └── summary_report.html
```

## Quick Start

Run the complete verification workflow:

```bash
python3 run_oblivious_verification.py
```

This will:
1. Verify environment setup
2. Generate test datasets (if needed)
3. Verify datasets have matching output sizes
4. Collect memory traces for all queries
5. Compare traces and generate reports

## Manual Steps

### 1. Generate Test Datasets

```bash
python3 generate_test_datasets.py
```

Creates dataset pairs with:
- Different internal data distributions (uniform vs skewed)
- Identical join output sizes
- Negative padding values for clean separation

### 2. Verify Datasets

```bash
python3 verify_datasets.py
```

Uses SQLite to verify both datasets produce identical output sizes.

### 3. Collect Memory Traces

```bash
# Collect all traces
python3 scripts/collect_traces.py --all

# Or collect specific trace
python3 scripts/collect_traces.py --query tb1 --dataset dataset_A
```

Uses Valgrind Lackey to capture:
- Load (L), Store (S), Modify (M) operations
- Memory addresses and access sizes
- Complete execution traces

### 4. Compare Traces

```bash
# Compare all queries
python3 scripts/compare_traces.py --all --html

# Or compare specific query
python3 scripts/compare_traces.py --query tb1
```

Performs:
- Operation sequence comparison
- Address pattern normalization
- Statistical tests (Chi-square, Kolmogorov-Smirnov)
- Entropy analysis
- Divergence detection

## Verification Criteria

An algorithm is considered **oblivious** if:

1. **Operation sequences match**: Same order of Load/Store/Modify operations
2. **Access patterns match**: Normalized addresses show identical patterns
3. **Statistical tests pass**: p-values > 0.05 (no significant difference)
4. **Pattern similarity > 95%**: High correlation between traces

## Output Reports

### JSON Report (`reports/trace_comparison_report.json`)
Detailed metrics including:
- Operation counts and sequences
- Statistical test results
- Pattern similarity scores
- Divergence analysis

### HTML Report (`reports/summary_report.html`)
Visual summary with:
- Query-by-query results
- Color-coded verdicts (green=oblivious, red=not oblivious)
- Key metrics in tabular format

## Troubleshooting

### Valgrind Not Found
```bash
sudo apt-get install valgrind
```

### SGX App Not Compiled
```bash
cd /home/r33wei/omwj/memory_const/impl/src
make clean && make
```

### Trace Collection Too Slow
Valgrind adds ~10-100x overhead. For faster testing:
- Use smaller datasets
- Test individual queries
- Consider Intel Pin for production use

### Different Operation Counts
If dataset A and B show different operation counts:
- Check dataset generation succeeded
- Verify SGX app handles both datasets correctly
- Look for data-dependent branches in code

## Advanced Usage

### Custom Datasets
Modify `generate_test_datasets.py` to create custom data distributions while maintaining output size constraints.

### Focused Analysis
Modify trace collection to focus on specific functions:
```bash
valgrind --tool=lackey --trace-mem=yes --fn-include="*join*" ./sgx_app
```

### Extended Statistics
Add more statistical tests in `compare_traces.py`:
- Mutual information
- Jensen-Shannon divergence
- Autocorrelation analysis

## Theory Background

**Oblivious algorithms** ensure memory access patterns don't leak information about input data. This is critical for:
- Secure computation in untrusted environments
- Side-channel attack prevention
- Privacy-preserving data processing

The framework verifies this property by checking that different input distributions produce statistically indistinguishable memory access patterns.