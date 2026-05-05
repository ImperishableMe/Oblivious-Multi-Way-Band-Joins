/* obliviator_1hop_chained_main.c — Obliviator FK-based 1-hop, chained.
 *
 * Banking 1-hop: account a1 ⋈ txn t ⋈ account a2
 *   WHERE a1.account_id = t.acc_from AND a2.account_id = t.acc_to
 *
 * Sequential chain:
 *   Step 1: account ⋈ txn   on account_id = acc_from
 *   Step 2: account ⋈ inter on account_id = acc_to
 *
 * Both steps are FK-shaped: account is the unique-keyed (table_0=true) side,
 * the txn / intermediate is the multi-keyed (table_0=false) side. The
 * intermediate side has duplicate keys (multiple txns can share an acc_to);
 * the FK kernel handles this correctly *as long as* account stays on
 * table_0=true. (Putting the duplicate-keyed side on table_0=true is what
 * broke the FK multiway driver previously — see docs/obliviator_multiway_design.md:42.)
 *
 * Step 1's output is repacked into an intermediate elem_t per row, keyed by
 * acc_to and carrying (txn_id, a1_id, a1_bal, a1_own, amount, txn_time)
 * in .data. Step 2's input buffer is [original accounts; intermediates].
 * Step 2's output already pairs each intermediate with its matching a2 —
 * no bitonic stitch needed. Final CSV emit walks the matched arrays in
 * lockstep.
 *
 * Final schema matches obliviator_1hop_main.c (the join-sort baseline) for
 * diff-ability:
 *   t.txn_id, a1.account_id, a1.balance, a1.owner_id,
 *   t.acc_to, t.amount, t.txn_time,
 *   a2.account_id, a2.balance, a2.owner_id
 *
 * Caveat — corruption: the FK kernel's parallel aggregation_tree_op2 path
 * propagates only key, not data. Both kernel calls inherit this; ~16% of
 * rows per step will have wrong account .data fields at thread counts > 1.
 * Acc_to is in the txn .data (not corrupted) so the chain remains
 * structurally sound — every row still matches in step 2, no rows dropped.
 * Single-thread is exact.
 *
 * CLI:
 *   ./obliviator_1hop_chained <num_threads> <src.txt> <output.csv>
 */

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "common/elem_t.h"
#include "enclave/threading.h"

extern size_t total_num_threads;

extern int  scalable_oblivious_join_init(int nthreads);
extern void scalable_oblivious_join_free(void);
extern double scalable_oblivious_join_to_array(elem_t *arr,
                                               long long length1,
                                               long long length2,
                                               elem_t **arr_index_out,
                                               long long *result_len,
                                               double *sort_time_out);

static void *start_thread_work(void *arg) { (void)arg; thread_start_work(); return NULL; }
static void die(const char *msg) { fprintf(stderr, "%s\n", msg); exit(2); }

static double now_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/* skip past one comma-separated field, return pointer to char after the comma. */
static const char *skip_field(const char *p) {
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
    return p;
}

/* ----- side-file loader (off-clock) -------------------------------------
 * Same format as obliviator_1hop_main.c reads (produced by
 * convert_banking_1hop.py): two lengths, then table_0=true rows (account),
 * then table_0=false rows (txn).
 */
static elem_t *load_file(const char *path, long long *l1_out, long long *l2_out) {
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); exit(2); }
    long long length1, length2;
    if (fscanf(fp, "%lld %lld\n", &length1, &length2) != 2) {
        fprintf(stderr, "%s: bad header\n", path); exit(2);
    }
    long long total = length1 + length2;
    elem_t *arr = calloc((size_t)total, sizeof(*arr));
    if (!arr) { fprintf(stderr, "%s: calloc(%lld elem_t) failed\n", path, total); exit(2); }
    for (long long i = 0; i < length1; i++) {
        if (fscanf(fp, "%lld %[^\n]\n", &arr[i].key, arr[i].data) != 2) {
            fprintf(stderr, "%s: parse error at account row %lld\n", path, i); exit(2);
        }
        arr[i].data[DATA_LENGTH - 1] = '\0';
        arr[i].table_0 = true;
    }
    for (long long i = length1; i < total; i++) {
        if (fscanf(fp, "%lld %[^\n]\n", &arr[i].key, arr[i].data) != 2) {
            fprintf(stderr, "%s: parse error at txn row %lld\n", path, i - length1); exit(2);
        }
        arr[i].data[DATA_LENGTH - 1] = '\0';
        arr[i].table_0 = false;
    }
    fclose(fp);
    *l1_out = length1;
    *l2_out = length2;
    return arr;
}

/* ----- intermediate builder (parallel) ----------------------------------
 *
 * After kernel A:
 *   A_acct[i]  — matched a1 row.   .key = a1.account_id (correct);
 *                                  .data = "<a1.balance>,<a1.owner_id>"
 *                                  (may be wrong on parallel-bug rows).
 *   A_txn[i]   — matched txn row.  .key = a1.account_id (= acc_from, correct);
 *                                  .data = "<txn_id>,<acc_to>,<amount>,<txn_time>"
 *                                  (always correct — kernel doesn't write
 *                                  to txn-side .data).
 *
 * Build intermediate[i] directly into the step-2 input buffer at offset
 * `len1_acct` (i.e. after the account block):
 *   .key      = acc_to  (parsed from A_txn[i].data — correct, since txn
 *                        .data is preserved by the kernel)
 *   .table_0  = false   (multi-keyed FK side for step 2)
 *   .data     = "<txn_id>,<a1_id>,<a1_bal>,<a1_own>,<amount>,<txn_time>"
 *               6 fields, max ≈ 47 B → fits DATA_LENGTH=64.
 */
struct inter_args {
    long long lo, hi;
    elem_t *A_acct;
    elem_t *A_txn;
    elem_t *out;          /* points into step2 buffer at offset len1_acct */
};

static void inter_worker(void *voidargs) {
    struct inter_args *a = (struct inter_args *)voidargs;
    for (long long i = a->lo; i < a->hi; i++) {
        /* Parse txn fields: A_txn[i].data = "txn_id,acc_to,amount,txn_time" */
        const char *d = a->A_txn[i].data;
        long long txn_id = strtoll(d, NULL, 10);
        const char *p1 = skip_field(d);            /* "acc_to,amount,txn_time" */
        long long acc_to = strtoll(p1, NULL, 10);
        const char *p2 = skip_field(p1);           /* "amount,txn_time" — pass through verbatim */

        a->out[i].key = acc_to;
        a->out[i].table_0 = false;
        int w = snprintf(a->out[i].data, DATA_LENGTH, "%lld,%lld,%s,%s",
                         txn_id,
                         a->A_acct[i].key,         /* a1.account_id (correct) */
                         a->A_acct[i].data,        /* "a1_bal,a1_own" (may be wrong) */
                         p2);                      /* "amount,txn_time" */
        if (w < 0 || w >= DATA_LENGTH) {
            fprintf(stderr, "inter row %lld: data overflow (need %d, have %d)\n",
                    i, w, DATA_LENGTH);
            exit(3);
        }
    }
}

/* ----- parallel emit ----------------------------------------------------
 *
 * After kernel B:
 *   inter[i]   — matched intermediate (table_0=false in step 2 output).
 *                .key  = acc_to (correct).
 *                .data = "txn_id,a1_id,a1_bal,a1_own,amount,txn_time"
 *                        — 6 comma-separated int32 fields.
 *   B_acct[i]  — matched a2 (table_0=true). .key = a2.account_id;
 *                .data = "a2_balance,a2_owner_id".
 *
 * Final CSV row format (matches join-sort baseline):
 *   t.txn_id, a1.account_id, a1.balance, a1.owner_id,
 *   t.acc_to, t.amount, t.txn_time,
 *   a2.account_id, a2.balance, a2.owner_id
 *
 * Field layout to splice:
 *   prefix (4 fields from inter.data) = "txn_id,a1_id,a1_bal,a1_own"
 *   <inter.key> = acc_to
 *   suffix (2 fields from inter.data) = "amount,txn_time"
 *   <B_acct.key> = a2.account_id
 *   <B_acct.data>= "a2_balance,a2_owner_id"
 */
struct emit_args {
    long long lo, hi;
    elem_t *inter;
    elem_t *B_acct;
    char *buf;
    size_t buf_cap;
    size_t bytes_used;
};

static void emit_worker(void *voidargs) {
    struct emit_args *a = (struct emit_args *)voidargs;
    char *p = a->buf;
    for (long long i = a->lo; i < a->hi; i++) {
        const char *d = a->inter[i].data;
        /* Walk past the first 4 commas to split [prefix | suffix]. */
        const char *split = d;
        for (int j = 0; j < 4; j++) {
            split = strchr(split, ',');
            if (!split) { fprintf(stderr, "emit row %lld: malformed inter.data: %s\n", i, d); exit(3); }
            split++;
        }
        /* split points to first char of "amount,txn_time".
         * (split - 1) is the comma after a1_own. prefix length = (split-1) - d. */
        p += sprintf(p, "%.*s,%lld,%s,%lld,%s\n",
                     (int)(split - 1 - d), d,           /* prefix: txn_id,a1_id,a1_bal,a1_own */
                     a->inter[i].key,                   /* acc_to */
                     split,                             /* suffix: amount,txn_time */
                     a->B_acct[i].key,                  /* a2.account_id */
                     a->B_acct[i].data);                /* a2_balance,a2_owner_id */
    }
    a->bytes_used = (size_t)(p - a->buf);
}

/* ----- generic chunked-parallel dispatch -------------------------------- */
static void dispatch(void (*worker)(void *), void *args_base, size_t arg_stride,
                     size_t num_threads) {
    struct thread_work *works = (num_threads > 1)
        ? calloc(num_threads - 1, sizeof(*works)) : NULL;
    if (num_threads > 1 && !works) die("dispatch: thread_work alloc failed");
    char *base = (char *)args_base;
    for (size_t t = 0; t < num_threads - 1; t++) {
        works[t].type = THREAD_WORK_SINGLE;
        works[t].single.func = worker;
        works[t].single.arg = base + t * arg_stride;
        thread_work_push(&works[t]);
    }
    worker(base + (num_threads - 1) * arg_stride);
    for (size_t t = 0; t + 1 < num_threads; t++) thread_wait(&works[t]);
    free(works);
}

/* ----- main ------------------------------------------------------------- */
int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <num_threads> <src.txt> <output.csv>\n", argv[0]);
        return 1;
    }
    size_t num_threads = (size_t)atoi(argv[1]);
    const char *src_path = argv[2];
    const char *out_csv  = argv[3];
    total_num_threads = num_threads;

    if (scalable_oblivious_join_init((int)num_threads) != 0) die("join init failed");
    thread_system_init();

    pthread_t *threads = NULL;
    if (num_threads > 1) {
        threads = malloc((num_threads - 1) * sizeof(pthread_t));
        if (!threads) die("thread array alloc failed");
        for (size_t i = 0; i < num_threads - 1; i++) {
            int rc = pthread_create(&threads[i], NULL, start_thread_work, NULL);
            if (rc != 0) { fprintf(stderr, "pthread_create: %s\n", strerror(rc)); return 1; }
        }
        printf("Threads: %zu  (1 main + %zu workers)\n", num_threads, num_threads - 1);
    } else {
        printf("Threads: 1\n");
    }

    /* ============================================================
     * Step 1:  account ⋈ txn  on  account_id = acc_from   (from src.txt)
     * ============================================================ */
    double load_t_s = now_sec();
    long long len1 = 0, len2 = 0;
    elem_t *arr = load_file(src_path, &len1, &len2);
    double load_t = now_sec() - load_t_s;
    printf("\n--- Step 1 (acc_from) ---\n");
    printf("loaded src: %lld accts + %lld txns  (%.3f s, off-clock)\n",
           len1, len2, load_t);

    /* Pre-allocate the step-2 input buffer (size = len1 + A_len; A_len ≤ len2).
     * We'll fill [0..len1) with the original accounts and [len1..len1+A_len)
     * with the intermediate built from kernel A's output. */
    long long step2_max = len1 + len2;
    elem_t *arr2 = malloc((size_t)step2_max * sizeof(*arr2));
    if (!arr2) die("step2 buffer alloc failed");

    /* Save the account block for step 2 BEFORE kernel A clobbers arr.
     * Off-clock — same status as the input CSV read. */
    double save_s = now_sec();
    memcpy(arr2, arr, (size_t)len1 * sizeof(*arr));
    double save_t = now_sec() - save_s;
    printf("saved %lld accounts for step 2 (%.6f s, off-clock)\n", len1, save_t);

    /* Run kernel A. */
    elem_t *A_acct = NULL;
    long long A_len = 0;
    double A_sort_t = 0.0;
    double A_join_t = scalable_oblivious_join_to_array(
        arr, len1, len2, &A_acct, &A_len, &A_sort_t);
    printf("kernel A: result=%lld  sort=%.6f  total=%.6f\n",
           A_len, A_sort_t, A_join_t);

    /* Build intermediates directly into arr2[len1..len1+A_len). */
    double inter_s = now_sec();
    struct inter_args *iargs = calloc(num_threads, sizeof(*iargs));
    if (!iargs) die("inter args calloc failed");
    long long rows_per   = A_len / (long long)num_threads;
    long long rows_extra = A_len % (long long)num_threads;
    long long cursor = 0;
    for (size_t t = 0; t < num_threads; t++) {
        long long chunk = rows_per + (t < (size_t)rows_extra ? 1 : 0);
        iargs[t].lo = cursor;
        iargs[t].hi = cursor + chunk;
        iargs[t].A_acct = A_acct;
        iargs[t].A_txn = arr;             /* matched txns sit in arr[0..A_len) */
        iargs[t].out = arr2 + len1;       /* intermediates land at offset len1 */
        cursor += chunk;
    }
    dispatch(inter_worker, iargs, sizeof(*iargs), num_threads);
    double inter_t = now_sec() - inter_s;
    printf("build intermediate: %.6f s\n", inter_t);
    free(iargs);
    free(A_acct);
    free(arr);

    /* ============================================================
     * Step 2:  account ⋈ intermediate  on  account_id = acc_to
     * arr2 layout: [accounts (1M, table_0=true) | intermediates (5M, table_0=false)]
     * ============================================================ */
    printf("\n--- Step 2 (acc_to) ---\n");
    printf("step2 input: %lld accts + %lld inters  = %lld total\n",
           len1, A_len, len1 + A_len);

    elem_t *B_acct = NULL;
    long long B_len = 0;
    double B_sort_t = 0.0;
    double B_join_t = scalable_oblivious_join_to_array(
        arr2, len1, A_len, &B_acct, &B_len, &B_sort_t);
    printf("kernel B: result=%lld  sort=%.6f  total=%.6f\n",
           B_len, B_sort_t, B_join_t);

    /* arr2[0..B_len) now holds matched intermediates;
     * B_acct[0..B_len) holds matching a2 accounts. They're paired by index. */

    /* ============================================================
     * Emit (parallel format → off-clock disk write)
     * ============================================================ */
    static const char header[] =
        "t.txn_id,"
        "a1.account_id,a1.balance,a1.owner_id,"
        "t.acc_to,t.amount,t.txn_time,"
        "a2.account_id,a2.balance,a2.owner_id\n";
    /* Per-row upper bound: 8 + 1 + 8 + 1 + 8 + 1 + 8 + 1 + 8 + 1 + 8 + 1 + 8
     * + 1 + 8 + 1 + 8 + 1 + 8 + 1 = ≤ ~95 B; pad to 112. */
    const size_t row_cap = 112;

    double emit_s = now_sec();
    struct emit_args *eargs = calloc(num_threads, sizeof(*eargs));
    if (!eargs) die("emit args calloc failed");
    long long erows_per   = B_len / (long long)num_threads;
    long long erows_extra = B_len % (long long)num_threads;
    long long ecursor = 0;
    for (size_t t = 0; t < num_threads; t++) {
        long long chunk = erows_per + (t < (size_t)erows_extra ? 1 : 0);
        eargs[t].lo = ecursor;
        eargs[t].hi = ecursor + chunk;
        eargs[t].inter = arr2;
        eargs[t].B_acct = B_acct;
        eargs[t].buf_cap = (size_t)chunk * row_cap;
        eargs[t].buf = malloc(eargs[t].buf_cap > 0 ? eargs[t].buf_cap : 1);
        if (!eargs[t].buf) die("emit per-thread buf alloc failed");
        ecursor += chunk;
    }
    dispatch(emit_worker, eargs, sizeof(*eargs), num_threads);
    double emit_t = now_sec() - emit_s;

    /* Off-clock: serial disk fwrite. */
    double write_s = now_sec();
    FILE *out = fopen(out_csv, "w");
    if (!out) { perror(out_csv); exit(2); }
    fwrite(header, 1, sizeof(header) - 1, out);
    for (size_t t = 0; t < num_threads; t++) {
        fwrite(eargs[t].buf, 1, eargs[t].bytes_used, out);
        free(eargs[t].buf);
    }
    fclose(out);
    double write_t = now_sec() - write_s;

    printf("emit  (%lld rows, %zu-way parallel format): %.6f s\n",
           B_len, num_threads, emit_t);
    printf("write (%lld rows, serial disk fwrite, off-clock): %.6f s\n",
           B_len, write_t);

    free(eargs);
    free(arr2);
    free(B_acct);

    /* ============================================================
     * Summary
     * ============================================================ */
    /* OBLIVIOUS WORK includes build_intermediate. The repack is a deterministic
     * O(N) pass with thread access patterns that depend only on the row
     * index — same obliviousness profile as pack/emit. It's between the two
     * kernel calls and required to feed step 2, so it counts.
     *
     * The save-accounts memcpy stays excluded — it's a pre-kernel input copy,
     * conceptually the same as a CSV load. */
    double oblivious_total      = A_join_t + inter_t + B_join_t;
    double oblivious_plus_emit  = oblivious_total + emit_t;
    double excluded_total       = save_t;

    printf("\n=== 1-Hop Summary (chained) ===\n");
    printf("  step 1 (sort)          : %.6f s\n", A_sort_t);
    printf("  step 1 (total)         : %.6f s   [includes sort]\n", A_join_t);
    printf("  build intermediate     : %.6f s\n", inter_t);
    printf("  step 2 (sort)          : %.6f s\n", B_sort_t);
    printf("  step 2 (total)         : %.6f s   [includes sort]\n", B_join_t);
    printf("  emit (in-memory)       : %.6f s\n", emit_t);
    printf("  ------------------------------------\n");
    printf("  OBLIVIOUS WORK         : %.6f s   [step 1 + build inter + step 2]\n", oblivious_total);
    printf("  OBLIVIOUS WORK + EMIT  : %.6f s   [...plus in-memory CSV format]\n", oblivious_plus_emit);
    printf("  excluded               : %.6f s   [save accts memcpy]\n",
           excluded_total);
    printf("  final rows             : %lld\n", B_len);
    printf("  output                 : %s\n", out_csv);
    printf("  off-clock CSV I/O      : load %.3f, write %.3f = %.3f s total\n",
           load_t, write_t, load_t + write_t);

    if (num_threads > 1 && threads) {
        thread_release_all();
        for (size_t i = 0; i < num_threads - 1; i++) pthread_join(threads[i], NULL);
        free(threads);
    }
    thread_system_cleanup();
    return 0;
}
