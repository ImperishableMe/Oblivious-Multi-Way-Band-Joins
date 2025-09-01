#!/usr/bin/env python3
"""
Compare memory access traces to verify oblivious behavior.
"""

import json
import numpy as np
from pathlib import Path
from scipy import stats
from collections import Counter, defaultdict
import hashlib
import argparse

class TraceComparator:
    def __init__(self, base_path="/home/r33wei/omwj/memory_const/test_access_patterns"):
        self.base_path = Path(base_path)
        self.processed_traces = self.base_path / "traces/processed"
        self.reports_path = self.base_path / "reports"
        self.reports_path.mkdir(exist_ok=True)
        
    def load_trace(self, trace_file):
        """Load a processed trace file"""
        with open(trace_file, 'r') as f:
            return json.load(f)
    
    def normalize_addresses(self, operations):
        """Normalize addresses to relative offsets within regions"""
        
        # Group addresses by high-order bits to identify regions
        regions = defaultdict(list)
        
        for op in operations:
            addr = int(op['address'], 16) if isinstance(op['address'], str) else op['address']
            # Use top 16 bits as region identifier
            region_id = (addr >> 48) & 0xFFFF
            regions[region_id].append(addr)
        
        # Find base addresses for each region
        region_bases = {}
        for region_id, addrs in regions.items():
            if addrs:
                region_bases[region_id] = min(addrs)
        
        # Normalize addresses
        normalized_ops = []
        for op in operations:
            addr = int(op['address'], 16) if isinstance(op['address'], str) else op['address']
            region_id = (addr >> 48) & 0xFFFF
            
            if region_id in region_bases:
                offset = addr - region_bases[region_id]
                normalized_ops.append({
                    'operation': op['operation'],
                    'offset': offset,
                    'size': op['size'],
                    'region': region_id
                })
            else:
                normalized_ops.append({
                    'operation': op['operation'],
                    'offset': addr,
                    'size': op['size'],
                    'region': 0
                })
        
        return normalized_ops
    
    def compute_operation_sequence_hash(self, operations):
        """Compute hash of operation sequence (ignoring addresses)"""
        sequence = ''.join([f"{op['operation']}{op['size']}" for op in operations])
        return hashlib.sha256(sequence.encode()).hexdigest()
    
    def compute_access_pattern_similarity(self, ops_a, ops_b):
        """Compute similarity between access patterns"""
        
        # Normalize addresses
        norm_a = self.normalize_addresses(ops_a)
        norm_b = self.normalize_addresses(ops_b)
        
        # Compare sequences
        if len(norm_a) != len(norm_b):
            return 0.0
        
        matches = 0
        for i in range(len(norm_a)):
            if (norm_a[i]['operation'] == norm_b[i]['operation'] and
                norm_a[i]['size'] == norm_b[i]['size'] and
                norm_a[i]['offset'] == norm_b[i]['offset']):
                matches += 1
        
        return matches / len(norm_a)
    
    def analyze_temporal_patterns(self, operations, window_size=1000):
        """Analyze temporal access patterns"""
        
        patterns = []
        num_windows = len(operations) // window_size
        
        for i in range(num_windows):
            window = operations[i*window_size:(i+1)*window_size]
            
            # Count operation types
            op_counts = Counter([op['operation'] for op in window])
            
            # Calculate entropy of addresses
            addresses = [op['address'] for op in window]
            addr_counts = Counter(addresses)
            total = len(addresses)
            
            entropy = 0
            for count in addr_counts.values():
                if count > 0:
                    p = count / total
                    entropy -= p * np.log2(p)
            
            patterns.append({
                'window': i,
                'load_ratio': op_counts.get('L', 0) / window_size,
                'store_ratio': op_counts.get('S', 0) / window_size,
                'modify_ratio': op_counts.get('M', 0) / window_size,
                'address_entropy': entropy
            })
        
        return patterns
    
    def statistical_tests(self, trace_a, trace_b):
        """Perform statistical tests on traces"""
        
        ops_a = trace_a['operations']
        ops_b = trace_b['operations']
        
        results = {}
        
        # 1. Operation type distribution test
        op_types_a = [op['operation'] for op in ops_a]
        op_types_b = [op['operation'] for op in ops_b]
        
        counts_a = Counter(op_types_a)
        counts_b = Counter(op_types_b)
        
        # Chi-square test for operation types
        observed = [counts_a.get('L', 0), counts_a.get('S', 0), counts_a.get('M', 0)]
        expected = [counts_b.get('L', 0), counts_b.get('S', 0), counts_b.get('M', 0)]
        
        if sum(expected) > 0:
            # Normalize expected to match total of observed
            scale = sum(observed) / sum(expected)
            expected = [e * scale for e in expected]
            
            if all(e > 0 for e in expected):
                chi2, p_value = stats.chisquare(observed, expected)
                results['chi_square_operations'] = {
                    'statistic': chi2,
                    'p_value': p_value,
                    'significant': p_value < 0.05
                }
        
        # 2. Access size distribution test
        sizes_a = [op['size'] for op in ops_a]
        sizes_b = [op['size'] for op in ops_b]
        
        if len(sizes_a) > 0 and len(sizes_b) > 0:
            ks_stat, ks_pvalue = stats.ks_2samp(sizes_a, sizes_b)
            results['ks_test_sizes'] = {
                'statistic': ks_stat,
                'p_value': ks_pvalue,
                'significant': ks_pvalue < 0.05
            }
        
        # 3. Temporal pattern analysis
        patterns_a = self.analyze_temporal_patterns(ops_a)
        patterns_b = self.analyze_temporal_patterns(ops_b)
        
        if patterns_a and patterns_b:
            # Compare entropy distributions
            entropy_a = [p['address_entropy'] for p in patterns_a]
            entropy_b = [p['address_entropy'] for p in patterns_b]
            
            if len(entropy_a) > 0 and len(entropy_b) > 0:
                ks_stat, ks_pvalue = stats.ks_2samp(entropy_a, entropy_b)
                results['ks_test_entropy'] = {
                    'statistic': ks_stat,
                    'p_value': ks_pvalue,
                    'significant': ks_pvalue < 0.05
                }
        
        return results
    
    def find_divergences(self, ops_a, ops_b, max_divergences=100):
        """Find where traces diverge"""
        
        divergences = []
        
        for i in range(min(len(ops_a), len(ops_b))):
            if (ops_a[i]['operation'] != ops_b[i]['operation'] or
                ops_a[i]['size'] != ops_b[i]['size']):
                
                divergences.append({
                    'position': i,
                    'dataset_a': f"{ops_a[i]['operation']} size={ops_a[i]['size']}",
                    'dataset_b': f"{ops_b[i]['operation']} size={ops_b[i]['size']}"
                })
                
                if len(divergences) >= max_divergences:
                    break
        
        return divergences
    
    def compare_traces(self, query_name, use_encrypted=False):
        """Compare traces for a specific query"""
        
        # Load traces
        if use_encrypted:
            trace_a_file = self.processed_traces / f"{query_name}_encrypted_dataset_A.json"
            trace_b_file = self.processed_traces / f"{query_name}_encrypted_dataset_B.json"
        else:
            trace_a_file = self.processed_traces / f"{query_name}_dataset_A.json"
            trace_b_file = self.processed_traces / f"{query_name}_dataset_B.json"
        
        if not trace_a_file.exists() or not trace_b_file.exists():
            print(f"Traces not found for {query_name}")
            return None
        
        trace_a = self.load_trace(trace_a_file)
        trace_b = self.load_trace(trace_b_file)
        
        ops_a = trace_a['operations']
        ops_b = trace_b['operations']
        
        # Compute various metrics
        results = {
            'query': query_name,
            'summary': {
                'dataset_a_ops': len(ops_a),
                'dataset_b_ops': len(ops_b),
                'ops_count_match': len(ops_a) == len(ops_b)
            }
        }
        
        # Check operation sequence
        seq_hash_a = self.compute_operation_sequence_hash(ops_a)
        seq_hash_b = self.compute_operation_sequence_hash(ops_b)
        results['summary']['sequences_match'] = seq_hash_a == seq_hash_b
        
        # Compute pattern similarity
        if len(ops_a) > 0 and len(ops_b) > 0:
            similarity = self.compute_access_pattern_similarity(ops_a, ops_b)
            results['summary']['pattern_similarity'] = similarity
        
        # Statistical tests
        results['statistical_tests'] = self.statistical_tests(trace_a, trace_b)
        
        # Find divergences
        divergences = self.find_divergences(ops_a, ops_b)
        results['divergences'] = {
            'count': len(divergences),
            'first_10': divergences[:10]
        }
        
        # Determine if algorithm is oblivious
        is_oblivious = (
            results['summary'].get('sequences_match', False) and
            results['summary'].get('pattern_similarity', 0) > 0.95 and
            all(not test.get('significant', True) 
                for test in results['statistical_tests'].values())
        )
        
        results['verdict'] = {
            'is_oblivious': is_oblivious,
            'confidence': results['summary'].get('pattern_similarity', 0)
        }
        
        return results
    
    def compare_all_traces(self, queries=None, use_encrypted=False):
        """Compare traces for all queries"""
        
        if queries is None:
            queries = ['tb1', 'tb2', 'tm1', 'tm2', 'tm3']
        
        all_results = {}
        
        for query in queries:
            print(f"\nComparing traces for {query.upper()}...")
            results = self.compare_traces(query, use_encrypted=use_encrypted)
            
            if results:
                all_results[query] = results
                
                # Print summary
                print(f"  Operations: A={results['summary']['dataset_a_ops']}, "
                      f"B={results['summary']['dataset_b_ops']}")
                print(f"  Sequences match: {results['summary']['sequences_match']}")
                print(f"  Pattern similarity: {results['summary'].get('pattern_similarity', 0):.2%}")
                print(f"  Verdict: {'OBLIVIOUS' if results['verdict']['is_oblivious'] else 'NOT OBLIVIOUS'}")
        
        # Save comprehensive report
        report_file = self.reports_path / "trace_comparison_report.json"
        with open(report_file, 'w') as f:
            json.dump(all_results, f, indent=2)
        
        print(f"\nReport saved to: {report_file}")
        
        return all_results
    
    def generate_summary_report(self, results):
        """Generate HTML summary report"""
        
        html_content = """<!DOCTYPE html>
<html>
<head>
    <title>Memory Trace Comparison Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        h1 { color: #333; }
        .query { border: 1px solid #ddd; padding: 15px; margin: 10px 0; }
        .oblivious { background-color: #d4edda; }
        .not-oblivious { background-color: #f8d7da; }
        table { border-collapse: collapse; width: 100%; }
        th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
        th { background-color: #f2f2f2; }
    </style>
</head>
<body>
    <h1>Memory Trace Comparison Report</h1>
"""
        
        for query, result in results.items():
            oblivious_class = "oblivious" if result['verdict']['is_oblivious'] else "not-oblivious"
            
            html_content += f"""
    <div class="query {oblivious_class}">
        <h2>{query.upper()}</h2>
        <table>
            <tr><th>Metric</th><th>Value</th></tr>
            <tr><td>Dataset A Operations</td><td>{result['summary']['dataset_a_ops']:,}</td></tr>
            <tr><td>Dataset B Operations</td><td>{result['summary']['dataset_b_ops']:,}</td></tr>
            <tr><td>Sequences Match</td><td>{result['summary']['sequences_match']}</td></tr>
            <tr><td>Pattern Similarity</td><td>{result['summary'].get('pattern_similarity', 0):.2%}</td></tr>
            <tr><td>Divergences</td><td>{result['divergences']['count']}</td></tr>
            <tr><td>Verdict</td><td><b>{'OBLIVIOUS' if result['verdict']['is_oblivious'] else 'NOT OBLIVIOUS'}</b></td></tr>
        </table>
    </div>
"""
        
        html_content += """
</body>
</html>
"""
        
        report_file = self.reports_path / "summary_report.html"
        with open(report_file, 'w') as f:
            f.write(html_content)
        
        print(f"HTML report saved to: {report_file}")

def main():
    parser = argparse.ArgumentParser(description='Compare memory access traces')
    parser.add_argument('--query', help='Specific query to compare (e.g., tb1)')
    parser.add_argument('--all', action='store_true', help='Compare all queries')
    parser.add_argument('--html', action='store_true', help='Generate HTML report')
    parser.add_argument('--encrypted', action='store_true', help='Compare encrypted dataset traces')
    
    args = parser.parse_args()
    
    comparator = TraceComparator()
    
    if args.all:
        results = comparator.compare_all_traces(use_encrypted=args.encrypted)
        if args.html and results:
            comparator.generate_summary_report(results)
    elif args.query:
        results = comparator.compare_traces(args.query, use_encrypted=args.encrypted)
        if results:
            print(json.dumps(results, indent=2))
    else:
        print("Usage: python compare_traces.py --all [--html] [--encrypted]")
        print("   or: python compare_traces.py --query tb1 [--encrypted]")

if __name__ == "__main__":
    main()