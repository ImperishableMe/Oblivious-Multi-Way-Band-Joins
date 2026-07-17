# OSort share of Full MWJ (no-filter) runtime

Question: what fraction of a full multi-way oblivious join (`sgx_app`, WK
algorithm, no filter) is spent inside the oblivious sorts (OSorts) versus the
rest of the join work?

## Method

Every oblivious sort in the MWJ pipeline funnels through
`Table::shuffle_merge_sort()` (`app/data_structures/table.cpp`), and the four
join phases run strictly sequentially. A process-wide accumulator at that
funnel point records each sort's wall-clock time and count;
`ObliviousJoin::Execute` snapshots it between phases and prints a
`SORT_TIMING` line alongside the existing `PHASE_TIMING` breakdown:

```
SORT_TIMING: Bottom-Up=<s>/<n> Top-Down=<s>/<n> Distribute-Expand=<s>/<n> Align-Concat=<s>/<n> Total=<s>/<n> Fraction=<sort/total>
```

Runs: `OBL_MWJ_SORT_THREADS=64 ./sgx_app input/queries/banking_2hop.sql <dataset> out.csv --no-filter`
(the E1/E3 convention). One measured run per dataset; banking_200k regenerated
with `scripts/generate_banking_scaled.py 200000 ... --seed 42`.

Cross-checks: banking_1M output = 23,264,179 rows and total = 593.4 s, matching
the E3-recorded no-filter run (595.0 s) — instrumentation overhead is
negligible. The Align-Concat sort delta matches the pre-existing
`ALIGN_CONCAT_SORTS` counter to within 10 µs.

## Sort counts (banking_2hop = 5-table chain, 4 join edges)

37 sorts total: 12 in Bottom-Up (3 per join edge), 12 in Top-Down (3 per join
edge), 5 in Distribute-Expand (1 per table), 8 in Align-Concat (2 per
concatenation step). Per join edge, the two multiplicity phases use 3 + 3 = 6
OSorts.

## Result (see summary.csv; raw stdout in *.log)

| dataset | total (s) | OSort (s) | OSort fraction | OSort : rest |
|---|---|---|---|---|
| banking_1k | 2.24 | 2.19 | 97.7% | 42 : 1 |
| banking_200k | 98.3 | 12.4 | 12.7% | 1 : 6.9 |
| banking_1M | 593.4 | 79.9 | 13.5% | 1 : 6.4 |

At realistic scale the OSorts are **~13% of the join**; the fraction is stable
across the 5x jump from 200k to 1M accounts. The tiny banking_1k run is
sort-dominated (97.7%) only because thread-pool/sync overhead of the 64-thread
bitonic sort swamps the microscopic linear passes — it is not representative.

Where the other ~87% goes at scale (banking_1M): Distribute-Expand (263.5 s,
only 1.7 s of it sorting) and Align-Concat (244.8 s, 46.6 s sorting) dominate.
Both phases do linear passes over the *expanded*, output-sized tables
(116.3M intermediate rows vs 13M input rows at 1M accounts), and that linear
work — distribute passes, expansion, alignment, concatenation — dwarfs the
n log^2 n sorts, which mostly run on the smaller pre-expansion tables and
parallelize well across 64 threads. Bottom-Up and Top-Down are ~37% sorting
internally (15.8 s of 42.5 s each) but are small phases overall.
