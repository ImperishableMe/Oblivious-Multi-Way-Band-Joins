#!/usr/bin/env python3
"""
Python implementation of the in-memory Waksman shuffle
Matches the C implementation to help debug the n=3 issue
"""

import random
from typing import List, Tuple

class WaksmanShuffle:
    def __init__(self, seed=42):
        """Initialize with deterministic RNG for testing"""
        self.rng = random.Random(seed)
        self.switch_history = []
        
    def get_switch_bit(self, level: int, position: int) -> int:
        """Get deterministic switch bit based on level and position"""
        # Use a simple hash for deterministic switches
        hash_val = (level * 1000 + position) % 100
        return 1 if hash_val < 50 else 0
    
    def swap_elements(self, array: List, idx1: int, idx2: int, swap: int):
        """Swap elements if swap=1"""
        if swap:
            array[idx1], array[idx2] = array[idx2], array[idx1]
            
    def waksman_recursive(self, array: List, start: int, stride: int, n: int, 
                          level: int, depth: int = 0) -> None:
        """
        Recursive Waksman shuffle implementation
        Matches the C implementation structure
        """
        indent = "  " * depth
        print(f"{indent}waksman_recursive: start={start}, stride={stride}, n={n}, level={level}")
        
        # Base cases
        if n <= 1:
            print(f"{indent}  Base case n<=1, returning")
            return
            
        if n == 2:
            # Single switch
            swap = self.get_switch_bit(level, start)
            idx1 = start
            idx2 = start + stride
            
            print(f"{indent}  Base case n=2: swap={swap} at positions {idx1},{idx2}")
            
            # Check bounds
            if idx2 >= len(array):
                print(f"{indent}  ERROR: idx2={idx2} >= len(array)={len(array)}")
                raise IndexError(f"Index {idx2} out of bounds for array of size {len(array)}")
                
            self.swap_elements(array, idx1, idx2, swap)
            self.switch_history.append((idx1, idx2, swap, f"n=2 base"))
            return
            
        # For n > 2: Waksman recursive structure
        half = n // 2
        print(f"{indent}  Recursive case n={n}, half={half}")
        
        # Input switches (one per pair)
        print(f"{indent}  Applying {half} input switches:")
        for i in range(half):
            idx1 = start + (i * 2) * stride
            idx2 = start + (i * 2 + 1) * stride
            
            # Check bounds
            if idx2 >= len(array):
                print(f"{indent}    ERROR: idx2={idx2} >= len(array)={len(array)}")
                raise IndexError(f"Index {idx2} out of bounds for array of size {len(array)}")
                
            swap = self.get_switch_bit(level, idx1)
            print(f"{indent}    Input switch {i}: swap={swap} at positions {idx1},{idx2}")
            self.swap_elements(array, idx1, idx2, swap)
            self.switch_history.append((idx1, idx2, swap, f"input {i}"))
            
        # Handle odd n - last element bypasses input switches
        if n % 2 == 1:
            print(f"{indent}  Odd n detected, adjusting half from {half} to {(n + 1) // 2}")
            half = (n + 1) // 2
            
        # Recursive calls on interleaved positions
        # Top subnetwork: even positions after input switches
        print(f"{indent}  Recursive call TOP: start={start}, stride={stride * 2}, n={half}")
        self.waksman_recursive(array, start, stride * 2, half, level + 1, depth + 1)
        
        # Bottom subnetwork: odd positions after input switches
        if n % 2 == 0:
            print(f"{indent}  Recursive call BOTTOM (even): start={start + stride}, stride={stride * 2}, n={half}")
            self.waksman_recursive(array, start + stride, stride * 2, half, level + 1, depth + 1)
        else:
            # For odd n, bottom network has one less element
            print(f"{indent}  Recursive call BOTTOM (odd): start={start + stride}, stride={stride * 2}, n={half - 1}")
            self.waksman_recursive(array, start + stride, stride * 2, half - 1, level + 1, depth + 1)
            
        # Output switches (one less than input for Waksman property)
        # First pair has no output switch
        num_output_switches = half - 1 if half > 0 else 0
        print(f"{indent}  Applying {num_output_switches} output switches:")
        for i in range(1, half):
            idx1 = start + (i * 2) * stride
            idx2 = start + (i * 2 + 1) * stride
            
            # Check bounds
            if idx2 >= len(array):
                print(f"{indent}    ERROR: idx2={idx2} >= len(array)={len(array)}")
                raise IndexError(f"Index {idx2} out of bounds for array of size {len(array)}")
                
            # Use different level offset to ensure different bits
            swap = self.get_switch_bit(level + 10000, idx1)
            print(f"{indent}    Output switch {i-1}: swap={swap} at positions {idx1},{idx2}")
            self.swap_elements(array, idx1, idx2, swap)
            self.switch_history.append((idx1, idx2, swap, f"output {i-1}"))
            
    def shuffle(self, array: List) -> List:
        """Main entry point for Waksman shuffle"""
        n = len(array)
        print(f"\n=== Starting Waksman shuffle for n={n} ===")
        print(f"Initial array: {array}")
        
        # Clear switch history
        self.switch_history = []
        
        # Make a copy to shuffle
        result = array.copy()
        
        # Apply Waksman shuffle
        self.waksman_recursive(result, 0, 1, n, 0)
        
        print(f"Final array: {result}")
        print(f"Total switches: {len(self.switch_history)}")
        return result


def test_waksman():
    """Test the Waksman shuffle with various sizes"""
    shuffler = WaksmanShuffle()
    
    # Test sizes that work and fail in C
    test_sizes = [2, 3, 4, 5]
    
    for n in test_sizes:
        print(f"\n{'='*60}")
        print(f"Testing n={n}")
        print('='*60)
        
        # Create test array
        array = list(range(n))
        
        try:
            result = shuffler.shuffle(array)
            
            # Check if it's a valid permutation
            if sorted(result) == array:
                print(f"✓ Valid permutation for n={n}")
            else:
                print(f"✗ INVALID result for n={n}: {result}")
                
            # Print switch details
            print(f"\nSwitch sequence for n={n}:")
            for idx1, idx2, swap, desc in shuffler.switch_history:
                print(f"  [{desc}] positions {idx1},{idx2} swap={swap}")
                
        except IndexError as e:
            print(f"✗ ERROR for n={n}: {e}")
            
    # Detailed trace for n=3 (the problematic case)
    print(f"\n{'='*60}")
    print("DETAILED TRACE FOR n=3")
    print('='*60)
    
    shuffler = WaksmanShuffle()
    array = [0, 1, 2]
    
    try:
        result = shuffler.shuffle(array)
        
        # Analyze the access pattern
        print(f"\nAccess pattern analysis for n=3:")
        max_idx = max(max(idx1, idx2) for idx1, idx2, _, _ in shuffler.switch_history)
        print(f"  Array size: 3")
        print(f"  Maximum index accessed: {max_idx}")
        print(f"  Issue: {'OUT OF BOUNDS!' if max_idx >= 3 else 'All accesses valid'}")
        
    except IndexError as e:
        print(f"Index error detected: {e}")


if __name__ == "__main__":
    test_waksman()