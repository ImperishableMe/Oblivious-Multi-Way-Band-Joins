#!/usr/bin/env python3
"""Generate large dataset for performance testing."""

import random

# Generate User.csv with 10,000 rows
with open('User.csv', 'w') as f:
    for i in range(1, 10001):
        f.write(f"{i},User{i}\n")

# Generate Follows.csv with 50,000 rows
with open('Follows.csv', 'w') as f:
    follows_set = set()
    while len(follows_set) < 50000:
        src = random.randint(1, 10000)
        dest = random.randint(1, 10000)
        if src != dest:  # No self-follows
            follows_set.add((src, dest))

    for src, dest in sorted(follows_set):
        timestamp = random.randint(2020, 2024)
        f.write(f"{src},{dest},{timestamp}\n")

print("Generated User.csv (10,000 rows) and Follows.csv (50,000 rows)")
