#!/usr/bin/env python3
"""
Fixed Python implementation of Waksman shuffle with clear semantics
n = size of my group (number of elements I'm shuffling)
"""

import random
from typing import List

class WaksmanShuffleFixed:
    def __init__(self, seed=42):
        """Initialize with deterministic RNG for testing"""
        self.rng = random.Random(seed)
        self.switch_history = []
        
    def get_switch_bit(self, level: int, position: int) -> int:
        """Get deterministic switch bit based on level and position"""
        hash_val = (level * 1000 + position) % 100
        return 1 if hash_val < 50 else 0
    
    def swap_elements(self, array: List, idx1: int, idx2: int, swap: int):
        """Swap elements if swap=1"""
        if swap:
            array[idx1], array[idx2] = array[idx2], array[idx1]
            
    def waksman_recursive(self, array: List, start: int, stride: int, n: int, 
                          level: int, depth: int = 0) -> None:
        """
        Recursive Waksman shuffle
        
        Parameters:
        - array: The full array being shuffled
        - start: Starting index in the array
        - stride: Distance between consecutive elements in my group
        - n: SIZE OF MY GROUP (number of elements I'm responsible for)
        - level: Recursion level for RNG
        - depth: Debug depth for printing
        """
        indent = "  " * depth
        print(f"{indent}waksman_recursive: start={start}, stride={stride}, n={n} (my group size), level={level}")
        
        # Base cases
        if n <= 1:
            print(f"{indent}  Base case: n<=1, nothing to do")
            return
            
        if n == 2:
            # Single switch between my two elements
            idx1 = start
            idx2 = start + stride
            swap = self.get_switch_bit(level, start)
            
            print(f"{indent}  Base case n=2: swap={swap} at positions {idx1},{idx2}")
            
            # Bounds check
            if idx2 >= len(array):
                print(f"{indent}  ERROR: idx2={idx2} >= len(array)={len(array)}")
                raise IndexError(f"Index {idx2} out of bounds")
                
            self.swap_elements(array, idx1, idx2, swap)
            self.switch_history.append((idx1, idx2, swap, f"n=2 base"))
            return
            
        # For n > 2: Waksman recursive structure
        # Key insight: We have n elements in our group
        # They form n/2 pairs (with possibly 1 unpaired element if n is odd)
        
        num_pairs = n // 2  # Number of pairs in my group
        print(f"{indent}  Recursive case: n={n} elements form {num_pairs} pairs")
        
        # INPUT SWITCHES: One switch per pair
        print(f"{indent}  INPUT SWITCHES: {num_pairs} switches")
        for i in range(num_pairs):
            # The i-th pair in my group
            idx1 = start + (i * 2) * stride      # Element 2*i of my group
            idx2 = start + (i * 2 + 1) * stride  # Element 2*i+1 of my group
            
            # Bounds check
            if idx2 >= len(array):
                print(f"{indent}    ERROR: idx2={idx2} >= len(array)={len(array)}")
                raise IndexError(f"Index {idx2} out of bounds")
                
            swap = self.get_switch_bit(level, idx1)
            print(f"{indent}    Input switch {i}: swap={swap} at positions {idx1},{idx2}")
            self.swap_elements(array, idx1, idx2, swap)
            self.switch_history.append((idx1, idx2, swap, f"input {i}"))
            
        # RECURSIVE CALLS
        # After input switches, elements are at:
        # - Even positions (0, 2, 4, ...): These go to top network
        # - Odd positions (1, 3, 5, ...): These go to bottom network
        # If n is odd, the last element (at position n-1) goes to top network
        
        if n % 2 == 0:
            # Even n: both networks have n/2 elements
            top_size = num_pairs
            bottom_size = num_pairs
        else:
            # Odd n: top network has one more element (the unpaired one)
            top_size = num_pairs + 1  # Includes the unpaired element
            bottom_size = num_pairs
            
        print(f"{indent}  RECURSIVE CALLS: top_size={top_size}, bottom_size={bottom_size}")
        
        # Top subnetwork: elements at positions 0, 2, 4, ... in my group
        print(f"{indent}  Calling TOP: start={start}, stride={stride * 2}, n={top_size}")
        self.waksman_recursive(array, start, stride * 2, top_size, level + 1, depth + 1)
        
        # Bottom subnetwork: elements at positions 1, 3, 5, ... in my group  
        print(f"{indent}  Calling BOTTOM: start={start + stride}, stride={stride * 2}, n={bottom_size}")
        self.waksman_recursive(array, start + stride, stride * 2, bottom_size, level + 1, depth + 1)
            
        # OUTPUT SWITCHES: One less than input switches (Waksman property)
        # The first pair doesn't have an output switch
        num_output_switches = max(0, num_pairs - 1)
        print(f"{indent}  OUTPUT SWITCHES: {num_output_switches} switches")
        
        for i in range(1, num_pairs):  # Start from pair 1, not pair 0
            # The i-th pair in my group (after recursive calls)
            idx1 = start + (i * 2) * stride
            idx2 = start + (i * 2 + 1) * stride
            
            # Bounds check
            if idx2 >= len(array):
                print(f"{indent}    ERROR: idx2={idx2} >= len(array)={len(array)}")
                raise IndexError(f"Index {idx2} out of bounds")
                
            swap = self.get_switch_bit(level + 10000, idx1)
            print(f"{indent}    Output switch {i-1}: swap={swap} at positions {idx1},{idx2}")
            self.swap_elements(array, idx1, idx2, swap)
            self.switch_history.append((idx1, idx2, swap, f"output {i-1}"))
            
    def shuffle(self, array: List) -> List:
        """Main entry point for Waksman shuffle"""
        n = len(array)
        print(f"\n=== Starting Waksman shuffle for array of size {n} ===")
        print(f"Initial array: {array}")
        
        self.switch_history = []
        result = array.copy()
        
        # Start with the full array as our group
        self.waksman_recursive(result, 0, 1, n, 0)
        
        print(f"Final array: {result}")
        print(f"Total switches: {len(self.switch_history)}")
        return result


def test_fixed_waksman():
    """Test the fixed Waksman implementation"""
    shuffler = WaksmanShuffleFixed()
    
    # Test the problematic sizes
    test_sizes = [2, 3, 4, 5, 7]
    
    for n in test_sizes:
        print(f"\n{'='*60}")
        print(f"Testing n={n}")
        print('='*60)
        
        array = list(range(n))
        
        try:
            result = shuffler.shuffle(array)
            
            # Verify it's a valid permutation
            if sorted(result) == array:
                print(f"✓ Valid permutation for n={n}")
            else:
                print(f"✗ INVALID result for n={n}: {result}")
                
            # Check bounds
            max_idx = max(max(idx1, idx2) for idx1, idx2, _, _ in shuffler.switch_history)
            print(f"  Max index accessed: {max_idx} (array size: {n})")
            print(f"  Bounds check: {'PASS' if max_idx < n else 'FAIL'}")
                
        except IndexError as e:
            print(f"✗ ERROR for n={n}: {e}")


if __name__ == "__main__":
    test_fixed_waksman()