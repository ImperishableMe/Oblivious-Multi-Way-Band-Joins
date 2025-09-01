#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path
import json

def run_command(cmd, cwd=None):
    """Run a command and return its output"""
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Error: {result.stderr}")
        return False
    print(result.stdout)
    return True

def main():
    test_path = Path(__file__).parent.parent
    scripts_path = test_path / "scripts"
    
    print("=" * 80)
    print("ENCRYPTED DATASET OBLIVIOUS PATTERN VERIFICATION")
    print("=" * 80)
    
    # Step 1: Encrypt all datasets
    print("\n1. Encrypting datasets...")
    print("-" * 40)
    encrypt_script = scripts_path / "encrypt_all_datasets.sh"
    if not run_command(["bash", str(encrypt_script)]):
        print("Failed to encrypt datasets")
        return 1
    
    # Step 2: Collect traces for encrypted datasets
    print("\n2. Collecting memory traces for encrypted datasets...")
    print("-" * 40)
    
    queries = ["tb1", "tb2", "tm1", "tm2", "tm3"]
    
    for query in queries:
        print(f"\nCollecting traces for {query}...")
        
        # Collect trace for encrypted dataset A
        cmd = ["python3", str(scripts_path / "collect_traces.py"), 
               "--query", query, "--dataset", "dataset_A", "--encrypted"]
        if not run_command(cmd):
            print(f"Failed to collect trace for encrypted dataset_A/{query}")
            continue
            
        # Collect trace for encrypted dataset B
        cmd = ["python3", str(scripts_path / "collect_traces.py"), 
               "--query", query, "--dataset", "dataset_B", "--encrypted"]
        if not run_command(cmd):
            print(f"Failed to collect trace for encrypted dataset_B/{query}")
            continue
    
    # Step 3: Compare traces
    print("\n3. Comparing memory access patterns...")
    print("-" * 40)
    
    comparison_results = {}
    for query in queries:
        print(f"\nComparing traces for {query}...")
        
        trace_a = test_path / "traces" / f"encrypted_{query}_dataset_A.trace"
        trace_b = test_path / "traces" / f"encrypted_{query}_dataset_B.trace"
        
        if not trace_a.exists() or not trace_b.exists():
            print(f"Missing trace files for {query}")
            continue
            
        cmd = ["python3", str(scripts_path / "compare_traces.py"),
               str(trace_a), str(trace_b), "--output", f"encrypted_{query}_comparison.json"]
        
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            print(result.stdout)
            # Parse the comparison results
            output_file = test_path / "comparisons" / f"encrypted_{query}_comparison.json"
            if output_file.exists():
                with open(output_file) as f:
                    comparison_results[query] = json.load(f)
        else:
            print(f"Comparison failed: {result.stderr}")
    
    # Step 4: Generate summary report
    print("\n" + "=" * 80)
    print("VERIFICATION SUMMARY")
    print("=" * 80)
    
    if comparison_results:
        all_oblivious = True
        for query, results in comparison_results.items():
            chi_square = results.get("chi_square", {})
            ks_test = results.get("ks_test", {})
            
            chi_p = chi_square.get("p_value", 0)
            ks_p = ks_test.get("p_value", 0)
            
            # High p-values (> 0.05) indicate patterns are similar (oblivious)
            is_oblivious = chi_p > 0.05 and ks_p > 0.05
            
            print(f"\n{query.upper()}:")
            print(f"  Chi-square p-value: {chi_p:.4f}")
            print(f"  KS test p-value: {ks_p:.4f}")
            print(f"  Pattern similarity: {results.get('pattern_similarity', 0):.2%}")
            print(f"  Verdict: {'✓ OBLIVIOUS' if is_oblivious else '✗ NOT OBLIVIOUS'}")
            
            if not is_oblivious:
                all_oblivious = False
        
        print("\n" + "=" * 80)
        if all_oblivious:
            print("✓ ALL QUERIES DEMONSTRATE OBLIVIOUS BEHAVIOR")
        else:
            print("✗ SOME QUERIES SHOW NON-OBLIVIOUS PATTERNS")
        print("=" * 80)
    else:
        print("No comparison results available")
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main())