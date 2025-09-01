#!/usr/bin/env python3
"""
Main script to run complete oblivious algorithm verification workflow.
"""

import subprocess
import sys
import json
from pathlib import Path
from datetime import datetime

def run_command(cmd, description):
    """Run a command and report status"""
    print(f"\n{'='*60}")
    print(f"{description}")
    print('='*60)
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, shell=True)
        
        if result.returncode == 0:
            print(result.stdout)
            return True
        else:
            print(f"Error: {result.stderr}")
            return False
    except Exception as e:
        print(f"Exception: {e}")
        return False

def main():
    base_path = Path("/home/r33wei/omwj/memory_const/test_access_patterns")
    
    print("\n" + "="*70)
    print(" OBLIVIOUS ALGORITHM VERIFICATION WORKFLOW ".center(70))
    print("="*70)
    print(f"Started at: {datetime.now().isoformat()}")
    
    # Step 1: Verify environment
    print("\n[Step 1/5] Verifying environment...")
    if not run_command(
        "python3 scripts/test_trace_collection.py",
        "Testing trace collection environment"
    ):
        print("✗ Environment verification failed. Please fix issues and retry.")
        sys.exit(1)
    
    # Step 2: Generate datasets (if needed)
    datasets_exist = (base_path / "dataset_A").exists() and (base_path / "dataset_B").exists()
    if not datasets_exist:
        print("\n[Step 2/5] Generating test datasets...")
        if not run_command(
            "python3 generate_test_datasets.py",
            "Generating dataset pairs with identical output sizes"
        ):
            print("✗ Dataset generation failed.")
            sys.exit(1)
    else:
        print("\n[Step 2/5] Datasets already exist, skipping generation.")
    
    # Step 3: Verify datasets
    print("\n[Step 3/5] Verifying datasets...")
    if not run_command(
        "python3 verify_datasets.py",
        "Verifying dataset output sizes match"
    ):
        print("✗ Dataset verification failed.")
        sys.exit(1)
    
    # Step 4: Collect memory traces
    print("\n[Step 4/5] Collecting memory traces...")
    print("NOTE: This may take several minutes per query...")
    
    # You can test with just one query first
    test_mode = input("Run in test mode (TB1 only)? [y/N]: ").lower() == 'y'
    
    if test_mode:
        # Test with TB1 only
        if not run_command(
            "python3 scripts/collect_traces.py --query tb1 --dataset dataset_A",
            "Collecting trace for TB1 with dataset_A"
        ):
            print("✗ Trace collection failed for TB1 dataset_A")
            sys.exit(1)
            
        if not run_command(
            "python3 scripts/collect_traces.py --query tb1 --dataset dataset_B",
            "Collecting trace for TB1 with dataset_B"
        ):
            print("✗ Trace collection failed for TB1 dataset_B")
            sys.exit(1)
    else:
        # Collect all traces
        if not run_command(
            "python3 scripts/collect_traces.py --all",
            "Collecting traces for all queries and datasets"
        ):
            print("✗ Trace collection failed.")
            sys.exit(1)
    
    # Step 5: Compare traces
    print("\n[Step 5/5] Comparing memory traces...")
    
    if test_mode:
        cmd = "python3 scripts/compare_traces.py --query tb1"
    else:
        cmd = "python3 scripts/compare_traces.py --all --html"
    
    if not run_command(cmd, "Comparing memory access patterns"):
        print("✗ Trace comparison failed.")
        sys.exit(1)
    
    # Load and display results
    reports_path = base_path / "reports"
    report_file = reports_path / "trace_comparison_report.json"
    
    if report_file.exists():
        with open(report_file, 'r') as f:
            results = json.load(f)
        
        print("\n" + "="*70)
        print(" VERIFICATION RESULTS ".center(70))
        print("="*70)
        
        for query, result in results.items():
            verdict = "OBLIVIOUS" if result['verdict']['is_oblivious'] else "NOT OBLIVIOUS"
            confidence = result['verdict']['confidence']
            symbol = "✓" if result['verdict']['is_oblivious'] else "✗"
            
            print(f"{symbol} {query.upper()}: {verdict} (confidence: {confidence:.2%})")
        
        # Overall verdict
        all_oblivious = all(r['verdict']['is_oblivious'] for r in results.values())
        
        print("\n" + "="*70)
        if all_oblivious:
            print("✓ ALL ALGORITHMS ARE OBLIVIOUS".center(70))
        else:
            print("✗ SOME ALGORITHMS ARE NOT OBLIVIOUS".center(70))
        print("="*70)
        
        if not test_mode:
            print(f"\nDetailed report: {reports_path / 'summary_report.html'}")
    
    print(f"\nCompleted at: {datetime.now().isoformat()}")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
        sys.exit(1)