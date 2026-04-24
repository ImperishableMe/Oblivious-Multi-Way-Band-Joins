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

### Query Suite (Banking)

#### Category A: k-hop Chain Queries (5 queries)

- **`banking_1hop.sql`** — 1-hop: direct transactions from a filtered account (3 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t, account AS a2
  WHERE a1.account_id = t.acc_from AND a2.account_id = t.acc_to
    AND a1.balance > 10000;
  ```
- **`banking_2hop.sql`** — 2-hop chain (5 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2, account AS a3
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a2.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a1.owner_id = 52 AND a3.owner_id = 45;
  ```
- **`banking_3hop.sql`** — 3-hop chain (7 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2, account AS a3, txn AS t3, account AS a4
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a2.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a3.account_id = t3.acc_from AND a4.account_id = t3.acc_to
    AND a1.owner_id = 52 AND a4.owner_id = 45;
  ```
- **`banking_4hop.sql`** — 4-hop chain (9 tables, exists as `banking_chain4_filtered.sql`)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2, account AS a3, txn AS t3, account AS a4, txn AS t4, account AS a5
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a2.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a3.account_id = t3.acc_from AND a4.account_id = t3.acc_to
    AND a4.account_id = t4.acc_from AND a5.account_id = t4.acc_to
    AND a1.owner_id = 52 AND a5.owner_id = 45;
  ```
- **`banking_5hop.sql`** — 5-hop chain (11 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2, account AS a3, txn AS t3, account AS a4, txn AS t4, account AS a5, txn AS t5, account AS a6
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a2.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a3.account_id = t3.acc_from AND a4.account_id = t3.acc_to
    AND a4.account_id = t4.acc_from AND a5.account_id = t4.acc_to
    AND a5.account_id = t5.acc_from AND a6.account_id = t5.acc_to
    AND a1.owner_id = 52 AND a6.owner_id = 45;
  ```

#### Category B: Star Query — Smurfing Detection (1 query)

Detects the "smurfing" money laundering pattern: a single high-balance account distributing funds to 4 separate recipients simultaneously.

```
             (a3)
              ^
              |
            [t2]
              |
(a2) <--[t1]-- (a1) --[t3]--> (a4)
              |
            [t4]
              |
              v
             (a5)
```

**Decomposition**: 9-way MWJ → 4 ForwardFill hops + 4-way reduced MWJ. Each branch `(a1)→[ti]→(ai)` is an independent hop. ForwardFill is computed once on `(account, txn, account)` and reused as 4 aliases. The reduced MWJ joins on the shared source `a1.account_id`.

- **`banking_star4.sql`** — 4-branch star (9 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2,
               txn AS t2, account AS a3,
               txn AS t3, account AS a4,
               txn AS t4, account AS a5
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a1.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a1.account_id = t3.acc_from AND a4.account_id = t3.acc_to
    AND a1.account_id = t4.acc_from AND a5.account_id = t4.acc_to
    AND a1.balance > 50000;
  ```

#### Category C: Tree Query (1 query)

Combines chain and branch: from a flagged account, follow one transaction to an intermediary, then trace two independent paths — one direct transfer and one 2-hop chain. Detects tree-shaped fund distribution.

- **`banking_tree.sql`** — Tree pattern (9 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2,
               txn AS t2, account AS a3,
               txn AS t3, account AS a4, txn AS t4, account AS a5
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a2.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a2.account_id = t3.acc_from AND a4.account_id = t3.acc_to
    AND a4.account_id = t4.acc_from AND a5.account_id = t4.acc_to
    AND a1.owner_id = 52;
  ```

### Files to Create
- `input/queries/banking_1hop.sql` through `banking_5hop.sql` — chain queries (5 files)
- `input/queries/banking_star4.sql` — 4-branch star query
- `input/queries/banking_tree.sql` — tree pattern query

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

> **Note**: the conversion script should report max out-degree alongside the average — the tail matters for query result sizes.

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

## W4: Anti-Money Laundering — IBM AML-Data

**Source**: [IBM/AML-Data](https://github.com/IBM/AML-Data) — synthetic financial transactions generated via multi-agent simulation, published at NeurIPS 2023 ([Altman et al., arXiv:2306.16424](https://arxiv.org/abs/2306.16424)).

**Domain**: Synthetic financial graph with ground-truth laundering labels. Complements Banking W1 by providing (a) a second financial-graph data point for E1 cross-dataset validation and (b) scale well beyond W1's 10M-edge target (up to 180M transactions).

### Key differences from W1 Banking

| Property | W1 Banking | W4 IBM AML-Data |
|----------|------------|------------------|
| Max scale | 10M txns | 180M txns |
| Node attributes | `balance`, `owner_id` | `bank_id` only (no balance) |
| Edge attributes | `amount`, `txn_time` | `amount`, `txn_time`, `currency`, `payment_format`, `is_laundering` |
| Ground-truth labels | none | per-edge `is_laundering` + separate pattern file |
| Origin | our synthetic generator | published NeurIPS'23 benchmark |

### Dataset Variants

From Altman et al. (NeurIPS 2023):

| Variant | Days | Accounts | Transactions | Laundering rate |
|---------|------|----------|--------------|------------------|
| HI-Small | 10 | 515K | 5M | 1/981 |
| HI-Medium | 16 | 2.08M | 32M | 1/905 |
| HI-Large | 97 | 2.12M | 180M | 1/807 |

We generate **HI-Small** and **HI-Medium**; **HI-Large** is a stretch goal for the E2 scaling ceiling. LI-* variants are skipped — same query shapes, only label density differs, and our metric is latency not detection accuracy.

### Schema (after conversion to int32)

The published dataset ships only a **transaction file** — no separate accounts table. The conversion script synthesizes the accounts table by taking the union of `(From Bank, Account)` and `(To Bank, Account.1)`.

**Account table** (`account.csv`, synthesized):

| Column | Source | Conversion |
|--------|--------|------------|
| `account_id` | distinct `Account` / `Account.1` hex strings | remapped to contiguous [1, N] |
| `bank_id` | `From Bank` / `To Bank` | already integer; preserved |

**Transaction table** (`txn.csv`):

| Column | Source | Conversion |
|--------|--------|------------|
| `txn_id` | row index | 1..M contiguous |
| `acc_from` | `Account` | remapped |
| `acc_to` | `Account.1` | remapped |
| `amount` | `Amount Paid` | `round(Amount Paid * 100)` — cents as int32 |
| `txn_time` | `Timestamp` | epoch days since min timestamp |
| `currency` | `Payment Currency` | integer code per distinct currency string |
| `payment_format` | `Payment Format` | integer code per distinct format string |
| `is_laundering` | `Is Laundering` | passthrough 0/1 |

### Cyclic Patterns Excluded

The IBM pattern zoo contains 8 labeled typologies. Five are **structurally cyclic** in their join graph (two paths converge on the same vertex pair, producing cycles in the schema hypergraph) and are **not supported by NebulaDB's acyclic-MWJ pipeline**:

| Pattern | Supported? | Reason |
|---------|-----------|--------|
| Fan-out | ✓ | Star — acyclic |
| Fan-in | ✓ | Star — acyclic |
| Gather-scatter | ✓ | Star centered at hub — acyclic |
| Scatter-gather | ✗ | 4-cycle in join graph (two paths converge) |
| Bipartite | ✗ | K₂,₂ is a 4-cycle |
| Stack | ✗ | Two stacked bipartite layers — cyclic |
| Cycle | ✗ | Cyclic by definition |
| Random | ✗ | General cyclic |

W4 queries cover only the three acyclic patterns plus k-hop chains.

### Query Suite (12 queries)

All queries anchor the starting account set with `a1.bank_id = X`, where `X` is picked at data-gen time to yield a few thousand starting accounts (bounds the join result at higher hops). Multi-edge patterns additionally require `t_i.amount > X` on every edge to preserve "large-flow" AML semantics.

Placeholders below: `5` for `bank_id`, `50000` for `amount`. Actual values emitted by the conversion script.

#### Category A: k-hop Chain Queries (5 queries)

- **`aml_1hop.sql`** — 1-hop (3 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a1.bank_id = 5;
  ```
- **`aml_2hop.sql`** — 2-hop (5 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2, account AS a3
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a2.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a1.bank_id = 5;
  ```
- **`aml_3hop.sql`** — 3-hop (7 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2, account AS a3, txn AS t3, account AS a4
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a2.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a3.account_id = t3.acc_from AND a4.account_id = t3.acc_to
    AND a1.bank_id = 5;
  ```
- **`aml_4hop.sql`** — 4-hop (9 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2, account AS a3, txn AS t3, account AS a4, txn AS t4, account AS a5
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a2.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a3.account_id = t3.acc_from AND a4.account_id = t3.acc_to
    AND a4.account_id = t4.acc_from AND a5.account_id = t4.acc_to
    AND a1.bank_id = 5;
  ```
- **`aml_5hop.sql`** — 5-hop (11 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2, txn AS t2, account AS a3, txn AS t3, account AS a4, txn AS t4, account AS a5, txn AS t5, account AS a6
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a2.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a3.account_id = t3.acc_from AND a4.account_id = t3.acc_to
    AND a4.account_id = t4.acc_from AND a5.account_id = t4.acc_to
    AND a5.account_id = t5.acc_from AND a6.account_id = t5.acc_to
    AND a1.bank_id = 5;
  ```

#### Category B: Fan-out (1 query)

One source distributes large amounts to 4 distinct recipients — the classic "one-to-many" laundering shape.

- **`aml_fanout.sql`** — 4-branch fan-out (9 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2,
               txn AS t2, account AS a3,
               txn AS t3, account AS a4,
               txn AS t4, account AS a5
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a1.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a1.account_id = t3.acc_from AND a4.account_id = t3.acc_to
    AND a1.account_id = t4.acc_from AND a5.account_id = t4.acc_to
    AND a1.bank_id = 5
    AND t1.amount > 50000 AND t2.amount > 50000
    AND t3.amount > 50000 AND t4.amount > 50000;
  ```

#### Category C: Fan-in (1 query)

Four sources funnel large amounts into a single destination — "many-to-one" gathering.

- **`aml_fanin.sql`** — 4-branch fan-in (9 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a5,
               account AS a2, txn AS t2,
               account AS a3, txn AS t3,
               account AS a4, txn AS t4
  WHERE a1.account_id = t1.acc_from AND a5.account_id = t1.acc_to
    AND a2.account_id = t2.acc_from AND a5.account_id = t2.acc_to
    AND a3.account_id = t3.acc_from AND a5.account_id = t3.acc_to
    AND a4.account_id = t4.acc_from AND a5.account_id = t4.acc_to
    AND a5.bank_id = 5
    AND t1.amount > 50000 AND t2.amount > 50000
    AND t3.amount > 50000 AND t4.amount > 50000;
  ```

#### Category D: Tree (1 query)

Root chains into a hub, hub splits into one direct recipient and one 2-hop chain — same structure as `banking_tree`.

- **`aml_tree.sql`** — tree pattern (9 tables)
  ```sql
  SELECT * FROM account AS a1, txn AS t1, account AS a2,
               txn AS t2, account AS a3,
               txn AS t3, account AS a4, txn AS t4, account AS a5
  WHERE a1.account_id = t1.acc_from AND a2.account_id = t1.acc_to
    AND a2.account_id = t2.acc_from AND a3.account_id = t2.acc_to
    AND a2.account_id = t3.acc_from AND a4.account_id = t3.acc_to
    AND a4.account_id = t4.acc_from AND a5.account_id = t4.acc_to
    AND a1.bank_id = 5
    AND t1.amount > 50000 AND t2.amount > 50000
    AND t3.amount > 50000 AND t4.amount > 50000;
  ```

#### Category E: Selectivity Variants (4 queries)

2-hop chain from a fixed bank, varying `t1.amount > X_K` threshold. Thresholds picked at data-gen time to achieve the target pass rate against the amount distribution.

- **`aml_sel_low.sql`** — ~80% pass: `... AND a1.bank_id = 5 AND t1.amount > X_80;`
- **`aml_sel_med.sql`** — ~60% pass: `... AND t1.amount > X_60;`
- **`aml_sel_high.sql`** — ~40% pass: `... AND t1.amount > X_40;`
- **`aml_sel_vhigh.sql`** — ~20% pass: `... AND t1.amount > X_20;`

(All share the same 2-hop join structure as `aml_2hop.sql`.)

### Data Pipeline

1. **Download** `HI-Small_Trans.csv` / `HI-Medium_Trans.csv` from the [Kaggle mirror](https://www.kaggle.com/datasets/ealtman2019/ibm-transactions-for-anti-money-laundering-aml) or IBM Box.
2. **Convert** (`scripts/convert_ibm_aml_to_nebuladb.py`):
   - Parse IBM CSV; synthesize accounts from union of `(From Bank, Account)` and `(To Bank, Account.1)`
   - Remap hex account IDs to contiguous [1, N]
   - Build integer codes for `Payment Currency`, `Payment Format`
   - Convert `Timestamp` → epoch days
   - Scale `Amount Paid × 100` (cents as int32)
   - Validate all values within [−1,073,741,820, 1,073,741,820]
   - Append sentinel row (-10000 for all columns)
   - Output comma-delimited CSV with header row
   - Print stats: accounts per bank (top 20), amount distribution percentiles (to pick `X_K` selectivity thresholds), suggested `bank_id` anchor values

### Files to Create

- `scripts/convert_ibm_aml_to_nebuladb.py` — conversion script
- `input/queries/aml_1hop.sql` … `aml_5hop.sql` — chain queries (5 files)
- `input/queries/aml_fanout.sql` — fan-out / star
- `input/queries/aml_fanin.sql` — fan-in
- `input/queries/aml_tree.sql` — tree / gather-scatter
- `input/queries/aml_sel_low.sql` … `aml_sel_vhigh.sql` — selectivity variants (4 files)
- `input/plaintext/ibm_aml_hi_small/` and `ibm_aml_hi_medium/` — output directories (account.csv, txn.csv)

---

## Implementation Order

1. **Verify W1** at 10M edge scale (quick — just run existing generator)
2. **Build W3 (SNAP)** — simpler pipeline (just download + convert, no external tool deps)
3. **Build W4 (IBM AML-Data)** — download + convert only; also pairs with W1 in E1 for cross-dataset validation
4. **Build W2 (LDBC)** — requires LDBC datagen setup (Java/Spark dependency)

### Verification for Each Workload
- [ ] Generated CSVs parse correctly (header, correct column count, all values int32)
- [ ] All values within [-1,073,741,820, 1,073,741,820]
- [ ] Sentinel row present at end of each CSV
- [ ] Foreign keys valid (edge src/dst IDs exist in node table)
- [ ] NebulaDB can load and run a 1-hop query against the data without errors
- [ ] Output matches SQLite baseline for the same query
