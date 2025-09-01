#!/usr/bin/env python3
"""
Test trace collection with a simple example.
"""

import subprocess
import os
from pathlib import Path

def test_valgrind_available():
    """Check if valgrind is available"""
    try:
        result = subprocess.run(['valgrind', '--version'], capture_output=True, text=True)
        if result.returncode == 0:
            print(f"✓ Valgrind found: {result.stdout.strip()}")
            return True
        else:
            print("✗ Valgrind not found")
            return False
    except FileNotFoundError:
        print("✗ Valgrind not installed")
        return False

def test_sgx_app_exists():
    """Check if sgx_app exists"""
    sgx_app = Path("/home/r33wei/omwj/memory_const/impl/src/sgx_app")
    if sgx_app.exists():
        print(f"✓ SGX app found: {sgx_app}")
        return True
    else:
        print(f"✗ SGX app not found at: {sgx_app}")
        return False

def test_datasets_exist():
    """Check if test datasets exist"""
    base_path = Path("/home/r33wei/omwj/memory_const/test_access_patterns")
    
    datasets_ok = True
    for dataset in ['dataset_A', 'dataset_B']:
        for query in ['tb1', 'tb2', 'tm1', 'tm2', 'tm3']:
            path = base_path / dataset / query
            if path.exists():
                print(f"✓ {dataset}/{query} exists")
            else:
                print(f"✗ {dataset}/{query} missing")
                datasets_ok = False
    
    return datasets_ok

def test_simple_trace():
    """Test collecting a simple trace"""
    print("\nTesting simple memory trace collection...")
    
    # Create a simple test program
    test_program = """
#include <stdio.h>
#include <stdlib.h>

int main() {
    int array[100];
    for (int i = 0; i < 100; i++) {
        array[i] = i * 2;
    }
    
    int sum = 0;
    for (int i = 0; i < 100; i++) {
        sum += array[i];
    }
    
    printf("Sum: %d\\n", sum);
    return 0;
}
"""
    
    # Write test program
    with open("test_prog.c", "w") as f:
        f.write(test_program)
    
    # Compile
    result = subprocess.run(['gcc', '-o', 'test_prog', 'test_prog.c'], 
                          capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"✗ Failed to compile test program: {result.stderr}")
        return False
    
    # Run with valgrind
    result = subprocess.run(
        ['valgrind', '--tool=lackey', '--trace-mem=yes', '--log-file=test_trace.txt', './test_prog'],
        capture_output=True, text=True
    )
    
    if result.returncode != 0:
        print(f"✗ Failed to run with valgrind: {result.stderr}")
        return False
    
    # Check trace file
    if os.path.exists('test_trace.txt'):
        with open('test_trace.txt', 'r') as f:
            lines = f.readlines()
        
        # Count memory operations
        loads = sum(1 for line in lines if line.strip().startswith('L '))
        stores = sum(1 for line in lines if line.strip().startswith('S '))
        modifies = sum(1 for line in lines if line.strip().startswith('M '))
        
        print(f"✓ Trace collected: {loads} loads, {stores} stores, {modifies} modifies")
        
        # Clean up
        os.remove('test_prog.c')
        os.remove('test_prog')
        os.remove('test_trace.txt')
        
        return True
    else:
        print("✗ No trace file generated")
        return False

def main():
    print("="*60)
    print("TRACE COLLECTION ENVIRONMENT TEST")
    print("="*60)
    
    all_ok = True
    
    # Test valgrind
    if not test_valgrind_available():
        all_ok = False
        print("\nTo install valgrind: sudo apt-get install valgrind")
    
    # Test SGX app
    if not test_sgx_app_exists():
        all_ok = False
        print("\nPlease compile SGX app first: cd /home/r33wei/omwj/memory_const/impl/src && make")
    
    # Test datasets
    if not test_datasets_exist():
        all_ok = False
        print("\nPlease generate datasets first: python generate_test_datasets.py")
    
    # Test simple trace collection
    if test_valgrind_available():
        if not test_simple_trace():
            all_ok = False
    
    print("\n" + "="*60)
    if all_ok:
        print("✓ ALL TESTS PASSED - Ready to collect traces")
        print("\nNext steps:")
        print("1. Run trace collection: python scripts/collect_traces.py --all")
        print("2. Compare traces: python scripts/compare_traces.py --all --html")
    else:
        print("✗ SOME TESTS FAILED - Please fix issues above")
    print("="*60)

if __name__ == "__main__":
    main()