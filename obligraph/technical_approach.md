# Technical Approach: Oblivious Property Graph Database
## Comprehensive Reference Document

**Authors**: Bishwajit Bhattacharjee, Nafis Ahmed
**Institution**: University of Waterloo
**Date**: 2025

---

## Table of Contents
1. [Threat Model and Assumptions](#threat-model)
2. [Background: The Evolution of Our Approach](#background)
3. [The 1-Hop Operator](#one-hop-operator)
4. [The Hybrid Optimization Strategy](#hybrid-strategy)
5. [Detailed Examples](#examples)
6. [Security Analysis](#security)
7. [Performance Benefits](#performance)
8. [Generalization](#generalization)

---

## 1. Threat Model and Assumptions {#threat-model}

### 1.1 Adversarial Model

We adopt the commonly used threat model in enclave-based oblivious systems:

**Adversary capabilities:**
- ✅ Observe encrypted network traffic between client and server
- ✅ Manage software stack outside the enclave
- ✅ Monitor memory accesses inside and outside the enclave at cache line granularity
- ✅ Launch side-channel attacks: cache attacks, branch prediction attacks, paging-based attacks, memory bus snooping
- ❌ Cannot breach the secure processor or access its secret key
- ❌ Cannot tamper with data or deviate from protocol (semi-honest adversary)

### 1.2 Public vs. Secret Information

**Public (non-sensitive) information:**
- Graph schema (node labels, edge labels, property types)
- **Base table sizes**: Number of nodes per label (|Person|, |Org|, etc.), number of edges per label (|WorksAt|, |LocatedIn|, etc.)
- Query type/structure being executed
- **Final output size** returned to the client

**Secret (sensitive) information:**
- Actual data values (node/edge properties)
- Data access patterns during query execution
- **Intermediate result sizes** during query processing
- Control flow patterns that depend on data

### 1.3 Key Principle

**Doubly oblivious execution**: Programs running in enclaves must ensure:
1. **Data obliviousness**: Memory access patterns are independent of actual data values
2. **Query obliviousness**: Control flow is independent of query predicates and data

This prevents adversaries from inferring sensitive information through side-channel observations.

---

## 2. Background: The Evolution of Our Approach {#background}

### 2.1 The Original Challenge

Multi-hop graph queries naturally require joining multiple node and edge tables. Consider a 2-hop query:

```cypher
MATCH (p:Person)-[w:WorksAt]->(o:Org)-[l:LocatedIn]->(c:City)
WHERE c.name = 'Berlin'
RETURN p.name, o.name
```

**Traditional relational execution:**
```
Step 1: Person ⋈ WorksAt → Intermediate₁
Step 2: Intermediate₁ ⋈ Org → Intermediate₂
Step 3: Intermediate₂ ⋈ LocatedIn → Intermediate₃
Step 4: Intermediate₃ ⋈ City → Final Result
```

**Problem**: The sizes |Intermediate₁|, |Intermediate₂|, |Intermediate₃| leak sensitive information about:
- How many people work at organizations (join selectivity)
- How many person-org pairs are associated with locations
- Data distribution patterns

Even though base table sizes (|Person|, |WorksAt|, |Org|, |LocatedIn|, |City|) are public, these intermediate sizes are **data-dependent and sensitive**.

### 2.2 Old Approach: Differential Privacy (Research Question 3 in Vision Paper)

**Idea**: Use differential privacy (DP) to add noise to intermediate result sizes, obfuscating the exact counts.

**Challenges:**
- ❌ **Privacy budget consumption**: Each query consumes privacy budget ε; accumulated budget grows with query workload
- ❌ **Noise accumulation**: Multiple operators in a query require noise at each step; noise compounds
- ❌ **Query sensitivity amplification**: Joins across multiple tables increase sensitivity, requiring larger noise
- ❌ **Caching complexity**: Existing DP caching (Turbo, cacheDP) only applies to linear queries over single tables, not complex multi-table joins

### 2.3 The Breakthrough: Oblivious Acyclic Multi-Way Joins

**Recent work** (referenced as Ruidi et al. / Hu & Wu 2025 in the papers):

For acyclic join graphs, there exists efficient oblivious join algorithms that **provably hide all intermediate result sizes** without resorting to worst-case padding.

**Key insight**: Acyclic structure allows carefully ordered join evaluation where intermediate results can be bounded/hidden through algorithmic guarantees rather than padding to worst-case size.

**Example worst-case padding problem:**
- For 2-hop query spanning edges e₁ and e₂
- Naïve worst-case padding: |e₁| × |e₂| (if all e₁ edges connect to all e₂ edges)
- For |e₁| = 1M, |e₂| = 1M: worst-case = 1 trillion tuples
- Actual result might be only 1000 tuples
- This padding "significantly degrades performance"

**Ruidi's contribution**: Avoids this catastrophic padding for acyclic joins.

**Limitation**: Operates on generic relational tables; doesn't exploit property graph structure.

### 2.4 Our Innovation: The Pivot

**Core idea**: Enhance Ruidi's acyclic join framework with **graph-native optimizations** that exploit property graph semantics.

**Key observation**: Our 1-hop operator has unique properties perfectly suited for this hybrid approach.

---

## 3. The 1-Hop Operator {#one-hop-operator}

### 3.1 Definition

**Input**: Three consecutive base tables forming the pattern:
```
BaseNode₁ → BaseEdge → BaseNode₂
```

**Example**: `Person → WorksAt → Org`

**Output**: A single table containing:
- Attributes from BaseNode₁ (needed for predicates/projection)
- Attributes from BaseEdge (needed for predicates/projection)
- Attributes from BaseNode₂ (needed for predicates/projection)
- Join key: BaseNode₂.id (for subsequent joins)

**Critical property**: Output size = |BaseEdge| (public base table size)

### 3.2 Algorithm Overview

**Step 1**: Build oblivious hash map for source nodes
- Scan BaseNode₁, extract relevant attributes
- Construct oblivious hash map O_src: key = node_id, value = attributes

**Step 2**: Process forward edge index
- Scan BaseEdge (forward index sorted by source_id)
- For each edge, look up source node attributes from O_src using **duplicate suppression**
- Concatenate source attributes + edge attributes → Vector V₁

**Step 3**: Process reverse edge index
- Build oblivious hash map O_dest for BaseNode₂
- Scan BaseEdge (reverse index sorted by dest_id)
- For each edge, look up destination node attributes from O_dest using **duplicate suppression**
- Create Vector V₂ with (dest_id, source_id, dest_attributes)

**Step 4**: Merge vectors
- Sort V₂ by (source_id, dest_id) to align with V₁
- Merge V₁ and V₂ to obtain unified vector V

**Step 5**: Filter and compact
- Evaluate query predicates on each entry in V
- Obliviously compact to remove non-matching entries

### 3.3 Duplicate Suppression (Critical for Obliviousness)

**Problem**: Without duplicate suppression, repeatedly accessing the same hash map key reveals node degree distribution.

**Example**:
- Edge table: [(0→1), (0→2), (0→3), (1→2)]
- Naïve approach: Access O_src[0] three times, O_src[1] once
- **Leakage**: Adversary observes node 0 has degree 3, node 1 has degree 1

**Solution**:
1. **Deduplication**: In sorted edge list by source_id, replace duplicate keys with DUMMY
   - [0, 0, 0, 1] → [0, DUMMY, DUMMY, 1]
2. **Hash map access**: Each key accessed exactly once
   - DUMMY keys map to random locations (no correlation)
3. **Expansion**: After hash map lookup, restore duplicates by forward-filling
   - [attr₀, DUMMY_RESULT, DUMMY_RESULT, attr₁] → [attr₀, attr₀, attr₀, attr₁]

**Result**: Uniform access pattern; no degree distribution leakage.

### 3.4 Performance

**Complexity**: 1 oblivious sort (to align V₂ with V₁)

**Comparison**:
- Traditional relational join approach: 4 oblivious sorts (for concatenation + merge in two binary joins)
- Our 1-hop operator: 1 oblivious sort
- **Improvement**: ~4× faster for this specific pattern

---

## 4. The Hybrid Optimization Strategy {#hybrid-strategy}

### 4.1 High-Level Approach

For any acyclic multi-hop query over property graph base tables:

**Phase 1: Graph-Native Optimization**
1. Identify all maximal **disjoint triplets** of base tables matching pattern `(BaseNode → BaseEdge → BaseNode)`
2. Apply 1-hop operator to each triplet
3. Each 1-hop output has size = |BaseEdge| (public information)

**Phase 2: Acyclic Join Completion**
4. Construct reduced join graph using:
   - 1-hop outputs (public-sized tables)
   - Remaining base tables not consumed by 1-hop operations
5. Apply Ruidi's oblivious acyclic multi-way join algorithm
6. Final output size is public (allowed by threat model)

### 4.2 Why This Works

**Security guarantee:**
- All inputs to Phase 2 have **public sizes** (either base table sizes or 1-hop output sizes = base edge table sizes)
- Ruidi's algorithm **hides intermediate results** during join processing
- Only **final output size** is revealed (permitted by threat model)
- **No sensitive intermediate sizes leaked**

**Performance benefit:**
- Each 1-hop operation is more efficient than equivalent 3-table join in Ruidi's framework
- Reduces join graph complexity (fewer nodes/edges for Ruidi to process)
- Exploits graph structure: forward/reverse indexes enable parallel bidirectional traversal

### 4.3 Critical Constraint

**Constraint**: Only **base tables** can participate in 1-hop operations, not intermediate results.

**Rationale**:
- Base table sizes are public (part of threat model)
- Intermediate result sizes are sensitive (reveal data distribution)
- If we used intermediate result in 1-hop, its size might leak information

**Implication**: We can only apply 1-hop to triplets in the **original query graph**, not to outputs of previous 1-hop operations.

---

## 5. Detailed Examples {#examples}

### 5.1 Example 1: Simple 2-Hop Query

**Query**: "Find people working at organizations located in Berlin"

```cypher
MATCH (p:Person)-[w:WorksAt]->(o:Org)-[l:LocatedIn]->(c:City)
WHERE c.name = 'Berlin'
RETURN p.name, o.name
```

**Original join graph (5 base tables):**
```
Person → WorksAt → Org → LocatedIn → City
(n₁)      (e₁)     (n₂)     (e₂)      (n₃)
```

**Step 1: Identify disjoint triplets**

**Triplet 1**: `(Person → WorksAt → Org)` = `(n₁ → e₁ → n₂)`
**Triplet 2**: `(Org → LocatedIn → City)` = `(n₂ → e₂ → n₃)`

**Problem**: These triplets **overlap** at Org (n₂)!

**Solution**: Choose non-overlapping triplets:

**Option A**:
- Triplet 1: `(Person → WorksAt → Org)`
- Remaining: `Org → LocatedIn → City`

**Option B**:
- Triplet 2: `(Org → LocatedIn → City)`
- Remaining: `Person → WorksAt → Org`

Either works! Let's proceed with **Option B**:

**Step 2: Apply 1-hop to Triplet 2**

```
OneHop(Org, LocatedIn, City) → T₁
```

**T₁ properties:**
- Size: |LocatedIn| = |e₂| (public)
- Schema: Contains Org.id, LocatedIn attributes, City attributes
- Represents: (Org, City) pairs connected by LocatedIn edges

**Step 3: Reduced join graph**

```
Person → WorksAt → T₁
(n₁)      (e₁)
```

**Join condition**: `WorksAt.dest_id = T₁.Org_id`

**Step 4: Apply Ruidi's acyclic join**

```
Ruidi-Join(Person, WorksAt, T₁) → Final Result
```

**Inputs to Ruidi:**
- Person: size |Person| (public)
- WorksAt: size |WorksAt| (public)
- T₁: size |LocatedIn| (public)

**Output**: Final result size is public (allowed by threat model)

**Security analysis:**
- ✅ T₁ size is public (= |LocatedIn|)
- ✅ Ruidi's algorithm hides intermediate results during 3-table join
- ✅ Final output size revealed (permitted)
- ✅ **No sensitive intermediate leakage**

**Performance:**
- 1 oblivious sort (in OneHop)
- X sorts in Ruidi's 3-table join
- Total: 1 + X sorts

**Comparison to pure Ruidi on original 5 tables:**
- Y sorts (for 5-table join)
- Claim: 1 + X < Y

### 5.2 Example 2: 3-Hop Linear Query

**Query**: Extended path across 4 node types

```cypher
MATCH (n₁)-[e₁]->(n₂)-[e₂]->(n₃)-[e₃]->(n₄)
RETURN n₁.attr, n₄.attr
```

**Original join graph (7 base tables):**
```
n₁ → e₁ → n₂ → e₂ → n₃ → e₃ → n₄
```

**Step 1: Identify maximal disjoint triplets**

**Triplet 1**: `(n₁ → e₁ → n₂)`
**Triplet 2**: `(n₃ → e₃ → n₄)`

These are **disjoint** (no overlapping base tables)! ✅

**Step 2: Apply 1-hop to each triplet**

```
OneHop(n₁, e₁, n₂) → T₁
  Size: |e₁| (public)
  Schema: n₁ attributes, e₁ attributes, n₂.id

OneHop(n₃, e₃, n₄) → T₂
  Size: |e₃| (public)
  Schema: n₃.id, e₃ attributes, n₄ attributes
```

**Step 3: Reduced join graph**

```
T₁ → e₂ → T₂
```

**Join conditions:**
- `T₁.n₂_id = e₂.source_id`
- `e₂.dest_id = T₂.n₃_id`

**Step 4: Apply Ruidi's acyclic join**

```
Ruidi-Join(T₁, e₂, T₂) → Final Result
```

**Inputs:**
- T₁: size |e₁| (public)
- e₂: size |e₂| (public)
- T₂: size |e₃| (public)

**Output**: Final result (public size)

**Security analysis:**
- ✅ All inputs have public sizes
- ✅ Reduced from 7-table to 3-table join
- ✅ Intermediate results during Ruidi's join are hidden
- ✅ Only final output size revealed

**Performance:**
- 2 oblivious sorts (one per 1-hop: T₁ and T₂)
- X sorts in Ruidi's 3-table join
- Total: 2 + X sorts

**Comparison:**
- Pure Ruidi on 7 tables: Y sorts
- Claim: 2 + X < Y (smaller join graph + optimized 1-hops)

### 5.3 Example 3: 5-Hop Linear Query

**Query**: Long path

```
n₁ → e₁ → n₂ → e₂ → n₃ → e₃ → n₄ → e₄ → n₅ → e₅ → n₆
```

**Maximal disjoint triplets:**

**Triplet 1**: `(n₁ → e₁ → n₂)` → T₁, size |e₁|
**Triplet 2**: `(n₃ → e₃ → n₄)` → T₂, size |e₃|
**Triplet 3**: `(n₅ → e₅ → n₆)` → T₃, size |e₅|

**Reduced join graph:**

```
T₁ → e₂ → T₂ → e₄ → T₃
```

**Ruidi's final join:**

```
Ruidi-Join(T₁, e₂, T₂, e₄, T₃) → Final Result
```

**Inputs**: All have public sizes (|e₁|, |e₂|, |e₃|, |e₄|, |e₅|)

**Performance:**
- 3 oblivious sorts (one per 1-hop)
- X sorts in Ruidi's 5-table join
- Total: 3 + X sorts
- Original: Y sorts for 11-table join
- Reduction: 11 tables → 5 tables

**Pattern**: For an n-hop linear query (2n+1 base tables):
- Can create n//2 disjoint 1-hop operations (alternating triplets)
- Reduces to approximately (n+1) tables for Ruidi's join

### 5.4 Example 4: Star Query (Non-Linear)

**Query**: Central node with multiple relationships

```
         → [e₁] → (n₂)
        /
(n₀) → [e₀] → (n₁) → [e₃] → (n₃)
        \
         → [e₂] → (n₄)
```

**Original join graph**: Star pattern, acyclic ✅

**Disjoint triplets:**

**Triplet 1**: `(n₀ → e₀ → n₁)` → T₁, size |e₀|
**Triplet 2**: `(n₁ → e₃ → n₃)` → T₂, size |e₃|

**Cannot include**:
- `(n₁ → e₁ → n₂)` - overlaps with Triplet 1 at n₁
- `(n₁ → e₂ → n₄)` - overlaps with Triplet 1 at n₁

**Reduced join graph:**

```
T₁ connects to e₁, T₂, e₂
```

**Ruidi's join:**

```
Ruidi-Join(T₁, e₁, n₂, T₂, e₂, n₄) → Final Result
```

**6 tables instead of original 8**

**Key insight**: Even for non-linear (but acyclic) join graphs, we can identify disjoint triplets and reduce complexity.

---

## 6. Security Analysis {#security}

### 6.1 Security Guarantees

**Theorem (Informal)**: The hybrid 1-hop + acyclic join approach leaks no more information than:
1. Base table sizes (public by threat model)
2. Final output size (public by threat model)
3. Query structure (public by threat model)

**Proof sketch:**

**Phase 1 (1-hop operations):**
- Each 1-hop processes only base tables (public sizes)
- Uses oblivious hash maps with duplicate suppression (hides access patterns)
- Uses oblivious sorting (hides data distribution)
- Output size = base edge table size (public)
- **No leakage** ✅

**Phase 2 (Acyclic join):**
- Inputs: 1-hop outputs (public sizes) + remaining base tables (public sizes)
- Ruidi's algorithm provably hides intermediate result sizes for acyclic joins
- Only final output size revealed (public by threat model)
- **No intermediate leakage** ✅

**Conclusion**: Total leakage = base sizes + final output size (exactly what threat model permits)

### 6.2 Comparison to Alternatives

**Alternative 1: Traditional relational joins**
- ❌ Leaks multiple intermediate result sizes
- ❌ Each binary join reveals join selectivity

**Alternative 2: Pure Ruidi on original tables**
- ✅ Hides intermediate results
- ❌ Less efficient (larger join graph, more oblivious operations)

**Alternative 3: Differential privacy**
- ❌ Consumes privacy budget
- ❌ Noise accumulation
- ❌ Complex caching for multi-table queries

**Our approach:**
- ✅ No intermediate leakage
- ✅ More efficient than pure Ruidi
- ✅ No privacy budget consumption
- ✅ Tailored to graph structure

### 6.3 Threat Model Compliance

**Public information revealed:**
- ✅ Base table sizes: Explicitly allowed
- ✅ Query structure: Explicitly allowed
- ✅ Final output size: Explicitly allowed

**Secret information protected:**
- ✅ Data values: Encrypted
- ✅ Memory access patterns: Oblivious primitives (hash maps, sorting, compaction)
- ✅ Intermediate result sizes: Hidden by 1-hop design + Ruidi's guarantees
- ✅ Control flow: Data-independent (doubly oblivious)

---

## 7. Performance Benefits {#performance}

### 7.1 Complexity Analysis

**For an n-hop linear query:**

**Traditional relational approach:**
- Number of tables: 2n + 1
- Number of binary joins: 2n
- Each join requires: ~4 oblivious sorts (concatenate + merge)
- **Total**: ~8n oblivious sorts

**Pure Ruidi acyclic join:**
- Number of tables: 2n + 1
- Acyclic join algorithm: Y sorts (depends on algorithm specifics)
- **Total**: Y sorts (Y < 8n, but still significant)

**Our hybrid approach:**
- Number of 1-hop operations: n // 2 (alternating triplets)
- Each 1-hop: 1 oblivious sort
- Reduced join graph size: ~(n + 1) tables
- Ruidi's join on reduced graph: X sorts
- **Total**: (n // 2) + X sorts

**Performance claim**: (n // 2) + X < Y < 8n

### 7.2 Concrete Example: 5-Hop Query

**Setup:**
- 11 base tables (6 nodes, 5 edges)
- Each base table: 1M rows

**Traditional relational:**
- 10 binary joins × 4 sorts/join = 40 oblivious sorts
- Each sort on ~1-2M rows (with intermediate growth)

**Pure Ruidi:**
- 11-table acyclic join
- Estimate: 20-30 oblivious sorts (depends on algorithm)

**Our hybrid:**
- 3 × 1-hop operations = 3 oblivious sorts
- 5-table Ruidi join = ~10 oblivious sorts
- **Total**: ~13 oblivious sorts

**Speedup**: 2-3× faster than pure Ruidi, 3× faster than relational

### 7.3 Additional Performance Benefits

**Parallelization:**
- All 1-hop operations are **independent** (disjoint triplets)
- Can be executed in **parallel** on multiple cores
- Reduces wall-clock time significantly

**Cache locality:**
- Forward/reverse edge indexes are pre-sorted
- Sequential scans have good cache performance
- Oblivious hash maps (Snoopy) designed for efficiency

**Reduced memory footprint:**
- Smaller intermediate tables (1-hop outputs vs. full join intermediates)
- Lower memory pressure in enclave (limited enclave memory is a known bottleneck)

---

## 8. Generalization {#generalization}

### 8.1 Extension to Non-Linear Acyclic Graphs

**Key insight**: The approach works for **any acyclic join graph**, not just linear paths.

**Algorithm**:
1. Given acyclic join graph over base tables
2. Identify maximal set of disjoint triplets `(Node → Edge → Node)` from base tables
3. Apply 1-hop to each triplet → get public-sized outputs
4. Construct reduced join graph using 1-hop outputs + remaining base tables
5. Apply Ruidi's acyclic join to reduced graph

**Example: Tree-shaped query**

```
            (n₂) ← [e₂] ← (n₁)
           /
         [e₀]
         /
      (n₀) → [e₁] → (n₃) → [e₃] → (n₄)
```

**Disjoint triplets:**
- `(n₁ → e₂ → n₂)` → T₁
- `(n₃ → e₃ → n₄)` → T₂

**Reduced graph:**
```
T₁ ← (n₁) ← [e₀] ← (n₀) → [e₁] → T₂
```

**Result**: 7 tables reduced to 5 tables, 2 optimized 1-hops

### 8.2 Extension to Queries with Filters

**Challenge**: Predicates on nodes/edges might further reduce selectivity.

**Solution**:
- Apply predicates **within** 1-hop operator (Step 5: filter and compact)
- Output is still bounded by |BaseEdge|, but may be smaller (compacted)
- Final output size reveals how many tuples satisfy all predicates (allowed by threat model)

**Example:**

```cypher
MATCH (p:Person)-[w:WorksAt]->(o:Org)-[l:LocatedIn]->(c:City)
WHERE p.age > 30 AND c.name = 'Berlin'
RETURN p.name
```

**1-hop processing:**
- `OneHop(Org, LocatedIn, City)` includes filter `c.name = 'Berlin'`
- Output T₁ is compacted (only Berlin cities)
- Size still ≤ |LocatedIn|, but compaction ratio reveals selectivity of filter
- **This is acceptable**: Final output will reveal this anyway

### 8.3 Limitations and Future Work

**Current limitations:**

**1. Acyclic join graphs only:**
- Approach relies on Ruidi's acyclic join algorithm
- **Cyclic queries** (e.g., triangle patterns, cycles in social networks) require different techniques
- Future work: Extend to oblivious worst-case optimal joins (WCOJ) for cyclic patterns

**2. Base table constraint:**
- 1-hop can only process base tables, not intermediate results
- For very long queries, might have many small 1-hops but still large final Ruidi join
- Future work: Investigate "virtual base table" concept for selected intermediate results

**3. Optimal triplet selection:**
- Current approach: greedy selection of disjoint triplets
- May not always minimize total cost
- Future work: Optimization algorithm for triplet selection (graph partitioning problem)

### 8.4 Research Questions Addressed

**RQ1** (from original vision paper): Efficient oblivious binary joins without concatenation
- ✅ Addressed by 1-hop operator (doesn't use traditional concatenate-and-sort join)

**RQ2**: Parallelizable 1-hop operators without relying on binary joins
- ✅ Addressed by 1-hop operator with forward/reverse index exploitation

**RQ3**: Hiding intermediate results efficiently without worst-case padding
- ✅ **NEW SOLUTION**: Hybrid approach eliminates sensitive intermediates entirely (1-hop outputs have public sizes; Ruidi hides the rest)

**RQ4**: Practical oblivious WCOJ for cyclic patterns
- ⚠️ **Future work**: Current approach limited to acyclic; cyclic queries remain open

---

## 9. Summary

### 9.1 The Complete Picture

**Problem**: Oblivious property graph query processing with no intermediate size leakage

**Solution**: Hybrid 1-hop + acyclic join approach

**Key components:**
1. **Graph-optimized storage**: Columnar format with forward/reverse edge indexes
2. **1-hop operator**: Optimized for `(Node → Edge → Node)` pattern, outputs public-sized tables
3. **Strategic decomposition**: Identify disjoint base table triplets for 1-hop processing
4. **Acyclic join completion**: Use Ruidi's algorithm on reduced graph

**Security**: No leakage beyond base table sizes and final output size

**Performance**: Fewer oblivious sorts, smaller join graphs, parallelizable 1-hop operations

**Applicability**: Any acyclic multi-hop query over property graphs

### 9.2 Contribution Summary

**Systems contribution:**
- First oblivious property graph database supporting arbitrary Cypher queries on acyclic join patterns

**Algorithmic contribution:**
- Novel hybrid approach combining graph-native 1-hop optimization with generic acyclic multi-way joins

**Performance contribution:**
- Reduced complexity compared to pure relational or pure acyclic join approaches
- Demonstrated feasibility through proof-of-concept implementation

**Security contribution:**
- Provably no intermediate size leakage while maintaining efficiency
- No privacy budget consumption (unlike DP-based approaches)

---

## 10. Future Directions

1. **Cyclic query support**: Extend to oblivious WCOJ with practical implementation
2. **Adaptive optimization**: Cost-based query planning for triplet selection
3. **Distributed execution**: Scale to very large graphs across multiple enclaves
4. **Incremental updates**: Support for graph mutations while maintaining obliviousness
5. **Broader query language**: Support for graph algorithms (shortest path, PageRank) in oblivious setting

---

**End of Technical Approach Document**
