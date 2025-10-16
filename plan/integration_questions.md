# Integration Planning: Obligraph 1-Hop ↔ Ruidi's Acyclic Join

**Date**: 2025-10-15
**Authors**: Bishwajit Bhattacharjee, Nafis Ahmed
**Status**: Planning Phase - Critical Questions

---

## Executive Summary

This document outlines critical design questions that must be answered before integrating the Obligraph 1-hop operator with Ruidi's oblivious acyclic multi-way join algorithm. The integration aims to implement the hybrid optimization strategy described in `obligraph/technical_approach.md`.

**Core Challenge**: Two independently developed codebases with different data models, query representations, and architectural assumptions must be unified while preserving oblivious security guarantees and achieving the promised performance benefits.

---

## Codebase Analysis Summary

### Obligraph (1-Hop Operator)

**Location**: `obligraph/` directory

**Data Model**:
- **Row Structure**: Maximum 32 bytes of data (`ROW_DATA_MAX_SIZE`)
- **Flexible Schema**: Columnar metadata tracks column offsets/types
- **Key Structure**: `PairKey = pair<key_t, key_t>` for (srcId, destId)
- **Support**: INT32, INT64, STRING (2-byte truncated), DOUBLE, BOOLEAN

**Table Abstraction**:
```cpp
struct Table {
    string name;
    string alias;
    Schema schema;                    // Column metadata
    vector<ColumnMeta> primaryKeys;   // 1 for nodes, 2 for edges
    vector<Row> rows;                 // Actual data
    TableType type;                   // NODE, EDGE, INTERMEDIATE
    size_t rowCount;
    vector<string> node_table_names;  // Edge-specific: {srcNode, destNode}
}
```

**Core Interface**:
```cpp
Table oneHop(Catalog& catalog, OneHopQuery& query, ThreadPool& pool);
```

**Algorithm Characteristics**:
- **Parallel execution**: Forward and reverse edge processing in parallel
- **Deduplication strategy**: Hash-based with oblivious suppression
- **Hash join**: Custom oblivious hash map (NOT Snoopy's standard hash table)
- **Oblivious operations**: `ObliviousSort`, `ObliviousChoose`, `ObliviousCompact`
- **Output size**: Exactly `|BaseEdge|` (public base table size)

**Oblivious Primitives Source**: Snoopy-based (`obl_primitives.h`)
- Extracted from Secure XGBoost project
- Assembly-level implementations for constant-time operations
- AVX2 SIMD optimizations available
- Cache-line-aware access patterns (`CACHE_LINE_SIZE = 192`)

**Key Operations in 1-Hop**:
1. `build_and_probe()`: Oblivious hash join between node and edge tables
2. `deduplicateRows()`: Replace duplicate keys with random dummies
3. `reduplicateRows()`: Restore duplicates via forward-fill
4. `Table::unionWith()`: Merge schemas and expand row data
5. `Table::filter()`: Apply predicates with oblivious compact
6. `Table::project()`: Column projection with schema rewriting

**Build System**: CMake-based, standalone compilation

---

### Ruidi's Codebase (Acyclic Multi-Way Join)

**Location**: Project root (`app/`, `main/`, `common/`)

**Data Model**:
- **Entry Structure**: ~2256 bytes in fat mode, ~260 bytes in slim mode
- **Fixed Schema**: `int32_t attributes[MAX_ATTRIBUTES]` array
- **Rich Metadata**: 15+ metadata fields for join algorithm phases

**Entry Structure** (`common/enclave_types.h`):
```cpp
typedef struct {
    // Entry metadata
    int32_t field_type;           // SORT_PADDING, SOURCE, START, END, TARGET, DIST_PADDING
    int32_t equality_type;        // EQ, NEQ, NONE

    // Join attribute
    int32_t join_attr;

    // Persistent metadata (across phases)
    int32_t original_index;
    int32_t local_mult;
    int32_t final_mult;
    int32_t foreign_sum;

    // Temporary metadata (reused between phases)
    int32_t local_cumsum;
    int32_t local_interval;
    int32_t foreign_interval;
    int32_t local_weight;

    // Expansion metadata
    int32_t copy_index;
    int32_t alignment_key;

    // Distribution fields
    int32_t dst_idx;
    int32_t index;

    // Data attributes (integers only)
    int32_t attributes[MAX_ATTRIBUTES];  // Typically 8-12 attributes
} entry_t;
```

**Table Abstraction**:
```cpp
class Table {
private:
    std::vector<Entry> entries;
    std::string table_name;
    size_t num_columns;
    std::vector<std::string> schema_column_names;  // Schema for slim mode

public:
    // Core operations
    void add_entry(const Entry& entry);
    Entry& operator[](size_t index);

    // TDX direct operations (post-migration)
    Table map(OpEcall op_type, int32_t* params = nullptr) const;
    void linear_pass(OpEcall op_type, int32_t* params = nullptr);
    void parallel_pass(Table& other, OpEcall op_type, int32_t* params = nullptr);
    void shuffle_merge_sort(OpEcall op_type);
    void distribute_pass(size_t distance, OpEcall op_type, int32_t* params = nullptr);
    void add_padding(size_t count, OpEcall padding_op);

    // Schema support (for slim mode)
    void set_schema(const std::vector<std::string>& columns);
    int32_t get_attribute(size_t row, const std::string& col_name) const;
};
```

**Core Interface**:
```cpp
class ObliviousJoin {
public:
    static Table Execute(JoinTreeNodePtr root);
    static Table ExecuteWithDebug(JoinTreeNodePtr root, const std::string& session_name);
};
```

**Algorithm Phases**:
1. **Bottom-Up Phase**: Compute local multiplicities via cumulative sums
2. **Top-Down Phase**: Compute final multiplicities via foreign intervals
3. **Distribute-Expand**: Replicate tuples based on multiplicities
4. **Align-Concat**: Construct final result with alignment

**Join Tree Representation**:
```cpp
struct JoinTreeNode {
    std::string node_name;
    std::shared_ptr<Table> table;  // Leaf node data
    std::shared_ptr<JoinTreeNode> left;
    std::shared_ptr<JoinTreeNode> right;
    // Constraint: left join_attr < right join_attr (band join)
};
```

**Query Flow**:
```
SQL Query → QueryParser → ParsedQuery → JoinTreeBuilder → JoinTreeNode → ObliviousJoin::Execute
```

**Data Loading**: CSV files via `TableIO::load_csv(filepath)`
- First row: column names
- Remaining rows: integer values (system limitation)
- Last row: sentinel values (-10000)
- Output: `Table` object with `Entry` vector

**Build System**: Makefile-based, TDX-ready (no SGX SDK required)

**Post-TDX Migration Status**:
- ✅ No application-level encryption (TDX handles it)
- ✅ No enclave boundary (direct function calls)
- ✅ Unified codebase (merged `enclave/trusted/` → `app/core_logic/`)
- ✅ Simplified architecture

**Data Constraints**:
- ALL values must be `int32_t` within `[-1,073,741,820, 1,073,741,820]`
- `NULL_VALUE = INT32_MAX = 2,147,483,647`
- `JOIN_ATTR_NEG_INF = -1,073,741,821`, `JOIN_ATTR_POS_INF = 1,073,741,821`
- String data NOT supported (parsed as 0 with warnings)

---

## Critical Integration Questions

### Q1: Data Structure Unification Strategy ⚠️ **CRITICAL**

**Problem**: 70x size difference between Row (32 bytes) and Entry (2256 bytes)

**Context**:
- Obligraph Row: Lightweight, flexible schema, supports multiple types
- Ruidi Entry: Heavy metadata for join algorithm, fixed integer attributes
- Conversion overhead could dominate 1-hop benefits if done naively

**Options**:

#### Option A: Adapter Layer (Recommended for Phase 1)
```cpp
// Conversion functions at integration boundary
Entry row_to_entry(const Row& row, const Schema& schema);
Row entry_to_row(const Entry& entry, const Schema& target_schema);
```

**Pros**:
- ✅ Minimal changes to both codebases
- ✅ Clear separation of concerns
- ✅ Easier to debug and test independently
- ✅ Can optimize conversion later

**Cons**:
- ❌ Memory copy overhead (32B → 2256B)
- ❌ Type conversion issues (strings → integers)
- ❌ Schema mapping complexity

**Implementation Considerations**:
- Where to place metadata? All join metadata starts as NULL/defaults
- How to handle Obligraph's flexible columns → Ruidi's fixed attributes array?
- Batch conversion to amortize overhead?

#### Option B: Unified Entry Structure
```cpp
typedef struct {
    // Existing Ruidi metadata (15 fields)
    int32_t field_type, equality_type, join_attr, ...;

    // NEW: Graph-specific metadata
    int32_t node_type;      // SOURCE_NODE, DEST_NODE, EDGE
    key_t src_id, dest_id;  // Composite keys for edges

    // Existing attributes array
    int32_t attributes[MAX_ATTRIBUTES];
} unified_entry_t;
```

**Pros**:
- ✅ Single data model throughout system
- ✅ No conversion overhead
- ✅ Easier to reason about data flow

**Cons**:
- ❌ Major refactor of Obligraph code
- ❌ Still have 70x bloat for 1-hop operations
- ❌ Graph metadata unused by Ruidi's algorithm
- ❌ Breaks Obligraph's type flexibility

#### Option C: Row-Based Ruidi (MAJOR REFACTOR)
Rewrite Ruidi's algorithm to use lightweight Rows + separate metadata tables.

**Pros**:
- ✅ Massive memory savings
- ✅ Better cache locality
- ✅ Unified lightweight data model

**Cons**:
- ❌ 6+ months of work (complete algorithm rewrite)
- ❌ High risk of introducing bugs
- ❌ Security proofs may need revision
- ❌ Not feasible for short-term integration

**Question**: Which option aligns with your timeline and performance goals?

**My Recommendation**: Start with **Option A (Adapter Layer)** for Phase 1 proof-of-concept. Measure conversion overhead. If significant, explore **Option B** for Phase 2 optimization.

---

### Q2: Integration Architecture ⚠️ **CRITICAL**

**Problem**: How do the two systems communicate at runtime?

#### Option A: Pre-processing Pipeline (Loosely Coupled)
```
┌─────────────┐      ┌──────────────┐      ┌────────────┐
│ Graph Query │─────>│  1-Hop       │─────>│   Ruidi    │
│  (Cypher)   │      │  Processor   │      │  Acyclic   │
└─────────────┘      │              │      │   Join     │
                     │  Outputs:    │      │            │
                     │  CSV files   │      │  Outputs:  │
                     │  T1, T2, T3  │      │  result.csv│
                     └──────────────┘      └────────────┘
```

**Flow**:
1. Parse graph query, identify triplets
2. Run 1-hop on each triplet → write intermediate CSVs
3. Construct reduced join graph (CSV paths)
4. Run Ruidi's join on reduced graph
5. Output final result

**Pros**:
- ✅ Zero code coupling (both systems stay independent)
- ✅ Easy to test each component separately
- ✅ Can use existing I/O infrastructure
- ✅ Scriptable workflow

**Cons**:
- ❌ File I/O overhead (serialize/deserialize tables)
- ❌ Disk space for intermediate tables
- ❌ No unified optimizer
- ❌ Manual orchestration required

**Use Case**: Quick proof-of-concept, benchmarking 1-hop vs pure Ruidi

---

#### Option B: Unified Query Planner (Tightly Integrated) ⭐ **RECOMMENDED**
```
┌─────────────┐      ┌──────────────────────┐      ┌──────────┐
│ Hybrid Query│─────>│  Smart Query Planner │─────>│  Output  │
│  (SQL/DSL)  │      │                      │      │          │
└─────────────┘      │  Phases:             │      └──────────┘
                     │  1. Parse query      │
                     │  2. Identify triplets│
                     │  3. Cost estimation  │
                     │  4. Execution plan   │
                     │                      │
                     │  Execution:          │
                     │  - 1-hop on triplets │ (in-memory)
                     │  - Ruidi on reduced  │
                     │    graph             │
                     └──────────────────────┘
```

**Architecture**:
```cpp
class HybridQueryExecutor {
public:
    Table execute(const HybridQuery& query) {
        // Phase 1: Identify base table triplets
        vector<Triplet> triplets = identify_triplets(query);

        // Phase 2: Execute 1-hop on each triplet (in-memory)
        map<string, Table> intermediate_tables;
        for (const auto& triplet : triplets) {
            Table result = oneHop(catalog, triplet.to_query(), pool);
            intermediate_tables[triplet.name] = convert_to_ruidi_table(result);
        }

        // Phase 3: Construct reduced join graph
        JoinTreeNodePtr root = build_reduced_tree(query, intermediate_tables);

        // Phase 4: Execute Ruidi's algorithm
        return ObliviousJoin::Execute(root);
    }
};
```

**Pros**:
- ✅ No I/O overhead (in-memory passing)
- ✅ Unified optimization opportunities
- ✅ Clean user interface (single query → result)
- ✅ Can make cost-based decisions (use 1-hop or not)

**Cons**:
- ❌ More complex implementation
- ❌ Tight coupling between components
- ❌ Requires unified query representation

**Use Case**: Production system, optimal performance

---

#### Option C: Library Integration (Moderate Coupling)
```cpp
// Extend Ruidi's JoinTreeBuilder to detect and process triplets
class ExtendedJoinTreeBuilder : public JoinTreeBuilder {
    JoinTreeNodePtr build_from_query(ParsedQuery query, map<string, Table> tables) {
        // Detect triplet patterns in query
        vector<TripletPattern> triplets = detect_triplet_patterns(query);

        // Process each triplet with 1-hop
        for (auto& triplet : triplets) {
            if (is_beneficial(triplet)) {
                Table optimized = run_onehop_on_triplet(triplet);
                tables[triplet.output_name] = optimized;
                query = rewrite_query_with_replacement(query, triplet);
            }
        }

        // Continue with standard Ruidi join tree building
        return JoinTreeBuilder::build_from_query(query, tables);
    }
};
```

**Pros**:
- ✅ Incremental integration (extend existing code)
- ✅ Backward compatible (still supports pure Ruidi)
- ✅ In-memory data passing
- ✅ Easier to test (can toggle 1-hop on/off)

**Cons**:
- ❌ Limited to specific join tree structures
- ❌ Triplet detection logic coupled with tree builder
- ❌ May miss optimization opportunities

**Use Case**: Gradual migration, compatibility with existing workloads

---

**Question**: Which architecture best fits your research goals and deployment model?

**My Recommendation**: **Option B (Unified Query Planner)** for maximum flexibility and performance, with **Option C** as a stepping stone if needed.

---

### Q3: Query Language & Representation

**Problem**: Users need to express both graph patterns (1-hop) and relational joins (Ruidi)

**Current State**:
- Obligraph: Cypher-like queries (`MATCH (a:Person)-[r:WorksAt]->(b:Org)`)
- Ruidi: SQL queries (`SELECT * FROM Person, WorksAt, Org WHERE ...`)

**Options**:

#### Option 1: SQL Only (Minimal Change)
Users write standard SQL. System infers graph patterns from:
- **Foreign key relationships** (if available)
- **Table naming conventions** (e.g., `Person_WorksAt_Org` indicates edge)
- **Join predicates** (detect `T1.id = T2.src_id` patterns)

**Example**:
```sql
-- User writes this
SELECT p.name, o.name
FROM Person p, WorksAt w, Org o, LocatedIn l, City c
WHERE p.id = w.person_id
  AND w.org_id = o.id
  AND o.id = l.org_id
  AND l.city_id = c.id
  AND c.name = 'Berlin';

-- System detects:
--   Triplet 1: (Person, WorksAt, Org) → 1-hop
--   Triplet 2: (Org, LocatedIn, City) → 1-hop (overlaps!)
-- Chooses: Triplet 2 only
-- Reduces to: Person ⋈ WorksAt ⋈ T1 (where T1 is 1-hop output)
```

**Pros**:
- ✅ No new query language
- ✅ Compatible with existing Ruidi queries
- ✅ Users familiar with SQL

**Cons**:
- ❌ Implicit triplet detection (may be fragile)
- ❌ No way to force/disable 1-hop optimization
- ❌ Schema metadata requirements (FK constraints)

---

#### Option 2: Cypher Only (Graph-Native)
Users write Cypher queries. System translates to internal representation.

**Example**:
```cypher
MATCH (p:Person)-[w:WorksAt]->(o:Org)-[l:LocatedIn]->(c:City)
WHERE c.name = 'Berlin' AND p.age > 25
RETURN p.name, o.name
```

**Translation**:
1. Parse Cypher → identify path pattern
2. Detect disjoint triplets: `(Org)-[LocatedIn]->(City)`
3. Run 1-hop → get T1
4. Remaining pattern: `(Person)-[WorksAt]->(Org)` → join with T1
5. Generate SQL equivalent for Ruidi: `SELECT ... FROM Person, WorksAt, T1 WHERE ...`

**Pros**:
- ✅ Natural for graph queries
- ✅ Explicit graph semantics (nodes, edges, paths)
- ✅ Standard language (Neo4j, Memgraph, etc.)

**Cons**:
- ❌ Need full Cypher parser (complex)
- ❌ Not compatible with existing Ruidi SQL queries
- ❌ Limited to graph-shaped queries

---

#### Option 3: Hybrid DSL (New Language)
Design a simple DSL that combines both paradigms.

**Example**:
```
// Hybrid query language
GRAPH_PATTERN:
  (Person)-[WorksAt]->(Org)-[LocatedIn]->(City)

FILTERS:
  City.name = 'Berlin'
  Person.age > 25

OPTIMIZE:
  USE_ONEHOP: (Org, LocatedIn, City)

RETURN:
  Person.name, Org.name
```

**Pros**:
- ✅ Explicit optimization hints
- ✅ Simple to parse
- ✅ Flexible (can express both patterns)

**Cons**:
- ❌ New language to design and implement
- ❌ Learning curve for users
- ❌ Maintenance burden

---

#### Option 4: Programmatic API (Code-Level)
Users build query plans directly in C++.

**Example**:
```cpp
// User code
Catalog catalog;
catalog.load("Person.csv", "WorksAt.csv", "Org.csv", ...);

// Manual triplet specification
OneHopQuery q1("Org", "LocatedIn", "City",
               {{"City", {Predicate("name", EQ, "Berlin")}}});
Table T1 = oneHop(catalog, q1, pool);

// Build join tree with intermediate result
auto tree = JoinTreeBuilder()
    .add_table("Person", catalog.get("Person"))
    .add_table("WorksAt", catalog.get("WorksAt"))
    .add_table("T1", T1)
    .add_join("Person.id = WorksAt.person_id")
    .add_join("WorksAt.org_id = T1.org_id")
    .build();

// Execute
Table result = ObliviousJoin::Execute(tree);
```

**Pros**:
- ✅ Maximum flexibility
- ✅ No parser needed
- ✅ Type-safe (compile-time checks)
- ✅ Easy debugging

**Cons**:
- ❌ Verbose for complex queries
- ❌ Not user-friendly for non-programmers
- ❌ No declarative optimization

---

**Question**: What's your target user? Database researchers? Application developers? Benchmark runners?

**My Recommendation**: Start with **Option 4 (Programmatic API)** for research flexibility. Add **Option 1 (SQL with auto-detection)** later for usability.

---

### Q4: Triplet Identification & Selection ⚠️ **CRITICAL**

**Problem**: "Identify all maximal disjoint triplets" is algorithmically non-trivial

**Context from Technical Approach**:
> For an n-hop linear query (2n+1 base tables), can create n//2 disjoint 1-hop operations (alternating triplets), reducing to approximately (n+1) tables for Ruidi's join.

**Key Constraint**: Only **base tables** can participate in 1-hop, not intermediate results (to preserve public size property).

**Challenge**: NP-hard optimization problem (maximum weighted matching in hypergraph)

**Options**:

#### Option 1: Manual Specification (User-Driven)
```cpp
// User explicitly lists triplets to optimize
vector<Triplet> triplets = {
    {"T1", "Person", "WorksAt", "Org"},
    {"T2", "Org", "LocatedIn", "City"}
};
// System validates disjoint property, executes
```

**Pros**:
- ✅ No complex algorithms needed
- ✅ User controls optimization
- ✅ Reproducible experiments

**Cons**:
- ❌ Manual effort for each query
- ❌ User must understand disjoint constraint
- ❌ No automation

---

#### Option 2: Greedy Heuristic (Fast Approximation)
```cpp
vector<Triplet> find_disjoint_triplets(QueryGraph& graph) {
    vector<Triplet> selected;
    set<string> used_tables;

    // Sort triplets by estimated benefit (heuristic)
    auto candidates = enumerate_all_triplets(graph);
    sort(candidates.begin(), candidates.end(),
         [](auto& a, auto& b) { return estimate_benefit(a) > estimate_benefit(b); });

    // Greedy selection
    for (auto& triplet : candidates) {
        if (!overlaps(triplet, used_tables)) {
            selected.push_back(triplet);
            mark_used(triplet, used_tables);
        }
    }
    return selected;
}
```

**Benefit Heuristic Ideas**:
- Larger edge tables → more benefit (fewer sorts)
- Central nodes in query graph → more impact
- Predicate selectivity (if available)

**Pros**:
- ✅ Automated selection
- ✅ Fast (O(n²) for n triplets)
- ✅ Works for most cases

**Cons**:
- ❌ Not optimal (may miss best combination)
- ❌ Heuristic tuning needed

---

#### Option 3: Optimal Selection (ILP Solver)
Formulate as Integer Linear Program:
```
Maximize: Σ benefit(i) * x_i
Subject to:
  x_i ∈ {0, 1}  (select triplet i or not)
  ∀ table t: Σ(i uses t) x_i ≤ 1  (disjoint constraint)
```

Solve with off-the-shelf solver (CPLEX, Gurobi, or open-source like GLPK).

**Pros**:
- ✅ Provably optimal selection
- ✅ Handles complex query graphs

**Cons**:
- ❌ Solver dependency
- ❌ Slower (may timeout for large graphs)
- ❌ Benefit function still heuristic

---

#### Option 4: Pattern-Based (Domain-Specific Rules)
```cpp
// Hardcoded rules for common patterns
vector<Triplet> find_triplets(QueryGraph& graph) {
    if (is_linear_path(graph)) {
        return alternating_triplets(graph);  // Example 5.2 from tech doc
    } else if (is_star(graph)) {
        return star_optimization(graph);     // Example 5.4 from tech doc
    } else if (is_tree(graph)) {
        return tree_optimization(graph);     // Example 8.1 from tech doc
    } else {
        return greedy_fallback(graph);
    }
}
```

**Pros**:
- ✅ Optimal for known patterns
- ✅ Fast recognition
- ✅ Documented in technical approach

**Cons**:
- ❌ Limited to predefined patterns
- ❌ May miss opportunities in hybrid graphs

---

**Question**: What query shapes do you expect? Linear paths? Stars? Arbitrary acyclic graphs?

**My Recommendation**: **Option 2 (Greedy Heuristic)** for general case, with **Option 4 (Pattern-Based)** fast path for common structures.

---

### Q5: Schema & Metadata Management

**Problem**: Obligraph has explicit graph semantics not present in Ruidi's relational model

**Obligraph's Graph Metadata**:
- **TableType**: `NODE` vs `EDGE` vs `INTERMEDIATE`
- **Primary Keys**: 1 for node tables, 2 (srcId, destId) for edge tables
- **Edge Indexes**: Forward (`_fwd`) and reverse (`_rev`) sorted copies
- **Node References**: Edge tables know their endpoint node table names

**Ruidi's Relational Metadata**:
- Table name (string)
- Number of columns (int)
- Schema column names (vector<string>)
- No graph semantics

**Integration Challenge**: How does Ruidi's `JoinTreeBuilder` know which tables are nodes/edges?

**Options**:

#### Option 1: Catalog Extension
```cpp
class GraphCatalog : public Catalog {
private:
    map<string, TableMetadata> metadata;

    struct TableMetadata {
        TableType type;              // NODE, EDGE
        vector<string> primary_keys; // ["id"] or ["src_id", "dest_id"]
        string src_node_table;       // For edges only
        string dst_node_table;       // For edges only
        bool has_reverse_index;      // Edge table has _rev copy?
    };

public:
    void register_node_table(const string& name, const string& pk);
    void register_edge_table(const string& name, const string& src, const string& dst);
    Table get_forward_index(const string& edge_name);
    Table get_reverse_index(const string& edge_name);
    bool is_triplet_candidate(const string& t1, const string& t2, const string& t3);
};
```

**Usage**:
```cpp
GraphCatalog catalog;
catalog.register_node_table("Person", "id");
catalog.register_node_table("Org", "id");
catalog.register_edge_table("WorksAt", "Person", "Org");

// System now knows WorksAt is an edge between Person and Org
```

**Pros**:
- ✅ Explicit metadata (no guessing)
- ✅ Supports complex schemas
- ✅ Validates triplet patterns

**Cons**:
- ❌ Manual catalog setup
- ❌ Extra code to maintain

---

#### Option 2: Naming Conventions
```
Node tables: Person.csv, Org.csv, City.csv
Edge tables: Person_WorksAt_Org.csv, Org_LocatedIn_City.csv

Indexes (optional):
  Person_WorksAt_Org_fwd.csv (sorted by Person.id)
  Person_WorksAt_Org_rev.csv (sorted by Org.id)
```

**Parsing Logic**:
```cpp
bool is_edge_table(const string& name) {
    // Count underscores: edge tables have exactly 2
    return count(name.begin(), name.end(), '_') == 2;
}

tuple<string, string, string> parse_edge_name(const string& name) {
    // "Person_WorksAt_Org" → ("Person", "WorksAt", "Org")
    // Split by underscore
    auto parts = split(name, '_');
    return {parts[0], parts[1], parts[2]};
}
```

**Pros**:
- ✅ No manual catalog
- ✅ Self-documenting file names
- ✅ Easy to implement

**Cons**:
- ❌ Fragile (fails on complex names)
- ❌ Limits table naming flexibility
- ❌ What about "Person_knows_Person"? (self-loops)

---

#### Option 3: Schema Configuration File
```json
{
  "nodes": [
    {"name": "Person", "primary_key": "id"},
    {"name": "Org", "primary_key": "id"},
    {"name": "City", "primary_key": "id"}
  ],
  "edges": [
    {
      "name": "WorksAt",
      "src_node": "Person",
      "dst_node": "Org",
      "src_key": "person_id",
      "dst_key": "org_id"
    },
    {
      "name": "LocatedIn",
      "src_node": "Org",
      "dst_node": "City",
      "src_key": "org_id",
      "dst_key": "city_id"
    }
  ]
}
```

**Pros**:
- ✅ Explicit and flexible
- ✅ Version control friendly
- ✅ Supports arbitrary schemas

**Cons**:
- ❌ Extra file to maintain
- ❌ Parsing logic needed
- ❌ Schema drift (file vs actual data)

---

#### Option 4: Schema Inference (Foreign Keys)
```cpp
// Analyze CSV headers and data to infer relationships
GraphSchema infer_schema(map<string, Table>& tables) {
    GraphSchema schema;

    // Detect primary keys (columns named "id", "pk", "*_id" with unique values)
    // Detect foreign keys (columns named "xxx_id" referencing table "xxx")
    // Build edge list from FK relationships

    for (auto& [name, table] : tables) {
        auto pk = detect_primary_key(table);
        if (pk) schema.add_node(name, pk);

        auto fks = detect_foreign_keys(table, tables);
        for (auto& fk : fks) {
            schema.add_edge(name, fk.ref_table, fk.column);
        }
    }
    return schema;
}
```

**Pros**:
- ✅ Fully automatic
- ✅ No manual metadata

**Cons**:
- ❌ Unreliable (heuristics fail on non-standard schemas)
- ❌ Expensive (requires data scanning)
- ❌ May infer wrong relationships

---

**Question**: How are your datasets structured? Do you have control over naming conventions?

**My Recommendation**: **Option 1 (Catalog Extension)** for correctness, with **Option 2 (Naming Convention)** as a shortcut for simple datasets.

---

### Q6: Oblivious Primitive Compatibility

**Problem**: Both systems use oblivious operations but may have different implementations

**Obligraph Primitives** (`obl_primitives.h` from Snoopy):
- `ObliviousSort(begin, end, comparator)` - Bitonic sort network
- `ObliviousCompact(begin, end, tags)` - Butterfly network compaction
- `ObliviousChoose(pred, t_val, f_val)` - Conditional move (CMOV)
- `ObliviousArrayAccess(arr, i, n)` - SIMD-optimized linear scan
- `ObliviousBytesAssign(pred, nbytes, t_val, f_val, out)` - AVX2 blending

**Implementation Details**:
- Assembly-level (`__asm__ volatile`) for constant-time guarantees
- AVX2 SIMD when `USE_AVX2` defined
- Cache-line-aware (`CACHE_LINE_SIZE = 192`)
- No branching on secret data

**Ruidi's Primitives** (need to verify):
- Likely custom implementations in `app/algorithms/`
- ShuffleManager (Waksman network) for oblivious permutation
- MergeSortManager for oblivious sorting
- Direct operations on `entry_t` structures

**Critical Security Question**: Do both implementations provide equivalent oblivious guarantees?

**Potential Issues**:
1. **Access Pattern Differences**: Snoopy's linear scan vs Ruidi's sorting
2. **Timing Channels**: Different implementations may have different timing behavior
3. **Cache Behavior**: Cache-line assumptions (192 vs 64 bytes?)
4. **Compiler Optimizations**: Assembly vs C++ (compiler may break obliviousness)

**Options**:

#### Option 1: Standardize on Obligraph/Snoopy Primitives
```cpp
// Replace Ruidi's sort calls with Snoopy's ObliviousSort
void Table::shuffle_merge_sort(OpEcall op_type) {
    // OLD: Custom shuffle + merge sort
    // NEW: Use Snoopy's primitives
    ObliviousSort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) {
                      return a.join_attr < b.join_attr;
                  });
}
```

**Pros**:
- ✅ Single trusted implementation
- ✅ Well-tested primitives (used in Secure XGBoost)
- ✅ AVX2 optimizations available

**Cons**:
- ❌ May break Ruidi's existing optimizations
- ❌ Performance regression possible
- ❌ Requires rewriting/refactoring

---

#### Option 2: Keep Separate, Verify Equivalence
```cpp
// Test harness
void verify_obliviousness() {
    // Run same operation with different inputs
    // Compare memory access traces (using Pin/Valgrind)

    auto data1 = generate_input(seed1);
    auto data2 = generate_input(seed2);

    auto trace1 = run_with_tracing(obligraph_sort, data1);
    auto trace2 = run_with_tracing(obligraph_sort, data2);
    assert(trace1 == trace2);  // Same access pattern

    auto trace3 = run_with_tracing(ruidi_sort, data1);
    auto trace4 = run_with_tracing(ruidi_sort, data2);
    assert(trace3 == trace4);  // Same access pattern
}
```

**Pros**:
- ✅ No code changes needed
- ✅ Keep existing optimizations
- ✅ Independent evolution

**Cons**:
- ❌ Verification overhead
- ❌ May find incompatibilities late
- ❌ Maintenance burden (verify after each change)

---

#### Option 3: Unified Oblivious Primitives Library
```cpp
// New library: libobl
namespace obl {
    // Unified interface
    template<typename Iter, typename Comparator>
    void sort(Iter begin, Iter end, Comparator cmp, SortAlgorithm algo = AUTO);

    template<typename Iter>
    void compact(Iter begin, Iter end, uint8_t* tags);

    template<typename T>
    T choose(bool pred, const T& t_val, const T& f_val);

    // Multiple implementations
    enum SortAlgorithm { BITONIC, WAKSMAN, MELLORSORT, AUTO };
}

// Both systems use unified API
```

**Pros**:
- ✅ Best-of-both-worlds (choose best algorithm per use case)
- ✅ Single API to maintain
- ✅ Easy to add new algorithms

**Cons**:
- ❌ Significant refactoring effort
- ❌ API design complexity
- ❌ May not fit both use cases perfectly

---

**Question**: How critical is provable obliviousness vs. performance for your research?

**My Recommendation**: **Option 2 (Keep Separate, Verify)** for Phase 1. Build verification test suite. If no issues, proceed. If problems found, migrate to **Option 1 (Snoopy Primitives)**.

---

### Q7: Testing & Validation Strategy

**Critical**: Integration must preserve correctness, security, and performance

#### Correctness Testing

**Option A: SQLite Baseline Comparison**
```bash
# Existing Ruidi test infrastructure
./test_join query.sql encrypted_data/

# New: Hybrid test
./test_hybrid_join graph_query.cypher encrypted_data/
# Internally:
#   1. Run hybrid (1-hop + Ruidi)
#   2. Run pure SQLite
#   3. Compare outputs (row-by-row)
```

**Test Cases**:
- Linear path queries (2-hop, 3-hop, 5-hop)
- Star queries (central node with multiple edges)
- Tree queries (branching paths)
- Queries with predicates (filters on nodes/edges)
- Edge cases (empty results, single row, cross products)

---

**Option B: Pure Ruidi Equivalence**
```bash
# Run same query with and without 1-hop optimization
./sgx_app --use-onehop=false query.sql data/ output1.csv
./sgx_app --use-onehop=true query.sql data/ output2.csv
diff output1.csv output2.csv  # Should be identical
```

**Benefits**:
- Isolates 1-hop correctness (vs baseline)
- No SQLite dependency
- Tests optimization orthogonality

---

#### Security Testing (Obliviousness Validation)

**Option A: Memory Trace Comparison**
```cpp
// Use instrumentation (Intel Pin, Valgrind, or custom)
void test_obliviousness() {
    // Generate datasets with same size, different values
    auto data1 = generate_graph(1000, seed=42);
    auto data2 = generate_graph(1000, seed=99);

    // Run with memory tracing
    auto trace1 = execute_with_tracing(hybrid_query, data1);
    auto trace2 = execute_with_tracing(hybrid_query, data2);

    // Compare memory access patterns
    assert(trace1.size() == trace2.size());
    for (size_t i = 0; i < trace1.size(); i++) {
        assert(trace1[i].address_offset == trace2[i].address_offset);
        // Note: Actual addresses differ, but offset patterns should match
    }
}
```

**Metrics**:
- Number of memory accesses (must be constant for fixed size)
- Access pattern (read/write sequence)
- Cache line accesses (detect data-dependent patterns)

---

**Option B: Operation Count Verification**
```cpp
// Instrument code to count operations
struct OpCounters {
    size_t sorts = 0;
    size_t compacts = 0;
    size_t choose_calls = 0;
    size_t hash_probes = 0;
};

// Run on different datasets
auto counts1 = run_instrumented(query, data1);
auto counts2 = run_instrumented(query, data2);

// All counts must match
assert(counts1 == counts2);
```

---

**Option C: Formal Verification (Ambitious)**
- Use tools like Cryptol/SAW for small components
- Prove oblivious property formally
- Out of scope for initial integration?

---

#### Performance Testing

**Benchmarks**:
1. **TPC-H Adapted**: Existing queries adapted to graph patterns
2. **LDBC SNB**: Social network benchmark (graph-native)
3. **Synthetic**: Parameterized (n-hop, selectivity, table sizes)

**Metrics**:
- **Execution Time**: Total query time (wall-clock)
- **Oblivious Operations**: Count of sorts, compacts, shuffles
- **Memory Usage**: Peak memory (especially with Entry bloat)
- **Speedup**: Hybrid vs Pure Ruidi (expect 2-3x per tech doc)

**Experiments**:
```bash
# Vary query complexity
for hops in 2 3 5 7; do
    ./benchmark --query=linear_${hops}_hop --method=hybrid
    ./benchmark --query=linear_${hops}_hop --method=pure_ruidi
done

# Vary scale
for scale in 0.001 0.01 0.1 1.0; do
    ./benchmark --query=tpch_q1 --scale=$scale --method=hybrid
done

# Vary selectivity
for sel in 0.01 0.1 0.5 0.9; do
    ./benchmark --query=filtered_path --selectivity=$sel --method=hybrid
done
```

---

**Question**: What's your priority? Correctness → Security → Performance? Or parallel validation?

**My Recommendation**:
1. **Phase 1**: Correctness (SQLite baseline) + Basic operation count check
2. **Phase 2**: Detailed obliviousness validation (memory traces)
3. **Phase 3**: Performance benchmarking (TPC-H + LDBC)

---

### Q8: Incremental vs. Big Bang Integration

**Trade-off**: Risk vs. Time-to-Result

#### Incremental Approach (Recommended)

**Phase 1: Proof-of-Concept (2-3 weeks)**
- ✅ Adapter layer (Row ↔ Entry conversion)
- ✅ Pre-processing pipeline (1-hop → CSV → Ruidi)
- ✅ Single test query (2-hop linear path)
- ✅ Correctness validation (compare with SQLite)

**Deliverable**: Working demo, measure conversion overhead

---

**Phase 2: In-Memory Integration (3-4 weeks)**
- ✅ Direct memory passing (no CSV I/O)
- ✅ Extend JoinTreeBuilder with triplet detection
- ✅ Greedy triplet selection heuristic
- ✅ Multiple test queries (linear, star patterns)
- ✅ Basic obliviousness testing (operation counts)

**Deliverable**: Prototype system, initial performance data

---

**Phase 3: Query Planner (4-6 weeks)**
- ✅ Unified query interface (SQL or Cypher)
- ✅ Cost-based triplet selection
- ✅ GraphCatalog for metadata management
- ✅ Full test suite (TPC-H adapted queries)
- ✅ Memory trace validation

**Deliverable**: Research prototype, draft paper results

---

**Phase 4: Optimization (4-6 weeks)**
- ✅ Optimize Row ↔ Entry conversion (batching, SIMD)
- ✅ Parallel 1-hop execution
- ✅ Fine-tuned oblivious primitives
- ✅ Comprehensive benchmarks (LDBC, synthetic)
- ✅ Performance comparison with baselines

**Deliverable**: Camera-ready system, full evaluation

---

**Total Timeline**: ~15-20 weeks (4-5 months)

**Alternatively: Big Bang Approach**
- Build complete unified system from scratch
- Risk: Integration issues discovered late
- Benefit: Optimal design (no legacy constraints)
- Timeline: ~6-8 months

**Question**: What's your deadline? Conference submission? Thesis chapter? Grant milestone?

**My Recommendation**: **Incremental approach** for research projects. Get early results, publish iteratively.

---

### Q9: TDX-Specific Considerations

**Context**: Both codebases are TDX-ready (post-SGX migration)

**TDX Benefits for Integration**:
- ✅ No ecall overhead (direct function calls)
- ✅ No encryption layer (TDX handles transparently)
- ✅ Unified memory space (no enclave boundary)
- ✅ Simpler debugging (GDB works inside TDX VM)

**Potential Issues**:

#### Memory Management
- **Obligraph**: Uses standard `vector<Row>`, `ThreadPool` allocations
- **Ruidi**: Pre-allocates large `vector<Entry>` for join operations
- **Integration**: May need unified memory pool? Or separate heaps OK?

**Question**: Are there TDX memory limits we should be aware of? (e.g., max VM size)

---

#### Thread Pool Coordination
- **Obligraph**: Uses custom `ThreadPool` class for parallel operations
- **Ruidi**: May have its own threading (need to check)
- **Integration**: Single unified thread pool or separate?

```cpp
// Potential conflict
ThreadPool obligraph_pool(8);  // For 1-hop
ThreadPool ruidi_pool(8);      // For join phases
// → 16 threads competing? Or share pool?
```

**Question**: How many cores will the TDX VM have access to?

---

#### Performance Profiling
- **TDX Overhead**: Minimal compared to SGX, but still exists
- **Profiling Tools**: Can we use `perf`, `vtune` inside TDX VM?
- **Optimization**: TDX-specific optimizations (e.g., cache line alignment)?

**Question**: Do you have TDX hardware available for testing? Or simulation?

---

#### Deployment Model
- **Single TDX VM**: Entire hybrid system runs in one VM
- **Multiple VMs**: 1-hop in one VM, Ruidi in another (distributed)
- **Hybrid**: Some tables in TDX, some in plaintext (if public)

**Question**: What's your target deployment scenario?

---

**My Recommendation**: Keep it simple initially (single TDX VM, shared thread pool, standard allocators). Optimize later if profiling shows issues.

---

### Q10: Example Use Case & Validation ⚠️ **CRITICAL FOR PLANNING**

**Please provide a concrete example query you want to run!**

This will help me:
- Understand your data model
- Design exact integration points
- Validate the approach with realistic workload
- Estimate performance benefits

**Ideal Information**:
1. **Query** (in any format):
   ```cypher
   MATCH (p:Person)-[w:WorksAt]->(o:Org)-[l:LocatedIn]->(c:City)
   WHERE c.name = 'Waterloo' AND p.age > 25
   RETURN p.name, o.name
   ```

2. **Schema**:
   ```
   Person: id (PK), name, age
   Org: id (PK), name
   City: id (PK), name
   WorksAt: person_id (FK), org_id (FK), since
   LocatedIn: org_id (FK), city_id (FK)
   ```

3. **Data Scale**:
   - |Person| = 1,000
   - |Org| = 100
   - |City| = 50
   - |WorksAt| = 2,000
   - |LocatedIn| = 100

4. **Expected Triplets**:
   - Option A: (Person, WorksAt, Org) → T1 with size 2,000
   - Option B: (Org, LocatedIn, City) → T1 with size 100
   - Both valid! Which is better? Need cost model.

5. **Current Status**:
   - Do you have this data in CSV format?
   - Can you run it with pure Ruidi now?
   - What's the baseline performance?

---

**Alternative**: If you don't have a specific query, tell me:
- **Domain**: Social network? Knowledge graph? Business data?
- **Query Patterns**: Mostly 2-hop? Long paths? Star queries?
- **Data Characteristics**: Sparse or dense? Uniform or skewed degree distribution?

---

## Summary: Critical Decision Points

| Question | Impact | Urgency | Recommendation |
|----------|--------|---------|----------------|
| **Q1: Data Structure** | High | Critical | Adapter Layer (Phase 1) |
| **Q2: Architecture** | High | Critical | Unified Query Planner |
| **Q3: Query Language** | Medium | High | Programmatic API first |
| **Q4: Triplet Selection** | High | High | Greedy Heuristic |
| **Q5: Schema Management** | Medium | Medium | Catalog Extension |
| **Q6: Oblivious Primitives** | High | Critical | Verify Equivalence |
| **Q7: Testing Strategy** | High | Medium | Incremental (Correctness→Security→Perf) |
| **Q8: Integration Approach** | High | Critical | Incremental (4 phases) |
| **Q9: TDX Considerations** | Low | Low | Standard approach (optimize later) |
| **Q10: Example Query** | High | Critical | **NEED INPUT** |

---

## Next Steps

1. **Answer Q10**: Provide example query + data → Concrete integration plan
2. **Answer Q1, Q2, Q4**: Core architecture decisions → Begin implementation
3. **Set up test infrastructure**: Baseline measurements → Track progress
4. **Phase 1 Kickoff**: Adapter layer + pre-processing pipeline → Proof of concept

**Timeline Estimate** (pending answers):
- Phase 1: 2-3 weeks
- Phase 2: 3-4 weeks
- Phase 3: 4-6 weeks
- Phase 4: 4-6 weeks
- **Total**: 4-5 months to research-ready prototype

---

## Open Research Questions

Beyond integration mechanics:

1. **Theoretical**: Can we prove the hybrid approach is optimal for certain query classes?
2. **Security**: Formal verification of combined oblivious guarantees?
3. **Generalization**: Extend to cyclic queries (WCOJ integration)?
4. **Adaptivity**: Runtime decision (use 1-hop or not) based on data statistics?
5. **Distribution**: Scale to multiple TDX VMs for very large graphs?

---

**Document Status**: Draft v1.0 - Awaiting answers to finalize integration plan

**Last Updated**: 2025-10-15
