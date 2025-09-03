#!/usr/bin/env python3
"""
Clean K-Way Network Implementation
A recursive K-way permutation network with proper input/output switches
Guarantees 100% valid permutations for any n and k
"""

import numpy as np
import random
from collections import defaultdict
import math


class CleanKWayNetwork:
    """
    Clean recursive K-way permutation network
    
    Algorithm:
    1. Input switches: Take k items, permute, distribute to groups
    2. Recursively permute each group
    3. Output switches: Collect from groups, permute, output
    """
    
    def __init__(self, n, k=2):
        """Initialize K-way network for n elements with radix k"""
        self.n = n
        self.k = k
        self.switches = {}  # Store switch permutations
        self.depth_counter = 0  # Track recursion depth for switch indexing
        
    def set_random_switches(self):
        """Generate all random switch permutations"""
        self.switches = {}
        self.depth_counter = 0
        # Pre-generate switches by doing a dry run
        self._generate_switches_recursive(0, self.n, 0)
        
    def _generate_switches_recursive(self, start, size, depth):
        """Recursively generate switch configurations"""
        if size <= 1:
            return
            
        if size <= self.k:
            # Base case: single k-way shuffle
            perm = list(range(size))
            random.shuffle(perm)
            self.switches[(depth, start, 'base')] = perm
            return
        
        # Generate input switches
        for i in range(0, size, self.k):
            chunk_size = min(self.k, size - i)
            perm = list(range(chunk_size))
            random.shuffle(perm)
            self.switches[(depth, start + i, 'input')] = perm
        
        # Calculate group sizes
        groups = self._calculate_group_sizes(size)
        
        # Generate output switches
        max_group_size = max(groups)
        for pos in range(max_group_size):
            # Count how many groups have element at this position
            active_groups = sum(1 for g in groups if pos < g)
            if active_groups > 1:
                perm = list(range(active_groups))
                random.shuffle(perm)
                self.switches[(depth, start, 'output', pos)] = perm
        
        # Recursively generate for each group
        group_start = start
        for group_size in groups:
            if group_size > 0:
                self._generate_switches_recursive(group_start, group_size, depth + 1)
                group_start += group_size
    
    def _calculate_group_sizes(self, size):
        """Calculate sizes of k groups for given total size"""
        base_size = size // self.k
        remainder = size % self.k
        # First 'remainder' groups get base_size + 1, rest get base_size
        groups = []
        for i in range(self.k):
            if i < remainder:
                groups.append(base_size + 1)
            elif base_size > 0:
                groups.append(base_size)
            else:
                groups.append(0)
        return groups
    
    def apply_permutation(self, array):
        """Apply the K-way network to permute the input array"""
        if len(array) != self.n:
            raise ValueError(f"Array size {len(array)} doesn't match network size {self.n}")
        
        result = array.copy()
        self.depth_counter = 0
        result = self._permute_recursive(result, 0, self.n, 0)
        return result
    
    def _permute_recursive(self, array, start, size, depth):
        """Recursively apply K-way permutation"""
        if size <= 1:
            return array
            
        # Extract the subarray we're working on
        subarray = array[start:start+size]
        
        if size <= self.k:
            # Base case: direct k-way shuffle
            perm = self.switches.get((depth, start, 'base'), list(range(size)))
            shuffled = [subarray[perm[i]] for i in range(size)]
            array[start:start+size] = shuffled
            return array
        
        # Step 1: Input switches
        groups = self._apply_input_switches(subarray, start, depth)
        
        # Step 2: Recursively permute each group
        group_start = 0
        for i, group in enumerate(groups):
            if len(group) > 0:
                # Copy group back to array for recursive processing
                array[start+group_start:start+group_start+len(group)] = group
                array = self._permute_recursive(array, start+group_start, len(group), depth + 1)
                # Extract permuted group
                groups[i] = array[start+group_start:start+group_start+len(group)]
                group_start += len(group)
        
        # Step 3: Output switches
        result = self._apply_output_switches(groups, start, depth)
        array[start:start+size] = result
        
        return array
    
    def _apply_input_switches(self, subarray, start, depth):
        """Apply input k-way switches to distribute elements to groups"""
        size = len(subarray)
        groups = [[] for _ in range(self.k)]
        
        for i in range(0, size, self.k):
            chunk = subarray[i:i+self.k]
            chunk_size = len(chunk)
            
            # Get permutation for this chunk
            perm = self.switches.get((depth, start + i, 'input'), list(range(chunk_size)))
            
            # Apply permutation and distribute to groups
            for j in range(chunk_size):
                groups[j].append(chunk[perm[j]])
        
        return groups
    
    def _apply_output_switches(self, groups, start, depth):
        """Apply output k-way switches to collect from groups"""
        output = []
        max_size = max(len(g) for g in groups)
        
        for pos in range(max_size):
            # Collect elements at position 'pos' from each non-empty group
            chunk = []
            for j in range(self.k):
                if pos < len(groups[j]):
                    chunk.append(groups[j][pos])
            
            if len(chunk) == 1:
                # No permutation needed for single element
                output.extend(chunk)
            else:
                # Get permutation for this position
                perm = self.switches.get((depth, start, 'output', pos), list(range(len(chunk))))
                
                # Apply permutation
                shuffled = [chunk[perm[i]] for i in range(len(chunk))]
                output.extend(shuffled)
        
        return output
    
    def get_permutation(self):
        """Get the permutation vector (where does each input go)"""
        identity = list(range(self.n))
        permuted = self.apply_permutation(identity)
        return permuted
    
    def check_validity(self):
        """Check if the network produces a valid permutation"""
        perm = self.get_permutation()
        return sorted(perm) == list(range(self.n))


def analyze_network(n, k, num_trials=10000):
    """Analyze the behavior of the clean K-way network"""
    print(f"\n{'='*60}")
    print(f"Analyzing Clean {k}-Way Network for n={n}")
    print(f"{'='*60}")
    
    valid_count = 0
    seen_perms = set()
    position_dist = defaultdict(lambda: defaultdict(int))
    
    for trial in range(num_trials):
        network = CleanKWayNetwork(n, k)
        network.set_random_switches()
        
        perm = network.get_permutation()
        perm_tuple = tuple(perm)
        seen_perms.add(perm_tuple)
        
        # Track distribution
        for i, p in enumerate(perm):
            position_dist[i][p] += 1
        
        # Check validity
        if network.check_validity():
            valid_count += 1
        else:
            if trial < 5:  # Show first few invalid cases for debugging
                print(f"  Invalid permutation found: {perm}")
    
    # Calculate statistics
    unique_count = len(seen_perms)
    validity_rate = 100 * valid_count / num_trials
    
    print(f"\nResults:")
    print(f"  Valid permutations: {valid_count}/{num_trials} ({validity_rate:.1f}%)")
    print(f"  Unique permutations: {unique_count:,}")
    
    if n <= 20:
        total_possible = math.factorial(n)
        coverage = 100 * unique_count / total_possible
        print(f"  Total possible: {total_possible:,}")
        print(f"  Coverage: {coverage:.4f}%")
    
    # Check uniformity
    avg_entropy = 0
    sample_positions = min(5, n)
    for i in range(sample_positions):
        probs = []
        for j in range(n):
            if position_dist[i][j] > 0:
                p = position_dist[i][j] / num_trials
                probs.append(p)
        if probs:
            entropy = -sum(p * np.log2(p) for p in probs if p > 0)
            avg_entropy += entropy
    
    avg_entropy /= sample_positions
    max_entropy = np.log2(n)
    uniformity = 100 * avg_entropy / max_entropy if max_entropy > 0 else 0
    
    print(f"\nUniformity:")
    print(f"  Average entropy: {avg_entropy:.3f}")
    print(f"  Maximum entropy: {max_entropy:.3f}")
    print(f"  Uniformity ratio: {uniformity:.1f}%")
    
    return valid_count, unique_count


def test_specific_cases():
    """Test the specific cases requested: k=5 for n=11,12,34,57"""
    print("="*70)
    print("TESTING CLEAN K-WAY NETWORK")
    print("="*70)
    
    test_cases = [
        (11, 5, 10000),
        (12, 5, 10000),
        (34, 5, 10000),
        (57, 5, 10000),
    ]
    
    results = []
    
    for n, k, trials in test_cases:
        valid_count, unique_count = analyze_network(n, k, trials)
        validity_rate = 100 * valid_count / trials
        
        results.append({
            'n': n,
            'k': k,
            'trials': trials,
            'valid_count': valid_count,
            'validity_rate': validity_rate,
            'unique_count': unique_count
        })
    
    # Summary table
    print("\n" + "="*70)
    print("SUMMARY TABLE")
    print("="*70)
    print(f"{'n':>5} {'k':>5} {'Trials':>8} {'Valid%':>10} {'Unique':>10}")
    print("-"*70)
    
    for r in results:
        print(f"{r['n']:5d} {r['k']:5d} {r['trials']:8d} "
              f"{r['validity_rate']:10.1f}% {r['unique_count']:10,}")


def test_simple_example():
    """Test a simple example to verify correctness"""
    print("\n" + "="*70)
    print("SIMPLE EXAMPLE TEST")
    print("="*70)
    
    # Test n=11, k=5 with detailed trace
    print("\nDetailed trace for n=11, k=5:")
    network = CleanKWayNetwork(11, 5)
    network.set_random_switches()
    
    input_array = list(range(11))
    print(f"Input: {input_array}")
    
    output = network.apply_permutation(input_array)
    print(f"Output: {output}")
    
    perm = network.get_permutation()
    print(f"Permutation vector: {perm}")
    print(f"Valid: {network.check_validity()}")
    
    # Show a few more examples
    print("\nAdditional examples:")
    for i in range(3):
        network = CleanKWayNetwork(11, 5)
        network.set_random_switches()
        perm = network.get_permutation()
        valid = "VALID" if network.check_validity() else "INVALID"
        print(f"  Example {i+1}: {perm} ({valid})")


def main():
    """Main test function"""
    # Run simple example first
    test_simple_example()
    
    # Run specific test cases
    test_specific_cases()
    
    # Test with various k values for comparison
    print("\n" + "="*70)
    print("COMPARISON WITH DIFFERENT K VALUES")
    print("="*70)
    
    n = 32
    for k in [2, 3, 4, 5, 8]:
        analyze_network(n, k, 5000)


if __name__ == "__main__":
    main()