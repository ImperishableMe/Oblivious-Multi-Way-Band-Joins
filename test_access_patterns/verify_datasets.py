#!/usr/bin/env python3
"""
Independent verification script for test datasets.
Loads queries and datasets from disk and validates output sizes match.
"""

import os
import sys
import sqlite3
import pandas as pd
from pathlib import Path
import json

class DatasetVerifier:
    def __init__(self, base_path="/home/r33wei/omwj/memory_const/test_access_patterns"):
        self.base_path = Path(base_path)
        self.queries_path = self.base_path / "queries"
        self.dataset_a_path = self.base_path / "dataset_A"
        self.dataset_b_path = self.base_path / "dataset_B"
        self.results = {}
    
    def verify_all_datasets(self):
        """Verify all datasets that exist on disk"""
        print("="*70)
        print("DATASET VERIFICATION REPORT")
        print("="*70)
        
        # Find all queries that have datasets
        available_queries = self.find_available_queries()
        
        if not available_queries:
            print("No datasets found to verify!")
            return
        
        print(f"Found datasets for: {', '.join(available_queries)}\n")
        
        # Verify each query
        for query_name in available_queries:
            self.verify_query(query_name)
        
        # Print summary
        self.print_summary()
    
    def find_available_queries(self):
        """Find queries that have both dataset_A and dataset_B"""
        available = []
        
        # Check for standard queries
        for query_name in ['tb1', 'tb2', 'tm1', 'tm2', 'tm3']:
            dataset_a_dir = self.dataset_a_path / query_name
            dataset_b_dir = self.dataset_b_path / query_name
            
            if dataset_a_dir.exists() and dataset_b_dir.exists():
                # Check if directories have CSV files
                a_has_files = any(dataset_a_dir.glob("*.csv"))
                b_has_files = any(dataset_b_dir.glob("*.csv"))
                
                if a_has_files and b_has_files:
                    available.append(query_name)
        
        return available
    
    def verify_query(self, query_name):
        """Verify a single query's datasets"""
        print(f"\n{'='*50}")
        print(f"Verifying {query_name.upper()}")
        print('='*50)
        
        try:
            # Load query
            query_file = self.queries_path / f"tpch_{query_name}.sql"
            if not query_file.exists():
                print(f"✗ Query file not found: {query_file}")
                self.results[query_name] = {'status': 'FAILED', 'error': 'Query file not found'}
                return
            
            with open(query_file, 'r') as f:
                query_sql = f.read()
            
            # Load datasets
            print("\n1. Loading datasets from disk...")
            dataset_a = self.load_dataset(self.dataset_a_path / query_name)
            dataset_b = self.load_dataset(self.dataset_b_path / query_name)
            
            if not dataset_a or not dataset_b:
                print("✗ Failed to load datasets")
                self.results[query_name] = {'status': 'FAILED', 'error': 'Failed to load datasets'}
                return
            
            # Analyze datasets
            print("\n2. Analyzing dataset contents...")
            stats_a = self.analyze_dataset(dataset_a, "Dataset A")
            stats_b = self.analyze_dataset(dataset_b, "Dataset B")
            
            # Run queries
            print("\n3. Running SQL queries...")
            output_a = self.run_query(dataset_a, query_sql)
            output_b = self.run_query(dataset_b, query_sql)
            
            print(f"   Dataset A output: {output_a:,} rows")
            print(f"   Dataset B output: {output_b:,} rows")
            
            # Verify match
            print("\n4. Verification result:")
            if output_a == output_b:
                print(f"   ✓ PASSED: Both datasets produce {output_a:,} rows")
                self.results[query_name] = {
                    'status': 'PASSED',
                    'output_size': output_a,
                    'stats_a': stats_a,
                    'stats_b': stats_b
                }
            else:
                diff = abs(output_a - output_b)
                print(f"   ✗ FAILED: Output mismatch (difference: {diff:,} rows)")
                self.results[query_name] = {
                    'status': 'FAILED',
                    'output_a': output_a,
                    'output_b': output_b,
                    'difference': diff
                }
            
        except Exception as e:
            print(f"✗ Error verifying {query_name}: {e}")
            self.results[query_name] = {'status': 'ERROR', 'error': str(e)}
    
    def load_dataset(self, dataset_path):
        """Load all CSV files from a dataset directory"""
        dataset = {}
        
        if not dataset_path.exists():
            return None
        
        # Load all CSV files
        for csv_file in dataset_path.glob("*.csv"):
            table_name = csv_file.stem
            try:
                df = pd.read_csv(csv_file)
                dataset[table_name] = df
                print(f"   Loaded {table_name}.csv: {len(df):,} rows, {len(df.columns)} columns")
            except Exception as e:
                print(f"   ✗ Error loading {csv_file}: {e}")
                return None
        
        return dataset
    
    def analyze_dataset(self, dataset, label):
        """Analyze dataset for padding and statistics"""
        stats = {}
        
        print(f"\n   {label} analysis:")
        
        for table_name, df in dataset.items():
            # Count padding rows (look for negative values in numeric columns)
            padding_count = 0
            
            # Find numeric columns
            numeric_cols = df.select_dtypes(include=['int64', 'float64']).columns
            
            if len(numeric_cols) > 0:
                # Check for any negative values (indicating padding)
                for col in numeric_cols:
                    if (df[col] < 0).any():
                        padding_count = max(padding_count, (df[col] < 0).sum())
            
            original_count = len(df) - padding_count
            
            stats[table_name] = {
                'total': len(df),
                'original': original_count,
                'padding': padding_count
            }
            
            print(f"     {table_name}: {len(df):,} total ({original_count:,} original + {padding_count:,} padding)")
        
        return stats
    
    def run_query(self, dataset, query_sql):
        """Run SQL query on dataset and return row count"""
        # Create in-memory SQLite database
        conn = sqlite3.connect(':memory:')
        
        try:
            # Load all tables
            for table_name, df in dataset.items():
                df.to_sql(table_name, conn, index=False, if_exists='replace')
            
            # Convert to COUNT query
            count_query = query_sql.replace("SELECT *", "SELECT COUNT(*)")
            
            # Execute query
            cursor = conn.cursor()
            cursor.execute(count_query)
            count = cursor.fetchone()[0]
            
            return count
            
        except Exception as e:
            print(f"     SQL Error: {e}")
            raise
        finally:
            conn.close()
    
    def print_summary(self):
        """Print summary of all verification results"""
        print("\n" + "="*70)
        print("VERIFICATION SUMMARY")
        print("="*70)
        
        passed = []
        failed = []
        errors = []
        
        for query_name, result in self.results.items():
            if result['status'] == 'PASSED':
                passed.append(query_name)
            elif result['status'] == 'FAILED':
                failed.append(query_name)
            else:
                errors.append(query_name)
        
        # Print results
        if passed:
            print(f"\n✓ PASSED ({len(passed)}):")
            for query_name in passed:
                output_size = self.results[query_name]['output_size']
                print(f"   - {query_name.upper()}: {output_size:,} rows")
        
        if failed:
            print(f"\n✗ FAILED ({len(failed)}):")
            for query_name in failed:
                result = self.results[query_name]
                if 'difference' in result:
                    print(f"   - {query_name.upper()}: difference of {result['difference']:,} rows")
                else:
                    print(f"   - {query_name.upper()}: {result.get('error', 'Unknown error')}")
        
        if errors:
            print(f"\n⚠ ERRORS ({len(errors)}):")
            for query_name in errors:
                error = self.results[query_name].get('error', 'Unknown error')
                print(f"   - {query_name.upper()}: {error}")
        
        # Overall status
        print("\n" + "-"*70)
        total = len(self.results)
        if total > 0:
            success_rate = len(passed) / total * 100
            print(f"Overall: {len(passed)}/{total} passed ({success_rate:.1f}%)")
            
            if len(passed) == total:
                print("\n🎉 All datasets verified successfully!")
            else:
                print(f"\n⚠ {len(failed) + len(errors)} datasets need attention")
        
        # Save results to JSON
        self.save_results()
    
    def save_results(self):
        """Save verification results to JSON file"""
        results_file = self.base_path / "verification_results.json"
        
        # Convert results to JSON-serializable format
        json_results = {}
        for query_name, result in self.results.items():
            json_results[query_name] = {}
            for key, value in result.items():
                if isinstance(value, (pd.DataFrame, pd.Series)):
                    json_results[query_name][key] = str(value)
                elif isinstance(value, dict):
                    # Recursively convert nested dicts
                    json_results[query_name][key] = self.convert_to_json_serializable(value)
                else:
                    # Convert numpy/pandas types to Python types
                    json_results[query_name][key] = self.convert_to_json_serializable(value)
        
        try:
            with open(results_file, 'w') as f:
                json.dump(json_results, f, indent=2)
            print(f"\nResults saved to: {results_file}")
        except Exception as e:
            print(f"\nFailed to save results: {e}")
    
    def convert_to_json_serializable(self, obj):
        """Convert numpy/pandas types to JSON-serializable Python types"""
        import numpy as np
        
        if isinstance(obj, (np.integer, pd.Int64Dtype)):
            return int(obj)
        elif isinstance(obj, (np.floating, float)):
            return float(obj)
        elif isinstance(obj, (np.ndarray, pd.Series)):
            return obj.tolist()
        elif isinstance(obj, dict):
            return {k: self.convert_to_json_serializable(v) for k, v in obj.items()}
        elif isinstance(obj, list):
            return [self.convert_to_json_serializable(item) for item in obj]
        else:
            return obj

def main():
    verifier = DatasetVerifier()
    verifier.verify_all_datasets()

if __name__ == "__main__":
    main()