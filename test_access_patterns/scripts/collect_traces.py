#!/usr/bin/env python3
"""
Collect memory access traces using Valgrind Lackey for oblivious algorithm verification.
"""

import os
import sys
import subprocess
import json
import re
from pathlib import Path
from datetime import datetime
import argparse

class TraceCollector:
    def __init__(self, base_path="/home/r33wei/omwj/memory_const"):
        self.base_path = Path(base_path)
        self.test_path = self.base_path / "test_access_patterns"
        self.sgx_app = self.base_path / "impl/src/sgx_app"
        self.traces_path = self.test_path / "traces"
        self.raw_traces = self.traces_path / "raw"
        self.processed_traces = self.traces_path / "processed"
        
        # Ensure directories exist
        self.raw_traces.mkdir(parents=True, exist_ok=True)
        self.processed_traces.mkdir(parents=True, exist_ok=True)
        
        # Memory operation patterns for Lackey output
        self.load_pattern = re.compile(r'L ([0-9a-fA-Fx]+),(\d+)')
        self.store_pattern = re.compile(r'S ([0-9a-fA-Fx]+),(\d+)')
        self.modify_pattern = re.compile(r'M ([0-9a-fA-Fx]+),(\d+)')
        
    def collect_trace(self, query_name, dataset_name, output_file=None, use_encrypted=False):
        """Collect memory trace for a specific query and dataset"""
        
        # Paths
        query_file = self.test_path / "queries" / f"tpch_{query_name}.sql"
        
        # Use encrypted dataset path if specified
        if use_encrypted:
            data_dir = self.test_path / f"encrypted_{dataset_name}" / query_name
        else:
            data_dir = self.test_path / dataset_name / query_name
        
        if output_file is None:
            output_file = "output.csv"
        
        # Construct valgrind command
        valgrind_cmd = [
            "valgrind",
            "--tool=lackey",
            "--trace-mem=yes",
            "--log-file=temp_trace.txt",
            str(self.sgx_app),
            str(query_file),
            str(data_dir),
            output_file
        ]
        
        print(f"Collecting trace for {query_name} with {dataset_name}...")
        print(f"Command: {' '.join(valgrind_cmd)}")
        
        try:
            # Run with valgrind
            result = subprocess.run(
                valgrind_cmd,
                capture_output=True,
                text=True,
                cwd=self.base_path / "impl/src"
            )
            
            if result.returncode != 0:
                print(f"Error running sgx_app: {result.stderr}")
                return None
                
            # Read and save raw trace
            trace_suffix = f"encrypted_{dataset_name}" if use_encrypted else dataset_name
            raw_trace_file = self.raw_traces / f"{query_name}_{trace_suffix}.trace"
            with open("temp_trace.txt", 'r') as f:
                raw_trace = f.read()
            
            with open(raw_trace_file, 'w') as f:
                f.write(raw_trace)
            
            print(f"Raw trace saved to: {raw_trace_file}")
            
            # Process the trace
            processed_file = self.process_trace(raw_trace_file, query_name, trace_suffix)
            
            # Clean up
            if os.path.exists("temp_trace.txt"):
                os.remove("temp_trace.txt")
            if os.path.exists(output_file):
                os.remove(output_file)
                
            return processed_file
            
        except Exception as e:
            print(f"Error collecting trace: {e}")
            return None
    
    def process_trace(self, raw_trace_file, query_name, dataset_name):
        """Process raw trace into structured format"""
        
        processed_file = self.processed_traces / f"{query_name}_{dataset_name}.json"
        operations = []
        
        with open(raw_trace_file, 'r') as f:
            lines = f.readlines()
        
        # Parse each line
        op_count = 0
        for line in lines:
            line = line.strip()
            
            # Skip non-memory lines
            if not line or line.startswith('=='):
                continue
                
            # Try to match each pattern
            op_type = None
            address = None
            size = None
            
            match = self.load_pattern.match(line)
            if match:
                op_type = 'L'
                address = match.group(1)
                size = int(match.group(2))
            else:
                match = self.store_pattern.match(line)
                if match:
                    op_type = 'S'
                    address = match.group(1)
                    size = int(match.group(2))
                else:
                    match = self.modify_pattern.match(line)
                    if match:
                        op_type = 'M'
                        address = match.group(1)
                        size = int(match.group(2))
            
            if op_type:
                operations.append({
                    'index': op_count,
                    'operation': op_type,
                    'address': address,
                    'size': size
                })
                op_count += 1
        
        # Save processed trace
        processed_data = {
            'metadata': {
                'query': query_name,
                'dataset': dataset_name,
                'timestamp': datetime.now().isoformat(),
                'total_operations': len(operations)
            },
            'operations': operations
        }
        
        with open(processed_file, 'w') as f:
            json.dump(processed_data, f, indent=2)
        
        print(f"Processed trace saved to: {processed_file}")
        print(f"Total operations: {len(operations)}")
        
        # Print statistics
        load_count = sum(1 for op in operations if op['operation'] == 'L')
        store_count = sum(1 for op in operations if op['operation'] == 'S')
        modify_count = sum(1 for op in operations if op['operation'] == 'M')
        
        print(f"  Loads: {load_count}")
        print(f"  Stores: {store_count}")
        print(f"  Modifies: {modify_count}")
        
        return processed_file
    
    def collect_all_traces(self, queries=None, use_encrypted=False):
        """Collect traces for all queries and datasets"""
        
        if queries is None:
            queries = ['tb1', 'tb2', 'tm1', 'tm2', 'tm3']
        
        results = {}
        
        for query in queries:
            print(f"\n{'='*60}")
            print(f"Collecting traces for {query.upper()}")
            if use_encrypted:
                print("Using ENCRYPTED datasets")
            print('='*60)
            
            # Collect for dataset A
            trace_a = self.collect_trace(query, 'dataset_A', use_encrypted=use_encrypted)
            
            # Collect for dataset B  
            trace_b = self.collect_trace(query, 'dataset_B', use_encrypted=use_encrypted)
            
            results[query] = {
                'dataset_A': trace_a,
                'dataset_B': trace_b
            }
            
            if trace_a and trace_b:
                print(f"✓ Successfully collected traces for {query}")
            else:
                print(f"✗ Failed to collect traces for {query}")
        
        return results

def main():
    parser = argparse.ArgumentParser(description='Collect memory access traces')
    parser.add_argument('--query', help='Specific query to trace (e.g., tb1)')
    parser.add_argument('--dataset', help='Dataset to use (dataset_A or dataset_B)')
    parser.add_argument('--all', action='store_true', help='Collect traces for all queries')
    parser.add_argument('--encrypted', action='store_true', help='Use encrypted datasets')
    
    args = parser.parse_args()
    
    collector = TraceCollector()
    
    if args.all:
        results = collector.collect_all_traces(use_encrypted=args.encrypted)
        print("\n" + "="*60)
        print("TRACE COLLECTION SUMMARY")
        if args.encrypted:
            print("(Using ENCRYPTED datasets)")
        print("="*60)
        for query, traces in results.items():
            status = "✓" if traces['dataset_A'] and traces['dataset_B'] else "✗"
            print(f"{status} {query.upper()}")
    elif args.query and args.dataset:
        collector.collect_trace(args.query, args.dataset, use_encrypted=args.encrypted)
    else:
        print("Usage: python collect_traces.py --all [--encrypted]")
        print("   or: python collect_traces.py --query tb1 --dataset dataset_A [--encrypted]")

if __name__ == "__main__":
    main()