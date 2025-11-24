# Oblivious Multi-Way Band Join Algorithm

This document explains the oblivious multi-way band join algorithm implemented in this codebase.

## Table of Contents

1. [Overview](#overview)
2. [Key Data Structures](#key-data-structures)
3. [The Four Phases](#the-four-phases)
4. [Worked Example](#worked-example)
5. [Phase 1: Bottom-Up](#phase-1-bottom-up-compute-local-multiplicities)
6. [Phase 2: Top-Down](#phase-2-top-down-compute-final-multiplicities)
7. [Phase 3: Distribute-Expand](#phase-3-distribute-expand-replicate-tuples)
8. [Phase 4: Align-Concat](#phase-4-align-concat-construct-final-result)
9. [What Makes It Oblivious](#what-makes-it-oblivious)
10. [Mathematical Foundations](#mathematical-foundations)

---

## Overview

The algorithm performs multi-way joins while maintaining **oblivious access patterns** - meaning the sequence of memory accesses is independent of the actual data values. This is critical for security in trusted execution environments (like Intel TDX) where an adversary might observe memory access patterns.

### The Core Problem

Given tables R₁, R₂, ..., Rₖ arranged in a join tree, compute:

```
R₁ ⋈_{θ₁} R₂ ⋈_{θ₂} R₃ ⋈ ... ⋈_{θₖ₋₁} Rₖ
```

Where each θᵢ is a **band constraint**: `Rᵢ.attr ∈ [Rⱼ.attr + d₁, Rⱼ.attr + d₂]`

**Challenge:** Do this obliviously with O(N + output) complexity.

---

## Key Data Structures

### Entry Structure

The `Entry` class (`app/data_structures/entry.h`) is the core data unit:

```cpp
class Entry {
    // Type and equality metadata
    int32_t field_type;        // SORT_PADDING, SOURCE, START, END, TARGET, DIST_PADDING
    int32_t equality_type;     // EQ, NEQ, NONE

    // Join attribute
    int32_t join_attr;         // The attribute value used for join matching

    // Persistent multiplicity metadata (survives across phases)
    int32_t original_index;    // Original position in input table
    int32_t local_mult;        // Multiplicities from subtree (bottom-up)
    int32_t final_mult;        // Final multiplicities in result (top-down)
    int32_t foreign_sum;       // Count from parent's side (for alignment)

    // Temporary metadata (reused between phases)
    int32_t local_cumsum;      // Cumulative sum in bottom-up phase
    int32_t local_interval;    // Interval computation (bottom-up)
    int32_t foreign_interval;  // Foreign interval computation (top-down)
    int32_t local_weight;      // Weight tracking (top-down)

    // Expansion and alignment metadata
    int32_t copy_index;        // Which copy of tuple (0 to final_mult-1)
    int32_t alignment_key;     // Key for sorting to align tuples
    int32_t dst_idx;           // Destination index in expanded table
    int32_t index;             // Current position

    // Original data attributes
    int32_t attributes[MAX_ATTRIBUTES];  // Up to 32 integer attributes
};
```

### Entry Types

Entries have different types based on their role:

| Type | Description |
|------|-------------|
| `SOURCE` | Child tuples (or parent in top-down) |
| `START` | Lower boundary marker for dual-entry technique |
| `END` | Upper boundary marker for dual-entry technique |
| `SORT_PADDING` | Used during sorting operations |
| `DIST_PADDING` | Padding created during expansion |

### Table Structure

The `Table` class manages collections of entries and provides batched operations:
- `map`: Apply function to each entry
- `linear_pass`: Scan left-to-right with window function
- `parallel_pass`: Process aligned pairs from two tables
- `shuffle_merge_sort`: Oblivious sorting
- `distribute_pass`: Variable-distance distribution

### JoinTreeNode Structure

```cpp
class JoinTreeNode {
    std::string table_name;
    Table table_data;                          // The actual table at this node
    std::vector<JoinTreeNodePtr> children;     // Child nodes
    std::weak_ptr<JoinTreeNode> parent;        // Parent node
    JoinConstraint constraint_with_parent;     // Join condition with parent
};
```

### JoinConstraint Structure

Band joins support flexible matching ranges:

```cpp
class JoinConstraint {
    std::string source_table, target_table;
    std::string source_column, target_column;

    // Band join parameters
    // Matches: source.attr IN [target.attr + dev1, target.attr + dev2]
    int32_t deviation1, deviation2;            // Lower and upper bounds
    equality_type_t equality1, equality2;      // EQ (closed) or NEQ (open) intervals
};
```

Examples:
- Equality: `dev1=0 (EQ), dev2=0 (EQ)` → `source.attr = target.attr`
- Band: `dev1=-100 (EQ), dev2=1000 (EQ)` → `[target-100, target+1000]`
- Open: `dev1=0 (NEQ), dev2=10 (NEQ)` → `(target, target+10)`
- Half-open: `dev1=0 (EQ), dev2=10 (NEQ)` → `[target, target+10)`

---

## The Four Phases

```
┌─────────────────────────────────────────────────────────────────┐
│  1. BOTTOM-UP PHASE    → Compute local multiplicities          │
│  2. TOP-DOWN PHASE     → Compute final multiplicities          │
│  3. DISTRIBUTE-EXPAND  → Replicate tuples                      │
│  4. ALIGN-CONCAT       → Construct final result                │
└─────────────────────────────────────────────────────────────────┘
```

### Algorithm Flow

```
INPUT: Join Tree T with tables R1, R2, ..., Rk and join constraints
       Each node v in T has table R_v and join constraint with parent

PHASE 1: Bottom-Up (Compute Local Multiplicities)
  - Post-order traversal from leaves to root
  - For each table, computes local_mult: how many times each tuple appears
    in the join result of its subtree

PHASE 2: Top-Down (Compute Final Multiplicities)
  - Pre-order traversal from root to leaves
  - Propagates foreign multiplicities from parent to children
  - Computes final_mult: exact number of times each tuple appears in final result

PHASE 3: Distribute-Expand (Replicate Tuples)
  - For each table, creates final_mult copies of each tuple
  - Uses oblivious distribution algorithm with variable-distance passes
  - Result: expanded tables with each tuple replicated correctly

PHASE 4: Align-Concat (Construct Final Result)
  - Pre-order tree traversal
  - For each parent-child pair: align and concatenate horizontally
  - Alignment ensures matching tuples from parent and child appear in same rows
  - Result: final join output with all tuples correctly matched

OUTPUT: Result table containing all matching tuples from the join
```

---

## Worked Example

### Input Tables

```
Table R (Parent):          Table S (Child):
┌────┬───────┐             ┌────┬───────┐
│ id │ value │             │ id │ value │
├────┼───────┤             ├────┼───────┤
│ r1 │   5   │             │ s1 │   3   │
│ r2 │  10   │             │ s2 │   7   │
│ r3 │  15   │             │ s3 │  12   │
└────┴───────┘             └────┴───────┘
```

### Join Constraint

`S.value IN [R.value - 3, R.value + 3]` (band join with ±3)

### Expected Matches

| Parent | Range | Matching Children |
|--------|-------|-------------------|
| r1 (value=5) | [2, 8] | s1 (3), s2 (7) |
| r2 (value=10) | [7, 13] | s2 (7), s3 (12) |
| r3 (value=15) | [12, 18] | s3 (12) |

### Expected Result

5 tuples: `(r1,s1), (r1,s2), (r2,s2), (r2,s3), (r3,s3)`

---

## Phase 1: Bottom-Up (Compute Local Multiplicities)

**Goal:** For each parent tuple, count how many child tuples match it.

### The Dual-Entry Technique

The key insight is the **dual-entry technique**: instead of iterating through matches (which would be data-dependent), we create boundary markers.

For each parent tuple, create:
- **START entry**: `join_attr = R.value + deviation1 = R.value - 3`
- **END entry**: `join_attr = R.value + deviation2 = R.value + 3`

For each child tuple, create:
- **SOURCE entry**: `join_attr = S.value`

### Step 1: Create Combined Table

```
Combined Table (unsorted):
┌──────────┬───────────┬────────────┬────────────┐
│ type     │ join_attr │ local_mult │ origin     │
├──────────┼───────────┼────────────┼────────────┤
│ SOURCE   │     3     │     1      │ s1         │
│ SOURCE   │     7     │     1      │ s2         │
│ SOURCE   │    12     │     1      │ s3         │
│ START    │     2     │     -      │ r1 (5-3)   │
│ END      │     8     │     -      │ r1 (5+3)   │
│ START    │     7     │     -      │ r2 (10-3)  │
│ END      │    13     │     -      │ r2 (10+3)  │
│ START    │    12     │     -      │ r3 (15-3)  │
│ END      │    18     │     -      │ r3 (15+3)  │
└──────────┴───────────┴────────────┴────────────┘
```

### Step 2: Sort by join_attr

**Tie-breaking rule:** For entries with equal join_attr:
- If constraint is **closed** `[a, b]`: START comes **before** SOURCE, SOURCE comes **before** END
- If constraint is **open** `(a, b)`: START comes **after** SOURCE, SOURCE comes **after** END

```
Sorted Combined Table (with proper tie-breaking for closed interval):
┌─────┬──────────┬───────────┬────────────┬─────────────────┐
│ pos │ type     │ join_attr │ local_mult │ cumsum(SOURCE)  │
├─────┼──────────┼───────────┼────────────┼─────────────────┤
│  0  │ START_r1 │     2     │     -      │       0         │
│  1  │ SOURCE   │     3     │     1      │       1         │  ← s1
│  2  │ START_r2 │     7     │     -      │       1         │  ← START before SOURCE
│  3  │ SOURCE   │     7     │     1      │       2         │  ← s2
│  4  │ END_r1   │     8     │     -      │       2         │
│  5  │ START_r3 │    12     │     -      │       2         │  ← START before SOURCE
│  6  │ SOURCE   │    12     │     1      │       3         │  ← s3
│  7  │ END_r2   │    13     │     -      │       3         │
│  8  │ END_r3   │    18     │     -      │       3         │
└─────┴──────────┴───────────┴────────────┴─────────────────┘
```

### Step 3: Compute Cumulative Sum

Scan left-to-right, accumulating `local_mult` only for SOURCE entries.

### Step 4: Compute Intervals (Match Count)

For each parent, the match count = `cumsum[END] - cumsum[START]`:

```
r1: cumsum[END_r1=4] - cumsum[START_r1=0] = 2 - 0 = 2  ✓ (matches s1, s2)
r2: cumsum[END_r2=7] - cumsum[START_r2=2] = 3 - 1 = 2  ✓ (matches s2, s3)
r3: cumsum[END_r3=8] - cumsum[START_r3=5] = 3 - 2 = 1  ✓ (matches s3)
```

### Step 5: Update Parent's local_mult

```
Parent R after bottom-up:
┌────┬───────┬────────────┐
│ id │ value │ local_mult │
├────┼───────┼────────────┤
│ r1 │   5   │     2      │  (matches s1, s2)
│ r2 │  10   │     2      │  (matches s2, s3)
│ r3 │  15   │     1      │  (matches s3)
└────┴───────┴────────────┘

Child S after bottom-up (unchanged, it's a leaf):
┌────┬───────┬────────────┐
│ id │ value │ local_mult │
├────┼───────┼────────────┤
│ s1 │   3   │     1      │
│ s2 │   7   │     1      │
│ s3 │  12   │     1      │
└────┴───────┴────────────┘
```

---

## Phase 2: Top-Down (Compute Final Multiplicities)

**Goal:** Propagate multiplicities from root to leaves to compute how many times each tuple appears in the final result.

### For Root (R)

Since R is the root, `final_mult = local_mult`:

```
Parent R:
┌────┬───────┬────────────┬────────────┐
│ id │ value │ local_mult │ final_mult │
├────┼───────┼────────────┼────────────┤
│ r1 │   5   │     2      │     2      │
│ r2 │  10   │     2      │     2      │
│ r3 │  15   │     1      │     1      │
└────┴───────┴────────────┴────────────┘
```

### For Child (S)

Now we propagate from R to S using the **reversed** dual-entry technique.

**Reversed constraint:** If `S.value IN [R.value - 3, R.value + 3]`, then equivalently `R.value IN [S.value - 3, S.value + 3]`.

- For each **child** tuple, create START and END entries
- For each **parent** tuple, create SOURCE entries

### Computing final_mult for Children

For a child c that matches parents p₁, p₂, ..., pₘ:
```
c.final_mult = Σᵢ (pᵢ.final_mult / pᵢ.local_mult) × c.local_mult
```

For our example (leaf children with local_mult = 1):

```
s1: matches r1 only
    contribution = r1.final_mult / r1.local_mult = 2/2 = 1
    s1.final_mult = 1 × 1 = 1  ✓

s2: matches r1, r2
    contribution from r1 = 2/2 = 1
    contribution from r2 = 2/2 = 1
    s2.final_mult = (1 + 1) × 1 = 2  ✓

s3: matches r2, r3
    contribution from r2 = 2/2 = 1
    contribution from r3 = 1/1 = 1
    s3.final_mult = (1 + 1) × 1 = 2  ✓
```

### Result after Top-Down

```
Child S after top-down:
┌────┬───────┬────────────┬────────────┐
│ id │ value │ local_mult │ final_mult │
├────┼───────┼────────────┼────────────┤
│ s1 │   3   │     1      │     1      │
│ s2 │   7   │     1      │     2      │
│ s3 │  12   │     1      │     2      │
└────┴───────┴────────────┴────────────┘
```

**Verification:**
- Total result size = Σ s.final_mult = 1 + 2 + 2 = 5 ✓
- Total result size = Σ r.final_mult = 2 + 2 + 1 = 5 ✓

---

## Phase 3: Distribute-Expand (Replicate Tuples)

**Goal:** Create `final_mult` copies of each tuple.

### Expand Parent R

```
Before:                          After expansion:
┌────┬───────┬────────────┐      ┌────┬───────┬────────────┐
│ id │ value │ final_mult │      │ id │ value │ copy_index │
├────┼───────┼────────────┤      ├────┼───────┼────────────┤
│ r1 │   5   │     2      │  →   │ r1 │   5   │     0      │
│ r2 │  10   │     2      │      │ r1 │   5   │     1      │
│ r3 │  15   │     1      │      │ r2 │  10   │     0      │
└────┴───────┴────────────┘      │ r2 │  10   │     1      │
                                 │ r3 │  15   │     0      │
                                 └────┴───────┴────────────┘
```

### Expand Child S

```
Before:                          After expansion:
┌────┬───────┬────────────┐      ┌────┬───────┬────────────┐
│ id │ value │ final_mult │      │ id │ value │ copy_index │
├────┼───────┼────────────┤      ├────┼───────┼────────────┤
│ s1 │   3   │     1      │  →   │ s1 │   3   │     0      │
│ s2 │   7   │     2      │      │ s2 │   7   │     0      │
│ s3 │  12   │     2      │      │ s2 │   7   │     1      │
└────┴───────┴────────────┘      │ s3 │  12   │     0      │
                                 │ s3 │  12   │     1      │
                                 └────┴───────┴────────────┘
```

### The Oblivious Distribution Algorithm

The expansion uses a **variable-distance distribution** algorithm:

```
Algorithm DistributeExpand(table):
    n = table.size
    output_size = Σ final_mult[i]

    // Step 1: Compute destination indices
    dst_idx[0] = 0
    for i = 1 to n-1:
        dst_idx[i] = dst_idx[i-1] + final_mult[i-1]

    // Step 2: Pad to output_size with PADDING entries
    table.resize(output_size)
    for i = n to output_size-1:
        table[i] = PADDING

    // Step 3: Assign index field
    for i = 0 to output_size-1:
        table[i].index = i

    // Step 4: Variable-distance distribution
    d = largest_power_of_2 ≤ output_size
    while d ≥ 1:
        for i = 0 to output_size-d-1:  // Compare pairs at distance d
            ObliviousCompareSwap(table[i], table[i+d])
        d = d / 2

    // Step 5: Final linear pass to fill remaining gaps
    LinearScan(table)
```

**Key:** Same operations run regardless of data - every pair at distance d is compared.

---

## Phase 4: Align-Concat (Construct Final Result)

**Goal:** Align the expanded tables so corresponding tuples are in the same row, then concatenate horizontally.

### The Alignment Problem

After expansion:

```
Expanded R:                    Expanded S:
┌───────┬────┬────────────┐    ┌───────┬────┬────────────┐
│ index │ id │ copy_index │    │ index │ id │ copy_index │
├───────┼────┼────────────┤    ├───────┼────┼────────────┤
│   0   │ r1 │     0      │    │   0   │ s1 │     0      │
│   1   │ r1 │     1      │    │   1   │ s2 │     0      │
│   2   │ r2 │     0      │    │   2   │ s2 │     1      │
│   3   │ r2 │     1      │    │   3   │ s3 │     0      │
│   4   │ r3 │     0      │    │   4   │ s3 │     1      │
└───────┴────┴────────────┘    └───────┴────┴────────────┘
```

We need to align so matching tuples are in the same row.

### The Alignment Key Formula

For child entry c at copy_index k:
```
alignment_key = foreign_sum(c) + ⌊k / local_mult(c)⌋
```

Where:
- `foreign_sum(c)` = cumulative position in parent's expanded table
- `k` = copy_index (0 to final_mult-1)
- `local_mult(c)` = c's local multiplicity (1 for leaves)

### Computing Alignment Keys

The parent-child matching creates a **bipartite correspondence**:

```
Parent copies:              Child copies:
r1[0] ────────────────────► s1[0]
r1[1] ────────────────────► s2[0]
r2[0] ────────────────────► s2[1]
r2[1] ────────────────────► s3[0]
r3[0] ────────────────────► s3[1]
```

Alignment keys for children:
```
s1[0]: alignment_key = 0  → aligns with r1[0]
s2[0]: alignment_key = 1  → aligns with r1[1]
s2[1]: alignment_key = 2  → aligns with r2[0]
s3[0]: alignment_key = 3  → aligns with r2[1]
s3[1]: alignment_key = 4  → aligns with r3[0]
```

### Sort and Concatenate

After sorting child by alignment_key and horizontal concatenation:

```
Final Result:
┌───┬───────┬─────────┬───────┬─────────┐
│ # │ R.id  │ R.value │ S.id  │ S.value │
├───┼───────┼─────────┼───────┼─────────┤
│ 0 │  r1   │    5    │  s1   │    3    │  ← r1 joins s1 (5 ∈ [0,6] ✓)
│ 1 │  r1   │    5    │  s2   │    7    │  ← r1 joins s2 (5 ∈ [4,10] ✓)
│ 2 │  r2   │   10    │  s2   │    7    │  ← r2 joins s2 (10 ∈ [4,10] ✓)
│ 3 │  r2   │   10    │  s3   │   12    │  ← r2 joins s3 (10 ∈ [9,15] ✓)
│ 4 │  r3   │   15    │  s3   │   12    │  ← r3 joins s3 (15 ∈ [9,15] ✓)
└───┴───────┴─────────┴───────┴─────────┘
```

---

## What Makes It Oblivious

The algorithm is **oblivious** because all memory access patterns are **data-independent**.

### 1. Dual-Entry Technique

Instead of iterating "for each match":
- Create boundary markers (START/END) for every tuple
- Sort all entries together (fixed comparison network)
- Use cumulative sums to count matches

**Same operations run whether there are 0 matches or 1000 matches.**

### 2. Fixed Sorting Networks

- Sorting uses oblivious sorting (comparison-based with fixed comparison sequence)
- Every comparison happens regardless of the data values
- No early termination

### 3. Linear Passes

- Always scan the entire array left-to-right
- Apply the same operation at every position
- No skipping or conditional branches based on data

### 4. Variable-Distance Distribution

- Fixed sequence of distances: n/2, n/4, n/8, ..., 1
- Compare all pairs at each distance
- Padding entries ensure constant-time operations

### 5. Parallel Passes for Concatenation

- Always process all aligned pairs
- Same operation at every position

### Security Result

An attacker observing memory accesses learns nothing about:
- Which tuples matched
- How many matches occurred
- What the data values were

---

## Mathematical Foundations

### Multiplicity Definitions

For a node v with table Rᵥ in the join tree:

**local_mult(t):** Number of tuples in the **subtree rooted at v** that join with tuple t.
- For a **leaf node**: `local_mult(t) = 1`
- For an **internal node**: Product of matching children's local multiplicities

**foreign_mult(t):** Number of times t appears due to matches **outside** its subtree.
- For **root**: `foreign_mult(t) = 1`

**final_mult(t):** Total appearances in the final result.
```
final_mult(t) = local_mult(t) × foreign_mult(t)
```

### The Dual-Entry Counting Formula

For a parent-child pair (P, C) with constraint `C.attr ∈ [P.attr + d₁, P.attr + d₂]`:

```
Combined = {(SOURCE, c.attr, c.local_mult) : c ∈ C}
         ∪ {(START, p.attr + d₁, p) : p ∈ P}
         ∪ {(END, p.attr + d₂, p) : p ∈ P}
```

After sorting by join_attr:
```
match_count(p) = cumsum[END_p] - cumsum[START_p]
```

Update rule:
```
p.local_mult = p.local_mult × match_count(p)
```

### Foreign Multiplicity Computation

For child c matching parents p₁, p₂, ..., pₘ:
```
foreign_mult(c) = Σᵢ (pᵢ.final_mult / pᵢ.local_mult)
```

Then:
```
c.final_mult = c.local_mult × foreign_mult(c)
```

### Alignment Key Formula

For child entry c with copy_index k, local_mult l, and foreign_sum f:
```
alignment_key = f + ⌊k / l⌋
```

This ensures each child copy aligns with the correct parent copy in the expanded tables.

---

## Multi-Way Extension

For 3+ tables, the algorithm extends naturally:

```
     R (root)
    / \
   S   T

Bottom-Up: S→R, T→R (post-order: S, T, R)
Top-Down:  R→S, R→T (pre-order: R, S, T)
Expand:    R, S, T
Align:     R ⋈ S → RS, RS ⋈ T → RST
```

Each join in the tree is processed independently using the same four phases, maintaining obliviousness throughout.

---

## Summary

```
┌─────────────────────────────────────────────────────────────────┐
│ INPUT: Tables R, S with constraint S.val ∈ [R.val-3, R.val+3]  │
├─────────────────────────────────────────────────────────────────┤
│ BOTTOM-UP: Count matches using dual-entry technique            │
│   R.local_mult = [2, 2, 1]  (r1→2, r2→2, r3→1)                │
│   S.local_mult = [1, 1, 1]  (leaves)                           │
├─────────────────────────────────────────────────────────────────┤
│ TOP-DOWN: Propagate from root to leaves                        │
│   R.final_mult = [2, 2, 1]  (root)                             │
│   S.final_mult = [1, 2, 2]  (s1→1, s2→2, s3→2)                │
├─────────────────────────────────────────────────────────────────┤
│ DISTRIBUTE-EXPAND: Create copies                               │
│   R_expanded = [r1,r1,r2,r2,r3] (5 entries)                    │
│   S_expanded = [s1,s2,s2,s3,s3] (5 entries)                    │
├─────────────────────────────────────────────────────────────────┤
│ ALIGN-CONCAT: Sort child by alignment_key, concatenate         │
│   alignment_keys = [0, 1, 2, 3, 4]                             │
│   Result: [(r1,s1), (r1,s2), (r2,s2), (r2,s3), (r3,s3)]       │
└─────────────────────────────────────────────────────────────────┘
```

---

## Code References

- Main orchestrator: `app/algorithms/oblivious_join.cpp`
- Bottom-up phase: `app/algorithms/bottom_up_phase.cpp`
- Top-down phase: `app/algorithms/top_down_phase.cpp`
- Distribute-expand: `app/algorithms/distribute_expand.cpp`
- Align-concat: `app/algorithms/align_concat.cpp`
- Entry structure: `app/data_structures/entry.h`
- Table operations: `app/data_structures/table.h`
