#!/usr/bin/env python3
"""
K-Way Network Analysis with Random K-way Switches
Analyzes generalized K-way permutation networks where each switch performs random K-way shuffle
"""

import numpy as np
import random
from collections import defaultdict
import math

class KWayNetwork:
    def __init__(self, n, k=2):
        """Initialize K-way network for n inputs with radix k"""
        self.n = n
        self.k = k
        self.switches = {}  # Dictionary to store switch permutations
        
    def set_random_switches(self):
        """Set all switches to random K-way permutations"""
        self.switches = {}
        self._set_random_recursive(0, self.n, 0)
        
    def _set_random_recursive(self, start, size, depth):
        """Recursively set random K-way switches"""
        if size <= 1:
            return
            
        # If size <= K, single K-way shuffle
        if size <= self.k:
            perm = list(range(size))
            random.shuffle(perm)
            self.switches[(depth, start, size)] = perm
            return
        
        # Calculate sub-array sizes
        base_size = size // self.k
        remainder = size % self.k
        group_sizes = [base_size + (1 if i < remainder else 0) for i in range(self.k)]
        
        # Remove empty groups
        non_empty_groups = [s for s in group_sizes if s > 0]
        num_groups = len(non_empty_groups)
        
        # Set switch for group-level shuffle
        group_perm = list(range(num_groups))
        random.shuffle(group_perm)
        self.switches[(depth, start, 'groups')] = group_perm
        
        # Recursively set switches for sub-arrays
        curr_pos = start
        for gsize in non_empty_groups:
            self._set_random_recursive(curr_pos, gsize, depth + 1)
            curr_pos += gsize
    
    def route(self, input_pos):
        """Route an input position through the network"""
        return self._route_recursive(input_pos, 0, self.n, 0)
    
    def _route_recursive(self, pos, start, size, depth):
        """Recursively route through K-way network"""
        if size <= 1:
            return pos  # Return actual position
            
        # If size <= K, single K-way shuffle
        if size <= self.k:
            perm = self.switches.get((depth, start, size))
            if perm is None:
                # Generate if not exists
                perm = list(range(size))
                random.shuffle(perm)
                self.switches[(depth, start, size)] = perm
            
            local_pos = pos - start
            # Find where local_pos goes in the permutation
            for i in range(size):
                if perm[i] == local_pos:
                    return start + i
            return pos  # Shouldn't happen
        
        # Split into K groups as evenly as possible
        base_size = size // self.k
        remainder = size % self.k
        group_sizes = [base_size + (1 if i < remainder else 0) for i in range(self.k)]
        
        # Remove empty groups
        non_empty_groups = [s for s in group_sizes if s > 0]
        num_groups = len(non_empty_groups)
        
        # Find which group this element belongs to
        local_pos = pos - start
        group_idx = 0
        offset = 0
        for i, gsize in enumerate(non_empty_groups):
            if local_pos < offset + gsize:
                group_idx = i
                break
            offset += gsize
        
        # Get or create permutation for groups at this level
        switch_key = (depth, start, 'groups')
        if switch_key not in self.switches:
            group_perm = list(range(num_groups))
            random.shuffle(group_perm)
            self.switches[switch_key] = group_perm
        
        group_perm = self.switches[switch_key]
        
        # Find where this group maps to
        new_group_idx = group_perm[group_idx]
        
        # Calculate offset for new group
        new_offset = sum(non_empty_groups[:new_group_idx])
        
        # Position within the group (stays the same)
        within_group_pos = local_pos - sum(non_empty_groups[:group_idx])
        
        # Recursively route within the new group
        final_pos = self._route_recursive(
            start + new_offset + within_group_pos,
            start + new_offset,
            non_empty_groups[new_group_idx],
            depth + 1
        )
        
        return final_pos
    
    def apply_permutation(self, input_array):
        """Apply the K-way network to permute the input array"""
        n = len(input_array)
        if n <= 1:
            return input_array.copy()
        
        result = np.zeros_like(input_array)
        for i in range(n):
            final_pos = self.route(i)
            result[final_pos] = input_array[i]
        
        return result
    
    def get_permutation(self):
        """Get the full permutation realized by current switch settings"""
        perm = []
        for i in range(self.n):
            perm.append(self.route(i))
        return perm
    
    def check_validity(self):
        """Check if current configuration produces a valid permutation"""
        perm = self.get_permutation()
        # Check if all outputs are covered exactly once
        return sorted(perm) == list(range(self.n))


def analyze_kway_network(n, k, num_trials=10000):
    """Analyze behavior of K-way network with random switches"""
    print(f"\n=== Analyzing {k}-Way Network with n={n}, {num_trials} trials ===\n")
    
    # Track statistics
    valid_perms = 0
    seen_perms = set()
    position_distribution = defaultdict(lambda: defaultdict(int))
    
    for trial in range(num_trials):
        network = KWayNetwork(n, k)
        network.set_random_switches()
        
        perm = network.get_permutation()
        perm_tuple = tuple(perm)
        seen_perms.add(perm_tuple)
        
        # Track where each input goes
        for inp, out in enumerate(perm):
            position_distribution[inp][out] += 1
        
        # Check validity
        if network.check_validity():
            valid_perms += 1
    
    # Calculate statistics
    total_possible = math.factorial(n)
    unique_seen = len(seen_perms)
    
    print(f"Valid permutations: {valid_perms}/{num_trials} ({100*valid_perms/num_trials:.2f}%)")
    print(f"Unique permutations seen: {unique_seen:,}")
    if n <= 20:
        print(f"Total possible: {total_possible:,}")
        print(f"Coverage: {100*unique_seen/total_possible:.4f}%")
    else:
        print(f"Total possible: {n}! (too large to compute)")
    
    # Check uniformity
    print("\nUniformity Analysis:")
    
    # Calculate entropy for a few positions
    sample_positions = min(5, n)
    avg_entropy = 0
    
    for i in range(sample_positions):
        probs = []
        for j in range(n):
            if position_distribution[i][j] > 0:
                p = position_distribution[i][j] / num_trials
                probs.append(p)
        
        entropy = -sum(p * np.log2(p) for p in probs if p > 0)
        avg_entropy += entropy
        
    avg_entropy /= sample_positions
    max_entropy = np.log2(n)
    
    print(f"Average entropy (first {sample_positions} positions): {avg_entropy:.3f}")
    print(f"Maximum entropy (uniform): {max_entropy:.3f}")
    print(f"Uniformity ratio: {100*avg_entropy/max_entropy:.1f}%")
    
    # Check distribution spread
    all_probs = []
    for i in range(n):
        for j in range(n):
            prob = position_distribution[i][j] / num_trials
            all_probs.append(prob)
    
    expected_prob = 1.0 / n
    std_dev = np.std(all_probs)
    max_deviation = max(abs(p - expected_prob) for p in all_probs)
    
    print(f"\nDistribution Statistics:")
    print(f"Expected probability (uniform): {expected_prob:.4f}")
    print(f"Actual std deviation: {std_dev:.4f}")
    print(f"Maximum deviation from expected: {max_deviation:.4f}")
    
    return valid_perms, unique_seen, total_possible if n <= 20 else None


def test_specific_cases():
    """Test K=5 for specific array sizes"""
    print("=" * 70)
    print("Testing K=5 Networks for Specific Array Sizes")
    print("=" * 70)
    
    test_cases = [
        (12, 5, 10000),
        (34, 5, 10000),
        (57, 5, 10000),
    ]
    
    results = []
    
    for n, k, num_trials in test_cases:
        print(f"\n{'='*60}")
        print(f"Testing n={n}, k={k}")
        print(f"{'='*60}")
        
        valid, unique, total = analyze_kway_network(n, k, num_trials)
        
        results.append({
            'n': n,
            'k': k,
            'trials': num_trials,
            'valid': valid,
            'unique': unique,
            'total': total,
            'valid_rate': 100 * valid / num_trials,
            'coverage': 100 * unique / total if total else None
        })
    
    # Summary table
    print("\n" + "=" * 70)
    print("SUMMARY TABLE")
    print("=" * 70)
    print(f"{'n':>5} {'k':>5} {'Trials':>8} {'Valid%':>10} {'Unique':>10} {'Coverage%':>12}")
    print("-" * 70)
    
    for r in results:
        coverage_str = f"{r['coverage']:.4f}%" if r['coverage'] is not None else "N/A"
        print(f"{r['n']:5d} {r['k']:5d} {r['trials']:8d} {r['valid_rate']:10.2f}% "
              f"{r['unique']:10,} {coverage_str:>12}")


def test_simple_example():
    """Test a simple example to verify correctness"""
    print("\n=== Simple Example Test ===")
    print("Testing K=3 network with n=6")
    
    network = KWayNetwork(6, 3)
    
    # Test a few random configurations
    for i in range(3):
        network.set_random_switches()
        input_array = np.arange(6)
        output = network.apply_permutation(input_array)
        perm = network.get_permutation()
        valid = "VALID" if network.check_validity() else "INVALID"
        
        print(f"\nTrial {i+1}:")
        print(f"  Input:  {input_array}")
        print(f"  Output: {output}")
        print(f"  Permutation: {perm} ({valid})")


def test_odd_sizes():
    """Test networks with odd-sized inputs"""
    print("\n" + "=" * 70)
    print("TESTING ODD-SIZED INPUTS")
    print("=" * 70)
    
    # Test different K values with odd n
    test_cases = [
        (2, 3),   # 2-way with n=3
        (2, 5),   # 2-way with n=5
        (2, 7),   # 2-way with n=7
        (2, 11),  # 2-way with n=11
        (3, 7),   # 3-way with n=7
        (3, 11),  # 3-way with n=11
        (5, 11),  # 5-way with n=11
    ]
    
    for k, n in test_cases:
        print(f"\n{'-'*50}")
        print(f"K={k}, n={n}")
        print(f"{'-'*50}")
        
        # Test validity
        valid_count = 0
        for _ in range(100):
            network = KWayNetwork(n, k)
            network.set_random_switches()
            if network.check_validity():
                valid_count += 1
        
        print(f"Valid rate: {valid_count}/100 ({valid_count}%)")
        
        # Show example permutation
        network = KWayNetwork(n, k)
        network.set_random_switches()
        perm = network.get_permutation()
        valid = "VALID" if network.check_validity() else "INVALID"
        print(f"Example permutation: {perm} ({valid})")
        
        # Check for duplicates/missing
        if not network.check_validity():
            seen = {}
            for i, p in enumerate(perm):
                if p in seen:
                    print(f"  Collision: input {seen[p]} and {i} both map to {p}")
                seen[p] = i
            for i in range(n):
                if i not in seen:
                    print(f"  Missing output: {i}")


def analyze_routing_logic():
    """Analyze the routing logic to understand the problem"""
    print("\n" + "=" * 70)
    print("ROUTING LOGIC ANALYSIS")
    print("=" * 70)
    
    # Simple case: K=3, n=6
    print("\nK=3, n=6 routing analysis:")
    network = KWayNetwork(6, 3)
    network.set_random_switches()
    
    print("Switch configuration:")
    for key, value in sorted(network.switches.items()):
        print(f"  {key}: {value}")
    
    print("\nRouting trace for each input:")
    for i in range(6):
        print(f"  Input {i} -> Output {network.route(i)}")
    
    valid = "VALID" if network.check_validity() else "INVALID"
    print(f"\nPermutation: {network.get_permutation()} ({valid})")


def main():
    """Main analysis"""
    # Test simple example first
    test_simple_example()
    
    # Test odd sizes
    test_odd_sizes()
    
    # Analyze routing logic
    analyze_routing_logic()
    
    # Run the specific test cases
    test_specific_cases()
    
    # Additional analysis for different K values
    print("\n" + "=" * 70)
    print("COMPARISON OF DIFFERENT K VALUES")
    print("=" * 70)
    
    n = 32
    num_trials = 10000
    
    for k in [2, 3, 4, 5, 8]:
        print(f"\n{'='*50}")
        print(f"K={k} Network Performance (n={n})")
        print(f"{'='*50}")
        analyze_kway_network(n, k, num_trials)


if __name__ == "__main__":
    main()