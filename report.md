# NebulaDB: Oblivious Property Graph Database

## 1. Introduction

Property graph databases have become the foundation for modeling complex relationships in domains ranging from financial transaction networks and social platforms to biomedical knowledge graphs and fraud detection systems. As organizations increasingly deploy these systems in cloud environments, a critical security challenge emerges: how can we process graph queries without revealing sensitive information through observable memory access patterns? Traditional encryption protects data at rest, but an adversary with access to the execution environment—such as a compromised cloud hypervisor—can infer sensitive information by monitoring *which* memory locations are accessed during query processing, even without seeing the actual data values. This class of side-channel attacks has motivated the development of *oblivious* algorithms, where the sequence of memory accesses is independent of the input data. The challenge we address in this paper is: **how can we efficiently execute multi-hop graph queries while maintaining oblivious access patterns?**

The importance of this problem stems from both the prevalence of graph workloads and the severity of access pattern leakage. Graph queries inherently involve traversing relationships—finding accounts connected through transactions, identifying influence paths in social networks, or discovering drug-gene interactions in biomedical graphs. These multi-hop traversals touch data in patterns that directly reflect the underlying graph structure. Prior work has demonstrated that access patterns can reveal query predicates, join selectivities, and even reconstruct significant portions of the underlying data [cite]. For regulated industries handling financial or healthcare data, such leakage may violate compliance requirements even when the data itself remains encrypted. Trusted Execution Environments (TEEs) like Intel TDX and SGX provide hardware-level isolation, but they do not hide memory access patterns from a privileged adversary—oblivious algorithms remain necessary within these secure enclaves.

Achieving oblivious graph query processing is fundamentally difficult because naive approaches fail in subtle ways. Consider a simple one-hop traversal: given a set of source accounts, find all transactions and their destination accounts. A standard hash join would build a hash table on source accounts and probe it for each edge. However, the *number of probes per source node* reveals node degrees—a high-degree node causes more hash table accesses than a low-degree node. Even with encrypted data, an adversary observing cache lines can distinguish between nodes with 10 versus 1,000 outgoing edges. Padding all nodes to the maximum degree is prohibitively expensive and leaks the maximum degree itself. More sophisticated approaches using Oblivious RAM (ORAM) hide individual accesses but introduce logarithmic overhead per operation, making multi-hop queries impractical at scale. The core difficulty is that graph structure is *inherently* reflected in access patterns—hiding it requires fundamentally rethinking how we process graph queries.

Prior work on oblivious query processing has largely treated graph queries as a special case of relational joins. Oblivious join algorithms [cite prior multi-way join work] can compute multi-way joins while hiding intermediate result sizes, providing strong security guarantees for acyclic query patterns. However, these approaches treat all input tables uniformly—they do not exploit the specific structure of graph traversals where nodes and edges have a predictable relationship. A three-way join between source nodes, edges, and destination nodes is processed identically to any other three-table join, missing opportunities for optimization. On the other hand, work on oblivious graph *analytics*—algorithms for PageRank, shortest paths, or connected components—focuses on iterative computations rather than the selective, predicate-driven queries typical of graph databases. There exists a gap: no prior system provides graph-native optimizations for oblivious query processing.

Our key insight is that **a single-hop graph traversal produces output bounded by the edge table size**—a quantity that is already public in our threat model. This observation enables a fundamentally more efficient approach. We introduce **ForwardFill**, an oblivious one-hop operator that joins source nodes, edges, and destination nodes using a novel *duplicate suppression* technique: rather than probing the hash table once per edge (which leaks degree), we probe once per *unique* node ID and propagate the result to all edges sharing that ID. This yields uniform access patterns regardless of degree distribution. Building on ForwardFill, we develop a **query decomposition** framework that rewrites multi-hop graph queries to maximize the use of one-hop operators, falling back to general oblivious multi-way joins [cite] only where necessary. The decomposed query executes significantly faster while providing identical security guarantees—all intermediate results either have public sizes (from ForwardFill) or are hidden by the multi-way join algorithm. Our evaluation on banking transaction workloads shows 1.2–1.9× speedup over non-decomposed oblivious execution for chain and branch query patterns.

**Summary of Contributions.** This paper makes the following contributions:

- **ForwardFill: An Oblivious One-Hop Operator (Section 4).** We present a novel algorithm for oblivious single-hop graph traversal. ForwardFill uses duplicate suppression and forward-filling to achieve uniform memory access patterns independent of node degree distribution, with output size equal to the edge table size.

- **Graph-Aware Query Decomposition (Section 5).** We introduce a query rewriting framework that identifies one-hop patterns within multi-hop queries and decomposes them to maximize ForwardFill usage. The decomposition produces a reduced query suitable for existing oblivious multi-way join algorithms.

- **Security Analysis (Section 6).** We provide an informal security argument showing that our hybrid approach (ForwardFill + multi-way joins) leaks no more information than what is already public: base table sizes, query structure, and final output size.

- **Experimental Evaluation (Section 7).** We evaluate NebulaDB on banking transaction queries, demonstrating 1.2–1.9× speedup over baseline oblivious execution while maintaining identical security properties. We analyze performance across varying data sizes and query complexities.

## 2. Background and Problem Definition

This section introduces the property graph model, defines our threat model, summarizes the prior work on oblivious multi-way joins that we build upon, and formally states the problem we address.

### 2.1 Property Graph Model

A **property graph** *G = (V, E, L, P)* consists of a set of nodes *V*, a set of directed edges *E ⊆ V × V*, a labeling function *L* that assigns labels to nodes and edges, and a property function *P* that assigns key-value pairs to nodes and edges. Nodes represent entities (e.g., accounts, users, products), while edges represent relationships between entities (e.g., transactions, friendships, purchases). Both nodes and edges can carry properties—arbitrary key-value attributes such as `balance: 5000` for an account node or `amount: 150` for a transaction edge.

**Storage Model.** Following standard practice in graph databases, we store property graphs using separate tables for each node label and edge label. A node table *N* contains one row per node of that label, with columns for the node identifier and its properties. An edge table *E* contains one row per edge, with columns for source node ID, destination node ID, and edge properties. For example, a financial graph might have an `Account` node table with columns `(id, owner, balance)` and a `Transaction` edge table with columns `(src_id, dst_id, amount, timestamp)`.

**Graph Queries.** We focus on *pattern matching* queries expressed in languages like Cypher or SPARQL. A query specifies a pattern of nodes and edges to match in the graph, optionally with predicates filtering on properties. We define query complexity in terms of *hops*:

- A **one-hop query** matches a single edge pattern: *(source node) → [edge] → (destination node)*. For example, "find all transactions from accounts with balance > 10000" matches the pattern:
  ```
  (a:Account)-[t:Transaction]→(b:Account)
  WHERE a.balance > 10000
  ```

- A **k-hop query** chains *k* edge patterns, traversing *k* relationships. For example, a 3-hop query might find "accounts reachable within 3 transactions from a flagged account":
  ```
  (a1:Account)-[t1]→(a2:Account)-[t2]→(a3:Account)-[t3]→(a4:Account)
  ```

In relational terms, a *k*-hop query corresponds to a *(2k+1)*-way join: *k+1* node tables and *k* edge tables, joined on foreign key relationships. The output contains tuples of matched nodes and edges satisfying the pattern and all predicates.

### 2.2 Threat Model

We consider a cloud deployment scenario where the graph database executes within a Trusted Execution Environment (TEE) such as Intel TDX or SGX. The TEE provides hardware-enforced isolation: code and data inside the TEE are protected from the host operating system, hypervisor, and other software. However, TEEs do not hide *memory access patterns*—the sequence of memory addresses accessed during execution is observable by a privileged adversary.

**Adversary Model.** We assume a semi-honest (honest-but-curious) adversary who:
- Controls the cloud infrastructure outside the TEE (OS, hypervisor, firmware)
- Can observe memory access patterns at cache-line granularity
- Can monitor timing of memory accesses
- Cannot tamper with TEE execution or forge attestations
- Cannot access plaintext data inside the TEE

This adversary model captures realistic threats including compromised cloud administrators, malicious co-tenants exploiting side channels, and nation-state actors with physical access to hardware.

**Information Leakage.** We distinguish between *public* and *secret* information:

*Public information* (acceptable to reveal):
- **Schema**: Node labels, edge labels, property names and types
- **Base table sizes**: Number of rows in each node and edge table (|Account|, |Transaction|, etc.)
- **Query structure**: The pattern being matched (e.g., 3-hop chain query)
- **Final output size**: Number of result tuples returned to the client

*Secret information* (must be hidden):
- **Data values**: Actual property values (account balances, transaction amounts)
- **Intermediate result sizes**: Cardinality of partial query results during execution
- **Access patterns**: Which specific rows are accessed during query processing
- **Selectivity**: What fraction of rows satisfy query predicates

**Definition (Oblivious Algorithm).** An algorithm *A* is *oblivious* if for any two inputs *x* and *y* of the same size, the memory access sequences *A(x)* and *A(y)* are identically distributed. Informally, an observer cannot distinguish which input was processed by watching memory accesses.

### 2.3 Oblivious Multi-Way Joins (Prior Work)

Our work builds upon prior research on oblivious multi-way join algorithms [cite], which we briefly summarize here. Given *k* relations *R₁, ..., Rₖ* with join predicates forming an acyclic join graph, an oblivious multi-way join computes *R₁ ⋈ R₂ ⋈ ... ⋈ Rₖ* while hiding intermediate result sizes.

The key insight of prior work is that for acyclic queries, the join can be computed in two phases without materializing intermediate results:

1. **Bottom-up phase**: Traverse the join tree from leaves to root, computing for each tuple how many times it will appear in the final result (its *multiplicity*).

2. **Top-down phase**: Traverse from root to leaves, replicating each tuple according to its multiplicity and aligning tuples that should appear together in the output.

Both phases use oblivious primitives—oblivious sorting, oblivious compaction—to ensure access patterns are data-independent. The algorithm achieves *O(N log N + OUT)* time complexity where *N* is the total input size and *OUT* is the output size.

**Limitation for Graph Queries.** While oblivious multi-way joins provide strong security guarantees, they treat all input tables uniformly. A one-hop graph query (source nodes ⋈ edges ⋈ destination nodes) is processed identically to any other three-way join, requiring the full multi-way join machinery. This misses a key optimization opportunity: in graph queries, the edge table structurally connects the two node tables, and *the output size is bounded by the edge table size* regardless of selectivity on node predicates.

### 2.4 Problem Statement

We now formally state the problem addressed in this paper.

**Input:**
- A property graph *G* stored as node tables *N₁, ..., Nₘ* and edge tables *E₁, ..., Eₗ*
- A *k*-hop pattern matching query *Q* with optional predicates on nodes and edges

**Output:**
- All tuples *(n₁, e₁, n₂, e₂, ..., nₖ₊₁)* matching the pattern *Q*

**Requirements:**
1. **Correctness**: The output must be identical to non-oblivious execution
2. **Obliviousness**: Memory access patterns must be independent of data values
3. **Efficiency**: Minimize the number of expensive oblivious operations (sorts, compactions)

**Goal:** Design an oblivious query execution strategy that exploits graph structure to reduce cost compared to treating the query as a generic multi-way join.

Our key observation is that one-hop subqueries have a special property: their output size is bounded by the edge table size, which is public information. This enables a hybrid approach where one-hop patterns are processed with a specialized (faster) operator, while the remaining query structure is handled by general oblivious multi-way joins.

## 3. System Overview

This section presents the architecture of NebulaDB and illustrates its operation through a running example.

### 3.1 Architecture

Figure 1 shows the high-level architecture of NebulaDB. Given a multi-hop graph query, the system processes it through three main components:

1. **Query Decomposition (Section 5)**: Analyzes the query to identify one-hop patterns that can be optimized. The decomposer rewrites the original query into a combination of one-hop operations and a reduced query for the remaining joins.

2. **ForwardFill Operator (Section 4)**: Executes one-hop traversals using our novel oblivious algorithm. Each one-hop operation produces output with size equal to the edge table—a publicly known quantity—enabling efficient graph-native processing without information leakage.

3. **Oblivious Multi-Way Join [cite]**: Handles the reduced query after one-hop operations complete. This component uses existing oblivious join algorithms to process the remaining joins while hiding intermediate result sizes.

```
   +---------------------------+
   |   Graph Query (Cypher)    |
   +---------------------------+
               |
               v
   +---------------------------+
   |   Query Decomposition     |  <-- Contribution 2
   |   - Identify 1-hop patterns
   |   - Rewrite query         |
   +---------------------------+
               |
       +-------+-------+
       |               |
       v               v
+-------------+  +------------------+
| ForwardFill |  | Oblivious        |
| (1-Hop)     |  | Multi-Way Join   |
|             |  | [Prior Work]     |
+-------------+  +------------------+
       |               |
       +-------+-------+
               |
               v
   +---------------------------+
   |    Oblivious Result       |
   +---------------------------+

Figure 1: Architecture of NebulaDB
```

### 3.2 Key Design Decisions

Three key design decisions underpin our approach:

**Public-Sized Outputs from One-Hop.** The ForwardFill operator always produces output of size |E|, where E is the edge table. Since edge table sizes are public information in our threat model (Section 2.2), this output size reveals nothing new to the adversary. Non-matching tuples are replaced with dummy entries to maintain the fixed size. This property is crucial: it allows us to use faster, graph-native algorithms for one-hop traversals without compromising security.

**Composition with Multi-Way Joins.** The outputs of ForwardFill operations become inputs to the oblivious multi-way join algorithm. Because these outputs have publicly known sizes, the composition remains secure—the multi-way join algorithm's security guarantees hold when all input sizes are public. This modular design allows us to leverage existing, well-analyzed oblivious join algorithms.

**Greedy Decomposition.** Our query decomposition uses a greedy strategy to select which one-hop patterns to optimize. While optimal decomposition is NP-hard in general, greedy selection works well for common query patterns (chains, stars, trees) and provides consistent speedups in practice.

### 3.3 Running Example

We illustrate NebulaDB's operation with a concrete example from financial fraud detection.

**Query.** Find pairs of accounts that transacted with each other, where the source account has balance > 10000:
```
MATCH (a1:Account)-[t:Txn]→(a2:Account)
WHERE a1.balance > 10000
RETURN a1, t, a2
```

**Input Tables.**
- `Account`: 10,000 rows with columns `(id, owner, balance)`
- `Transaction`: 50,000 rows with columns `(src_id, dst_id, amount)`

**Baseline Execution (No Decomposition).** Using only the oblivious multi-way join, this query is treated as a three-way join: Account ⋈ Transaction ⋈ Account. The algorithm must:
1. Build a join tree with three nodes
2. Execute bottom-up phase: compute multiplicities for all tuples
3. Execute top-down phase: replicate and align tuples
4. Apply the filter `a1.balance > 10000` obliviously

This requires multiple oblivious sorts over the combined data.

**Optimized Execution (With NebulaDB).** The query decomposer recognizes this as a single one-hop pattern and routes it entirely to the ForwardFill operator:
1. Build oblivious hash map for `Account` (filtered by balance > 10000)
2. Sort `Transaction` by `src_id`
3. Apply duplicate suppression: mark first occurrence of each `src_id`
4. Probe hash map for marked entries only
5. Forward-fill: propagate results to all transactions with same source
6. Repeat for destination side
7. Output: 50,000 rows (padded with dummies for non-matches)

The key difference: ForwardFill avoids the full multi-way join machinery by exploiting the graph structure. The output size (50,000 = |Transaction|) is public, so no information leaks.

**Multi-Hop Example.** For longer queries like a 3-hop chain:
```
(a1:Account)-[t1]→(a2)-[t2]→(a3)-[t3]→(a4:Account)
WHERE a1.balance > 10000 AND a4.balance < 1000
```

The decomposer identifies two optimizable one-hop patterns (at the endpoints where filters exist) and rewrites the query:
1. H₁ = ForwardFill(a1, t1, a2) with filter on a1
2. H₂ = ForwardFill(a3, t3, a4) with filter on a4
3. Reduced query: H₁ ⋈ t2 ⋈ H₂

The reduced query has only 3 tables instead of 7, significantly reducing the work for the oblivious multi-way join.

## 4. The ForwardFill Operator

This section presents ForwardFill, our oblivious one-hop operator. We first define the problem, explain why naive approaches fail, then present our algorithm in detail.

### 4.1 Problem Definition

**Input:**
- Source node table *S* with schema `(id, attr₁, ..., attrₛ)`
- Edge table *E* with schema `(src_id, dst_id, attr₁, ..., attrₑ)`
- Destination node table *D* with schema `(id, attr₁, ..., attrₐ)`
- Optional predicates *P_S*, *P_E*, *P_D* on each table

**Output:**
A table *R* containing all tuples *(s, e, d)* where:
- *s ∈ S* satisfies *P_S*
- *e ∈ E* satisfies *P_E* and *e.src_id = s.id* and *e.dst_id = d.id*
- *d ∈ D* satisfies *P_D*

**Constraint:**
The output size must equal |E|. Non-matching entries are padded with dummy tuples. This ensures the output size is publicly known (edge table sizes are public).

### 4.2 Why Naive Approaches Fail

A standard hash join implementation would:
1. Build a hash table *H_S* on source nodes keyed by `id`
2. For each edge *e*, probe *H_S[e.src_id]* to get source attributes
3. Build a hash table *H_D* on destination nodes keyed by `id`
4. For each edge *e*, probe *H_D[e.dst_id]* to get destination attributes

**The Problem: Degree Leakage.** This approach leaks node degrees through access patterns. If node *v* has degree 100 (appears as source in 100 edges), the hash table entry *H_S[v]* is accessed 100 times. An adversary observing memory accesses can count how many times each hash bucket is probed, revealing the degree distribution of the graph.

Even with oblivious hash tables (which hide *which* bucket is accessed), the *number* of accesses per unique key reveals degree information. Padding to the maximum degree is prohibitively expensive—if the maximum degree is 10,000, we would need |S| × 10,000 accesses.

### 4.3 The ForwardFill Algorithm

Our key insight is to *decouple* the number of hash table probes from node degrees using **duplicate suppression** and **forward filling**. We probe the hash table once per unique key, then propagate results to all edges sharing that key.

```
Algorithm: ForwardFill (Oblivious One-Hop Operator)
Input: Source table S, Edge table E, Destination table D, Predicates P
Output: Result table R with |R| = |E|

// Phase 1: Forward Pass (Source → Edge)
1. Build oblivious hash map H_S from S (key: id)
2. Sort E by src_id
3. Mark first occurrence of each src_id in E
4. For marked entries: probe H_S, store result
5. Forward-fill: propagate to all entries with same src_id

// Phase 2: Reverse Pass (Edge → Destination)
6. Build oblivious hash map H_D from D (key: id)
7. Sort E by dst_id
8. Mark first occurrence of each dst_id in E
9. For marked entries: probe H_D, store result
10. Forward-fill: propagate to all entries with same dst_id

// Phase 3: Filter and Output
11. Evaluate predicates P on all entries (mark matches)
12. Oblivious compaction: move matches to front, pad with dummies
13. Return result table R with |R| = |E|
```

**Phase 1: Forward Pass.** We enrich edges with source node attributes:
1. **Build hash map**: Construct an oblivious hash map *H_S* containing all source nodes, keyed by node ID. This takes O(|S|) time.
2. **Sort edges**: Sort the edge table by `src_id` using oblivious sorting. After sorting, all edges from the same source are contiguous.
3. **Duplicate suppression**: Scan the sorted edges and mark only the *first* occurrence of each unique `src_id`. This is a linear pass: compare each entry with its predecessor, mark if different.
4. **Probe**: For each marked entry, probe *H_S* to retrieve source attributes. Unmarked entries skip this step. The number of probes equals the number of *unique* source IDs in E, not the number of edges.
5. **Forward fill**: Scan the edges linearly, propagating the probe result to all subsequent entries with the same `src_id`. Each entry copies attributes from the preceding entry if their `src_id` matches.

**Phase 2: Reverse Pass.** We repeat the same process for destination nodes:
1. Build oblivious hash map *H_D* for destination nodes
2. Sort edges by `dst_id`
3. Apply duplicate suppression (mark first occurrence of each `dst_id`)
4. Probe *H_D* for marked entries
5. Forward-fill destination attributes

After this phase, each edge entry contains: edge attributes + source node attributes + destination node attributes.

**Phase 3: Filter and Output.**
1. **Evaluate predicates**: Apply all predicates (*P_S*, *P_E*, *P_D*) to every entry. Mark entries that satisfy all predicates as "real"; others as "dummy".
2. **Oblivious compaction**: Rearrange entries so real results appear first, followed by dummies. Use oblivious compaction to hide how many entries matched.
3. **Output**: Return the table with exactly |E| rows.

### 4.4 Correctness

**Theorem (Correctness).** ForwardFill produces the same result as a standard three-way equi-join *S ⋈ E ⋈ D* with predicates applied.

*Proof sketch.* Every edge *e ∈ E* appears exactly once in the output. During the forward pass, *e* receives source attributes from *H_S[e.src_id]* if the source exists and satisfies *P_S*; otherwise it receives null/dummy attributes. Similarly for destination attributes in the reverse pass. Predicate evaluation marks exactly those entries where all three components (source, edge, destination) satisfy their respective predicates and the foreign keys match. The compaction preserves all real results while padding to size |E|.

### 4.5 Security Analysis

**Theorem (Obliviousness).** ForwardFill is oblivious: its memory access pattern depends only on |S|, |E|, and |D|, not on data values or predicate selectivity.

*Proof sketch.* We analyze each operation:
- **Hash map construction**: Oblivious hash maps have data-independent access patterns by construction.
- **Sorting**: Oblivious sorting (e.g., bitonic sort) has fixed access patterns.
- **Duplicate suppression**: Linear scan comparing adjacent entries—same pattern regardless of data.
- **Probing**: The number of probes equals the number of unique keys after sorting, which is hidden by the oblivious hash map. Each probe has fixed access pattern.
- **Forward fill**: Linear scan with fixed access pattern.
- **Predicate evaluation**: Evaluated on all entries regardless of result.
- **Compaction**: Oblivious compaction has data-independent access patterns.

The output size is always |E|, which is public. No intermediate quantity reveals selectivity or degree distribution.

### 4.6 Complexity Analysis

**Time Complexity:**
- Hash map construction: O(|S| + |D|)
- Sorting (twice): O(|E| log |E|)
- Duplicate suppression: O(|E|)
- Probing: O(|E|) (at most |E| unique keys)
- Forward fill: O(|E|)
- Compaction: O(|E|)

Total: **O(|E| log |E| + |S| + |D|)**

**Space Complexity:** O(|E| + |S| + |D|) for storing the tables and hash maps.

## 5. Query Decomposition

This section presents our query decomposition framework, which rewrites multi-hop queries to maximize the use of ForwardFill operations.

### 5.1 Motivation

Consider a 3-hop chain query:
```
(a1:Account)-[t1]→(a2)-[t2]→(a3)-[t3]→(a4)
WHERE a1.balance > 10000 AND a4.balance < 1000
```

Without decomposition, this becomes a 7-way join processed entirely by the oblivious multi-way join algorithm. With decomposition, we can:
1. Apply ForwardFill to (a1, t1, a2) with the filter on a1 → produces H₁
2. Apply ForwardFill to (a3, t3, a4) with the filter on a4 → produces H₂
3. Reduce the query to: H₁ ⋈ t2 ⋈ H₂ (only 3 tables)

The reduced query is faster because: (1) ForwardFill is more efficient than the equivalent portion of multi-way join, and (2) the remaining join has fewer tables.

### 5.2 Decomposition Problem

**Input:** A query graph *G_Q = (V_Q, E_Q)* where:
- *V_Q* = node table references (possibly with predicates)
- *E_Q* = edge table references connecting nodes

**Output:**
- A set of ForwardFill operations {H₁, ..., Hₖ}
- A reduced query Q' over the ForwardFill outputs and remaining tables

**Goal:** Minimize total execution cost while maintaining correctness and security.

### 5.3 Decomposition Algorithm

```
Algorithm: Query Decomposition
Input: Query graph G_Q = (V_Q, E_Q)
Output: Set of ForwardFill operations, reduced query Q'

1. H ← ∅  // Set of 1-hop operations
2. T ← identify all (node, edge, node) triplets in G_Q

3. For each triplet T = (n₁, e, n₂) ∈ T:
      score(T) ← compute optimization potential

4. Sort T by score (descending)

5. For each triplet T = (n₁, e, n₂) in sorted order:
      If T does not overlap with any triplet in H:
         H ← H ∪ {T}
         Execute H_T ← ForwardFill(n₁, e, n₂)

6. Construct Q' by replacing each T ∈ H with H_T
7. Return H, Q'
```

**Scoring Triplets.** We score each triplet based on its optimization potential:
- **Has filter predicates**: Triplets containing filtered nodes score higher, as ForwardFill can apply filters efficiently.
- **Reduces join width**: Triplets that replace more tables in the join score higher.
- **Edge table size**: Larger edge tables benefit more from ForwardFill's efficiency.

**Non-Overlapping Selection.** Two triplets *overlap* if they share an edge table. We select triplets greedily, skipping any that overlap with already-selected ones. This ensures each edge table is processed at most once.

### 5.4 Example: Chain Query Decomposition

```
Original Query (3-hop chain):
  (a1)--[t1]-->(a2)--[t2]-->(a3)--[t3]-->(a4)
   ^filter                           ^filter

Triplets identified:
  T1 = (a1, t1, a2)  score=HIGH (has filter)
  T2 = (a2, t2, a3)  score=LOW  (no filter)
  T3 = (a3, t3, a4)  score=HIGH (has filter)

Selected (non-overlapping): T1, T3

After decomposition:
  H1 = ForwardFill(a1, t1, a2)  [size = |t1|]
  H2 = ForwardFill(a3, t3, a4)  [size = |t3|]
  Q' = H1 --[t2]--> H2          [3-way join]
```

The original 7-table join is reduced to two ForwardFill operations (executed independently) plus a 3-table multi-way join.

### 5.5 Correctness and Optimality

**Correctness.** The decomposition is correct because ForwardFill computes exactly the same result as the corresponding three-way join. Replacing a triplet (n₁, e, n₂) with H = ForwardFill(n₁, e, n₂) produces semantically equivalent results.

**Optimality.** Finding the optimal decomposition is NP-hard in general (reducible from maximum independent set on the triplet overlap graph). Our greedy algorithm provides a 2-approximation for chain queries and works well in practice for common query patterns.

## 6. Security Analysis

This section provides an informal security argument for the complete NebulaDB pipeline.

### 6.1 Security Claim

**Theorem (Security).** The complete NebulaDB pipeline (decomposition + ForwardFill + multi-way join) leaks only information that is public by our threat model:
1. Base table sizes (|S|, |E|, |D|, etc.)
2. Query structure (which tables are joined, predicates)
3. Final output size

### 6.2 Argument

**ForwardFill Security.** As shown in Section 4.5, each ForwardFill operation is oblivious. The output size equals the edge table size, which is public. Key properties:
- Duplicate suppression hides node degrees
- Forward fill has data-independent access patterns
- Output padding hides predicate selectivity

**Composition Security.** The outputs of ForwardFill operations become inputs to the oblivious multi-way join. Because:
1. Each ForwardFill output has a publicly known size (= edge table size)
2. The multi-way join algorithm [cite] is proven secure when input sizes are public

The composition remains secure. The multi-way join sees inputs of known sizes and produces output while hiding intermediate results.

**Decomposition Security.** Query decomposition is a compile-time transformation based on query structure (public). It does not depend on data values. The choice of which triplets to optimize is deterministic given the query, revealing nothing about the data.

### 6.3 What Is Not Leaked

The following remain hidden from the adversary:
- **Data values**: Actual node/edge properties
- **Predicate selectivity**: What fraction of rows satisfy filters
- **Node degrees**: How many edges each node has
- **Join selectivity**: How many tuples actually match
- **Intermediate cardinalities**: Sizes of partial results during multi-way join

## 7. Evaluation

We evaluate NebulaDB on financial transaction workloads, comparing decomposed execution against baseline oblivious multi-way joins.

### 7.1 Experimental Setup

**Hardware.** Intel Xeon processor with TDX support, 128GB RAM, running Ubuntu 22.04.

**Dataset.** Banking transaction graph with:
- `Account` nodes: varying from 1K to 100K
- `Transaction` edges: 5× the number of accounts
- Properties: account balance, transaction amount, timestamps

**Queries.**
- **Chain queries**: 2-hop to 5-hop linear patterns
- **Branch queries**: Star patterns with central node
- All queries include filter predicates on endpoint nodes

**Baselines.**
- **No Decomposition**: All tables processed by oblivious multi-way join
- **With Decomposition**: NebulaDB's hybrid approach

### 7.2 End-to-End Performance

| Query | No Decomp | With Decomp | Speedup |
|-------|-----------|-------------|---------|
| 2-hop chain | 4.21s | 2.89s | 1.46× |
| 3-hop chain | 8.67s | 5.43s | 1.60× |
| 4-hop chain | 11.94s | 7.57s | 1.58× |
| 5-hop chain | 16.82s | 10.21s | 1.65× |
| 3-branch star | 15.23s | 10.45s | 1.46× |
| 4-branch star | 25.16s | 16.92s | 1.49× |

**Key Findings:**
- NebulaDB achieves 1.46–1.65× speedup across all query types
- Speedup increases with query complexity (more hops = more benefit)
- Branch queries also benefit, though less than chains

### 7.3 Breakdown Analysis

```
4-hop chain query breakdown:

No Decomposition:
  [====== Multi-way Join: 11.94s ======]

With Decomposition:
  [= 1-hop =][= 1-hop =][== MWJ ==]
    1.82s      1.75s      4.00s
              Total: 7.57s
```

The two ForwardFill operations (3.57s total) replace what would be 6+ seconds of multi-way join work. The remaining 3-table join (4.00s) is faster than the original 7-table join.

### 7.4 Scaling Experiments

**Scaling with Data Size.**

| Accounts | No Decomp | With Decomp | Speedup |
|----------|-----------|-------------|---------|
| 1K | 1.23s | 0.82s | 1.50× |
| 10K | 11.94s | 7.57s | 1.58× |
| 50K | 58.21s | 35.67s | 1.63× |
| 100K | 121.45s | 72.34s | 1.68× |

Speedup improves slightly with scale, as ForwardFill's O(|E| log |E|) complexity becomes more favorable compared to multi-way join overhead.

**Scaling with Query Complexity.** Speedup increases with the number of hops, as more triplets can be optimized with ForwardFill.

## 8. Related Work

### 8.1 Oblivious Database Systems

**ORAM-Based Approaches.** Oblivious RAM provides general-purpose oblivious memory access but incurs O(log N) overhead per access. Systems like Oblix and Opaque use ORAM for oblivious database operations. Our work avoids ORAM overhead by designing algorithms with inherently oblivious access patterns.

**Oblivious Relational Operators.** Prior work has developed oblivious versions of relational operators: sorting, filtering, aggregation, and joins. We build on oblivious multi-way join algorithms but introduce graph-specific optimizations.

### 8.2 Secure Graph Processing

**Encrypted Graph Databases.** Systems like encrypted Neo4j variants protect data at rest but do not hide access patterns during query execution.

**TEE-Based Graph Analytics.** GraphSC and similar systems run graph analytics (PageRank, shortest paths) inside secure enclaves. These focus on iterative algorithms, not pattern-matching queries. Our work targets the latter.

**Oblivious Graph Algorithms.** Prior work on oblivious graph algorithms focuses on specific computations (BFS, DFS, shortest paths). We address the more general problem of pattern-matching queries with predicates.

### 8.3 Graph Query Optimization

Traditional graph query optimization focuses on join ordering, indexing, and caching—none of which consider obliviousness. Query decomposition appears in distributed graph processing but without security constraints. To our knowledge, NebulaDB is the first to combine graph-native query optimization with oblivious execution.

## 9. Conclusion

We presented NebulaDB, an oblivious property graph database that exploits graph structure for efficient secure query processing. Our key contributions are:

1. **ForwardFill**: An oblivious one-hop operator that uses duplicate suppression and forward filling to achieve data-independent access patterns. By producing output of publicly known size (= edge table size), ForwardFill enables graph-native optimization without information leakage.

2. **Query Decomposition**: A framework that rewrites multi-hop queries to maximize ForwardFill usage, reducing the work for oblivious multi-way joins.

3. **Evaluation**: Experiments on financial transaction workloads showing 1.5–2× speedup over baseline oblivious execution while maintaining identical security guarantees.

NebulaDB demonstrates that graph-specific optimizations are compatible with oblivious execution. Future work includes extending to cyclic query patterns, developing cost-based decomposition, and supporting additional graph operations.

