# Phase 1: Datasets & Workloads for NebulaDB Evaluation

## Context

NebulaDB needs 3 diverse workloads for a SIGMOD/VLDB evaluation: a financial graph (existing), a standard benchmark graph (LDBC SNB), and a real-world graph (SNAP). All data must be int32 values within [-1,073,741,820, 1,073,741,820], CSV format with sentinel rows.

**System constraint**: Integer-only properties, CSV input with header row, sentinel row at end (value -10000).

---

## W1: Banking / Financial Fraud Detection (existing)

**Status**: Already implemented.

- **Tables**: Account (nodes), Owner (nodes), Transaction (edges)
- **Properties**: id, balance, amount, timestamp — all integers
- **Generator**: `scripts/generate_banking_scaled.py`
- **Degree distribution**: Zipfian (realistic)
- **Existing scales**: Up to 1M accounts (5M txns at 5x ratio)

**Work needed**:
- Verify generator works at 2M accounts / 10M txns (our 10M edge target)
- If not, extend generator to handle larger scales efficiently (streaming mode already exists)

**Files to modify**: `scripts/generate_banking_scaled.py` (if needed)

---

## W2: Social Network — LDBC SNB

**Source**: LDBC SNB data generator — the standard graph DB benchmark.
**Tables used**: Person (nodes) and Person_knows_Person (edges) only.

**Reference**: LDBC Interactive Complex query IC1 ("Transitive friends with certain name") is a 3-hop chain traversal over Person→Knows→Person with attribute filter — our chain queries are directly inspired by it.

### Schema (after conversion to int32)

**Person table** (`person.csv`):
| Column | Source | Conversion |
|--------|--------|------------|
| `id` | `Person.id` | Remapped to contiguous [1, N] |
| `age` | `Person.birthday` | `current_year - birth_year` |
| `city_id` | `Person.LocationCityId` | Direct (already integer FK) |
| `join_date` | `Person.creationDate` | Days since epoch |

**Knows table** (`knows.csv`):
| Column | Source | Conversion |
|--------|--------|------------|
| `src_id` | `Knows.Person1Id` | Remapped (same mapping as Person.id) |
| `dst_id` | `Knows.Person2Id` | Remapped |
| `since_date` | `Knows.creationDate` | Days since epoch |

### LDBC Scale Factors

Source: [ldbc_snb_docs entity counts](https://github.com/ldbc/ldbc_snb_docs/blob/main/tables/legacy/table-number-of-entities-interactive.tex) (Hadoop-based datagen).

| SF | Persons | Knows Edges | Avg Degree |
|-----|---------|-------------|------------|
| 1 | 11,000 | 226,515 | ~41 |
| 10 | 73,000 | 2,431,407 | ~67 |
| 30 | 182,000 | 7,514,541 | ~83 |
| 100 | 499,000 | 24,842,767 | ~100 |

We need SF1, SF10, SF30, and SF100.

### Data Pipeline

1. **Install LDBC datagen**:
   - Clone `github.com/ldbc/ldbc_snb_datagen_spark` (Spark-based, current version)
   - Requires Java 11+, Apache Spark 3.x
   - Alternative: use the Docker image for reproducibility
   - Alternative: download pre-generated datasets from LDBC if available
2. **Generate raw data**: Run datagen at each SF, output to CSV
   - Key output files: `dynamic/Person.csv`, `dynamic/Person_knows_Person.csv`
3. **Convert to NebulaDB format** (`scripts/convert_ldbc_to_nebuladb.py`):
   - Parse LDBC CSV (pipe-delimited `|` format)
   - Build ID remap: LDBC Person IDs → contiguous [1, N]
   - Compute `age` from birthday string
   - Convert ISO dates to epoch days for `join_date` and `since_date`
   - `city_id`: use LDBC's integer LocationCityId directly (already int FK)
   - Validate all values within bounds
   - Append sentinel row (-10000 for all columns)
   - Output: comma-delimited CSV with header row
   - Print stats: median/max degree, age distribution, suggested start person IDs (median-degree nodes)

### Query Suite (10 queries)

All k-hop queries fix the start person: `p1.id = <X>`. The conversion script outputs recommended start person IDs (median-degree nodes) per SF. The value `933` below is a placeholder.

**Estimated result sizes** (SF10, avg degree ~67):
- 1-hop: ~67 rows, 2-hop: ~4.5K rows, 3-hop: ~300K rows
- 4-hop: ~20M rows (needs endpoint filter), 5-hop: ~1.3B rows (needs endpoint filter)

#### Category A: k-hop Chain Queries (5 queries)

- **`ldbc_1hop.sql`** — 1-hop: direct friends of person X (3 tables)
  ```sql
  SELECT * FROM person AS p1, knows AS k, person AS p2
  WHERE p1.id = k.src_id AND k.dst_id = p2.id
    AND p1.id = 933;
  ```
- **`ldbc_2hop.sql`** — 2-hop: friends-of-friends (5 tables)
  ```sql
  SELECT * FROM person AS p1, knows AS k1, person AS p2, knows AS k2, person AS p3
  WHERE p1.id = k1.src_id AND k1.dst_id = p2.id
    AND p2.id = k2.src_id AND k2.dst_id = p3.id
    AND p1.id = 933;
  ```
- **`ldbc_3hop.sql`** — 3-hop: 3rd-order connections (7 tables)
  ```sql
  SELECT * FROM person AS p1, knows AS k1, person AS p2, knows AS k2, person AS p3, knows AS k3, person AS p4
  WHERE p1.id = k1.src_id AND k1.dst_id = p2.id
    AND p2.id = k2.src_id AND k2.dst_id = p3.id
    AND p3.id = k3.src_id AND k3.dst_id = p4.id
    AND p1.id = 933;
  ```
- **`ldbc_4hop.sql`** — 4-hop with endpoint filter (9 tables)
  ```sql
  SELECT * FROM person AS p1, knows AS k1, person AS p2, knows AS k2, person AS p3, knows AS k3, person AS p4, knows AS k4, person AS p5
  WHERE p1.id = k1.src_id AND k1.dst_id = p2.id
    AND p2.id = k2.src_id AND k2.dst_id = p3.id
    AND p3.id = k3.src_id AND k3.dst_id = p4.id
    AND p4.id = k4.src_id AND k4.dst_id = p5.id
    AND p1.id = 933 AND p5.age > 40;
  ```
- **`ldbc_5hop.sql`** — 5-hop with endpoint filter (11 tables)
  ```sql
  SELECT * FROM person AS p1, knows AS k1, person AS p2, knows AS k2, person AS p3, knows AS k3, person AS p4, knows AS k4, person AS p5, knows AS k5, person AS p6
  WHERE p1.id = k1.src_id AND k1.dst_id = p2.id
    AND p2.id = k2.src_id AND k2.dst_id = p3.id
    AND p3.id = k3.src_id AND k3.dst_id = p4.id
    AND p4.id = k4.src_id AND k4.dst_id = p5.id
    AND p5.id = k5.src_id AND k5.dst_id = p6.id
    AND p1.id = 933 AND p6.age > 40;
  ```

#### Category B: Branch Query (1 query)

Inspired by `banking_branch_filtered.sql`. Chain from p1, p2 forks to two 2nd-order contacts. Tests bushy vs linear join tree decomposition.

- **`ldbc_branch.sql`** — Branch at midpoint (7 tables)
  ```sql
  SELECT * FROM person AS p1, knows AS k1, person AS p2, knows AS k2, person AS p3, knows AS k3, person AS p4
  WHERE p1.id = k1.src_id AND k1.dst_id = p2.id
    AND p2.id = k2.src_id AND k2.dst_id = p3.id
    AND p2.id = k3.src_id AND k3.dst_id = p4.id
    AND p1.id = 933;
  ```

#### Category C: Selectivity Variants (4 queries)

2-hop from fixed start person, varying endpoint filter on p3. Semantics: "2nd-order connections of person X older than Y."

- **`ldbc_sel_low.sql`** — ~80% pass: `... AND p1.id = 933 AND p3.age > 20;`
- **`ldbc_sel_med.sql`** — ~60% pass: `... AND p1.id = 933 AND p3.age > 30;`
- **`ldbc_sel_high.sql`** — ~40% pass: `... AND p1.id = 933 AND p3.age > 40;`
- **`ldbc_sel_vhigh.sql`** — ~20% pass: `... AND p1.id = 933 AND p3.age > 50;`

(All share the same 2-hop join structure as `ldbc_2hop.sql`.)

### Files to Create
- `scripts/convert_ldbc_to_nebuladb.py` — conversion script
- `input/queries/ldbc_1hop.sql` through `ldbc_5hop.sql` — chain queries (5 files)
- `input/queries/ldbc_branch.sql` — branch query
- `input/queries/ldbc_sel_low.sql` through `ldbc_sel_vhigh.sql` — selectivity variants (4 files)
- `input/plaintext/ldbc_sf1/` through `ldbc_sf100/` — output directories

---

## W3: Patent Citation Network — SNAP cit-Patents

**Source**: [SNAP cit-Patents](https://snap.stanford.edu/data/cit-Patents.html) edge list + [NBER US Patent Data](https://www.nber.org/research/data/us-patents-1975-1999) metadata.
**Domain**: US patent citations (1963–1999) — a different domain from W1 (financial) and W2 (social), providing a directed acyclic graph (DAG) with real attributes.

### Dataset

- **Nodes**: 3,774,768 patents
- **Edges**: 16,518,948 directed citations (citing → cited)
- **Structure**: DAG (citations only go backward in time), diameter 22, avg clustering 0.08
- **Avg out-degree**: ~4.3 (much sparser than LDBC's 40–100)
- **Scale**: Full dataset only, no subsampling

**Key differences from W2 (LDBC social network):**

| Property | W2: LDBC (social) | W3: cit-Patents (citation) |
|----------|-------------------|---------------------------|
| Edge semantics | Friendship (bidirectional) | Citation (directed, temporal) |
| Graph structure | Small-world, high clustering | DAG, low clustering (0.08) |
| Avg degree | 41–100 | ~4.3 (directed) |
| Node attributes | Synthetic (age from birthday) | Real (grant year, tech category, claims) |
| Result explosion | Yes (endpoint filters at 4-5 hop) | No (sparse even at 5 hops) |

### Schema (real attributes from NBER data)

All attributes are naturally integers — no string hashing or synthetic generation needed.

**Patent table** (`patent.csv`):
| Column | Source | Description |
|--------|--------|-------------|
| `id` | `pat63_99.patent` | Remapped to contiguous [1, N] |
| `gyear` | `pat63_99.gyear` | Grant year (1963–1999) |
| `cat` | `pat63_99.cat` | Technology category (1–6: Computers, Drugs/Medical, Electrical, Chemical, Mechanical, Other) |
| `claims` | `pat63_99.claims` | Number of claims |

**Citation table** (`citation.csv`):
| Column | Source | Description |
|--------|--------|-------------|
| `src_id` | `cit-Patents citing` | Citing patent (remapped) |
| `dst_id` | `cit-Patents cited` | Cited patent (remapped) |

### Data Pipeline

1. **Download**:
   - SNAP edge list: `snap.stanford.edu/data/cit-Patents.txt.gz`
   - NBER patent metadata: `nber.org` → `pat63_99.zip` (ASCII CSV)
2. **Convert** (`scripts/convert_patents_to_nebuladb.py`):
   - Parse SNAP edge list (tab-separated, skip `#` comment lines)
   - Parse NBER `pat63_99` CSV (patent, gyear, cat, claims columns)
   - Join edge list with patent metadata on patent number
   - Remap patent IDs to contiguous [1, N]
   - Handle missing values: patents without metadata get `gyear=0, cat=0, claims=0`
   - Validate all values within bounds
   - Append sentinel row (-10000 for all columns)
   - Output: comma-delimited CSV with header row
   - Print stats: out-degree distribution, gyear distribution, suggested start patent IDs

### Query Suite (10 queries)

All queries follow citation chains in the forward direction (citing → cited, i.e., following references backward in time). With avg out-degree ~4.3, no endpoint filters are needed at any depth.

**Estimated result sizes** (avg out-degree ~4.3):
- 1-hop: ~4 rows, 2-hop: ~19, 3-hop: ~82, 4-hop: ~353, 5-hop: ~1.5K

The start patent ID is a placeholder (`933`); the conversion script outputs recommended IDs.

#### Category A: k-hop Chain Queries (5 queries)

- **`snap_1hop.sql`** — 1-hop: direct references of patent X (3 tables)
  ```sql
  SELECT * FROM patent AS p1, citation AS c, patent AS p2
  WHERE p1.id = c.src_id AND c.dst_id = p2.id
    AND p1.id = 933;
  ```
- **`snap_2hop.sql`** — 2-hop: references of references (5 tables)
  ```sql
  SELECT * FROM patent AS p1, citation AS c1, patent AS p2, citation AS c2, patent AS p3
  WHERE p1.id = c1.src_id AND c1.dst_id = p2.id
    AND p2.id = c2.src_id AND c2.dst_id = p3.id
    AND p1.id = 933;
  ```
- **`snap_3hop.sql`** — 3-hop: 3rd-order references (7 tables)
  ```sql
  SELECT * FROM patent AS p1, citation AS c1, patent AS p2, citation AS c2, patent AS p3, citation AS c3, patent AS p4
  WHERE p1.id = c1.src_id AND c1.dst_id = p2.id
    AND p2.id = c2.src_id AND c2.dst_id = p3.id
    AND p3.id = c3.src_id AND c3.dst_id = p4.id
    AND p1.id = 933;
  ```
- **`snap_4hop.sql`** — 4-hop (9 tables)
  ```sql
  SELECT * FROM patent AS p1, citation AS c1, patent AS p2, citation AS c2, patent AS p3, citation AS c3, patent AS p4, citation AS c4, patent AS p5
  WHERE p1.id = c1.src_id AND c1.dst_id = p2.id
    AND p2.id = c2.src_id AND c2.dst_id = p3.id
    AND p3.id = c3.src_id AND c3.dst_id = p4.id
    AND p4.id = c4.src_id AND c4.dst_id = p5.id
    AND p1.id = 933;
  ```
- **`snap_5hop.sql`** — 5-hop (11 tables)
  ```sql
  SELECT * FROM patent AS p1, citation AS c1, patent AS p2, citation AS c2, patent AS p3, citation AS c3, patent AS p4, citation AS c4, patent AS p5, citation AS c5, patent AS p6
  WHERE p1.id = c1.src_id AND c1.dst_id = p2.id
    AND p2.id = c2.src_id AND c2.dst_id = p3.id
    AND p3.id = c3.src_id AND c3.dst_id = p4.id
    AND p4.id = c4.src_id AND c4.dst_id = p5.id
    AND p5.id = c5.src_id AND c5.dst_id = p6.id
    AND p1.id = 933;
  ```

#### Category B: Branch Query (1 query)

A cited patent has two independent reference chains. Tests bushy join tree decomposition.

- **`snap_branch.sql`** — Branch at midpoint (7 tables)
  ```sql
  SELECT * FROM patent AS p1, citation AS c1, patent AS p2, citation AS c2, patent AS p3, citation AS c3, patent AS p4
  WHERE p1.id = c1.src_id AND c1.dst_id = p2.id
    AND p2.id = c2.src_id AND c2.dst_id = p3.id
    AND p2.id = c3.src_id AND c3.dst_id = p4.id
    AND p1.id = 933;
  ```

#### Category C: Selectivity Variants (4 queries)

2-hop from fixed start patent, varying endpoint filter on p3's grant year. Semantics: "references of references granted after year Y."

- **`snap_sel_low.sql`** — ~70% pass: `... AND p1.id = 933 AND p3.gyear > 1975;`
- **`snap_sel_med.sql`** — ~50% pass: `... AND p1.id = 933 AND p3.gyear > 1980;`
- **`snap_sel_high.sql`** — ~30% pass: `... AND p1.id = 933 AND p3.gyear > 1985;`
- **`snap_sel_vhigh.sql`** — ~15% pass: `... AND p1.id = 933 AND p3.gyear > 1990;`

(All share the same 2-hop join structure as `snap_2hop.sql`.)

### Files to Create
- `scripts/convert_patents_to_nebuladb.py` — download + merge + conversion script
- `input/queries/snap_1hop.sql` through `snap_5hop.sql` — chain queries (5 files)
- `input/queries/snap_branch.sql` — branch query
- `input/queries/snap_sel_low.sql` through `snap_sel_vhigh.sql` — selectivity variants (4 files)
- `input/plaintext/snap_patents/` — output directory (patent.csv, citation.csv)

---

## W4: Controlled Synthetic Graphs (for sensitivity experiments)

For experiments E5 (degree distribution) and E7 (edge-to-node ratio), we need graphs with precise control over parameters.

### Controlled Degree Distribution Generator

Generate a graph with exactly N_edges edges, where the degree of each node follows a specified distribution:
- **Uniform**: Every node has the same degree (N_edges / N_nodes)
- **Zipfian**: Degree ~ Zipf(alpha), alpha in {1.0, 1.5, 2.0}
- **Power-law**: Degree ~ Pareto(alpha=2.1), matching real social networks

All use the same total edge count (e.g., 1M) so comparisons are fair.

### Variable Ratio Generator

Fix total edges at 1M, vary edges-per-node:
| Ratio | Nodes | Edges |
|-------|-------|-------|
| 2x | 500K | 1M |
| 5x | 200K | 1M |
| 10x | 100K | 1M |
| 25x | 40K | 1M |
| 50x | 20K | 1M |
| 100x | 10K | 1M |

### Files to Create
- `scripts/generate_controlled_degree.py` — parameterized graph generator
- Output directories under `input/plaintext/synthetic_*/`

---

## Implementation Order

1. **Verify W1** at 10M edge scale (quick — just run existing generator)
2. **Build W3 (SNAP)** — simpler pipeline (just download + convert, no external tool deps)
3. **Build W2 (LDBC)** — requires LDBC datagen setup (Java/Spark dependency)
4. **Build W4 (Synthetic)** — straightforward Python generator

### Verification for Each Workload
- [ ] Generated CSVs parse correctly (header, correct column count, all values int32)
- [ ] All values within [-1,073,741,820, 1,073,741,820]
- [ ] Sentinel row present at end of each CSV
- [ ] Foreign keys valid (edge src/dst IDs exist in node table)
- [ ] NebulaDB can load and run a 1-hop query against the data without errors
- [ ] Output matches SQLite baseline for the same query
