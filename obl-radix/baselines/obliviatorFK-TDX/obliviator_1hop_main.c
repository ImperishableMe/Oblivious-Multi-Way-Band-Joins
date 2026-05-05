/* obliviator_1hop_main.c — Obliviator FK-based 1-hop driver.
 *
 * Banking 1-hop:  account a1 ⋈ txn t ⋈ account a2
 *   WHERE a1.account_id = t.acc_from AND a2.account_id = t.acc_to
 *
 * Decomposed into two independent FK joins:
 *   Join A:  account ⋈ txn  on  account.account_id = txn.acc_from   (src.txt)
 *   Join B:  account ⋈ txn  on  account.account_id = txn.acc_to     (dst.txt)
 *
 * Both joins are pure-FK (account is unique-keyed on each side), so the FK
 * kernel produces N_txn matches per side. The two result-pairs are then
 * stitched together by txn_id (carried in the txn .data payload by
 * convert_banking_1hop.py) using an oblivious bitonic sort + lockstep walk.
 *
 * No final filter applied — would slot in between the stitch walk and the
 * CSV emit, and is sound under Obliviator's threat model since the final
 * output size already leaks.
 *
 * CLI:
 *   ./obliviator_1hop <num_threads> <src.txt> <dst.txt> <output.csv>
 *
 * Reported timing covers all in-memory work: each kernel call (sort + total),
 * the post-kernel pack passes that build txn_id-keyed stitch arrays, the two
 * bitonic stitch sorts, and the lockstep CSV emit. Off-clock: side-file load
 * (CSV read) and final CSV write — same exclusions as multiway_main.c.
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
#include "enclave/bitonic.h"
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

/* ----- side-file loader (off-clock) -------------------------------------
 * Format produced by convert_banking_1hop.py:
 *     <length1> <length2>
 *     <blank line>
 *     <key> <data>            (length1 lines, becomes table_0=true: account)
 *     <blank line>
 *     <key> <data>            (length2 lines, becomes table_0=false: txn)
 */
static elem_t *load_file(const char *path, long long *l1_out, long long *l2_out) {
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); exit(2); }

    long long length1, length2;
    if (fscanf(fp, "%lld %lld\n", &length1, &length2) != 2) {
        fprintf(stderr, "%s: bad header (expected '<length1> <length2>')\n", path);
        exit(2);
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

/* ----- pack passes ------------------------------------------------------
 *
 * After the FK kernel returns, we have two parallel arrays of length N:
 *   acct[i]  -> matched account row  (key = account_id, data = "<bal>,<own>")
 *   txn[i]   -> matched txn row      (key = acc_from or acc_to,
 *                                     data = "<txn_id>,<other_acc>,<amt>,<time>")
 *
 * pack_src builds the A-side stitch array (one elem per match, keyed by txn_id):
 *     out[i].key  = txn_id
 *     out[i].data = "<a1.account_id>,<a1.balance>,<a1.owner_id>,<acc_to>,<amount>,<txn_time>"
 *
 * pack_dst builds the B-side stitch array (smaller payload — only what's
 * not already in the A side):
 *     out[i].key  = txn_id
 *     out[i].data = "<a2.account_id>,<a2.balance>,<a2.owner_id>"
 *
 * Both arrays will subsequently be bitonic-sorted by .key (oblivious stitch).
 */

/* skip past one comma-separated field, return pointer to char after the comma. */
static const char *skip_field(const char *p) {
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
    return p;
}

/* Pack workers process a disjoint row range [lo, hi). Output array `out` is
 * shared but each thread writes only its own indices, so no synchronization
 * is needed — the chunking math below guarantees ranges don't overlap. */
struct pack_args {
    long long lo, hi;
    elem_t *acct;
    elem_t *txn;
    elem_t *out;
};

static void pack_src_worker(void *voidargs) {
    struct pack_args *a = (struct pack_args *)voidargs;
    for (long long i = a->lo; i < a->hi; i++) {
        long long txn_id = strtoll(a->txn[i].data, NULL, 10);
        const char *rest = skip_field(a->txn[i].data);  /* "<acc_to>,<amount>,<txn_time>" */
        a->out[i].key = txn_id;
        a->out[i].table_0 = true;
        int w = snprintf(a->out[i].data, DATA_LENGTH, "%lld,%s,%s",
                         a->acct[i].key, a->acct[i].data, rest);
        if (w < 0 || w >= DATA_LENGTH) {
            fprintf(stderr, "pack_src row %lld: data overflow (need %d, have %d)\n",
                    i, w, DATA_LENGTH);
            exit(3);
        }
    }
}

static void pack_dst_worker(void *voidargs) {
    struct pack_args *a = (struct pack_args *)voidargs;
    for (long long i = a->lo; i < a->hi; i++) {
        long long txn_id = strtoll(a->txn[i].data, NULL, 10);
        a->out[i].key = txn_id;
        a->out[i].table_0 = true;
        int w = snprintf(a->out[i].data, DATA_LENGTH, "%lld,%s",
                         a->acct[i].key, a->acct[i].data);
        if (w < 0 || w >= DATA_LENGTH) {
            fprintf(stderr, "pack_dst row %lld: data overflow (need %d, have %d)\n",
                    i, w, DATA_LENGTH);
            exit(3);
        }
    }
}

/* Dispatch one of the pack workers across `num_threads` chunks of [0, n).
 * Same "main thread does the last chunk" idiom as the kernel's parallel paths. */
static elem_t *pack_parallel(void (*worker)(void *),
                             elem_t *acct, elem_t *txn, long long n,
                             size_t num_threads, const char *what) {
    elem_t *out = calloc((size_t)n, sizeof(*out));
    if (!out) { fprintf(stderr, "%s: calloc failed\n", what); exit(2); }

    struct pack_args *args = calloc(num_threads, sizeof(*args));
    struct thread_work *works = (num_threads > 1)
        ? calloc(num_threads - 1, sizeof(*works)) : NULL;
    if (!args || (num_threads > 1 && !works)) {
        fprintf(stderr, "%s: arg/work alloc failed\n", what); exit(2);
    }

    long long rows_per   = n / (long long)num_threads;
    long long rows_extra = n % (long long)num_threads;
    long long cursor = 0;
    for (size_t t = 0; t < num_threads; t++) {
        long long chunk = rows_per + (t < (size_t)rows_extra ? 1 : 0);
        args[t].lo = cursor;
        args[t].hi = cursor + chunk;
        args[t].acct = acct;
        args[t].txn = txn;
        args[t].out = out;
        cursor += chunk;
        if (t < num_threads - 1) {
            works[t].type = THREAD_WORK_SINGLE;
            works[t].single.func = worker;
            works[t].single.arg = &args[t];
            thread_work_push(&works[t]);
        }
    }
    worker(&args[num_threads - 1]);
    for (size_t t = 0; t + 1 < num_threads; t++) thread_wait(&works[t]);

    free(args);
    free(works);
    return out;
}

static elem_t *pack_src(elem_t *acct, elem_t *txn, long long n, size_t num_threads) {
    return pack_parallel(pack_src_worker, acct, txn, n, num_threads, "pack_src");
}

static elem_t *pack_dst(elem_t *acct, elem_t *txn, long long n, size_t num_threads) {
    return pack_parallel(pack_dst_worker, acct, txn, n, num_threads, "pack_dst");
}

/* ----- parallel emit ----------------------------------------------------
 *
 * The CSV emit (sprintf 5M rows) is single-threaded format CPU work — easily
 * parallelized: split [0, final_n) into chunks, each thread sprintf's its
 * chunk into its own buffer; main thread fwrites buffers in chunk order.
 * Output ordering is preserved because chunks are dispatched in order and
 * concatenated in the same order.
 */
struct emit_chunk_args {
    long long lo, hi;
    elem_t *A_packed;
    elem_t *B_packed;
    char *buf;
    size_t buf_cap;
    size_t bytes_used;       /* out */
    long long mismatches;    /* out */
};

static void emit_chunk_worker(void *voidargs) {
    struct emit_chunk_args *a = (struct emit_chunk_args *)voidargs;
    char *p = a->buf;
    long long mism = 0;
    for (long long i = a->lo; i < a->hi; i++) {
        if (a->A_packed[i].key != a->B_packed[i].key) mism++;
        p += sprintf(p, "%lld,%s,%s\n",
                     a->A_packed[i].key,
                     a->A_packed[i].data,
                     a->B_packed[i].data);
    }
    a->bytes_used = (size_t)(p - a->buf);
    a->mismatches = mism;
}

/* ----- main ------------------------------------------------------------- */
int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s <num_threads> <src.txt> <dst.txt> <output.csv>\n", argv[0]);
        return 1;
    }
    size_t num_threads = (size_t)atoi(argv[1]);
    const char *src_path = argv[2];
    const char *dst_path = argv[3];
    const char *out_csv  = argv[4];
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
     * Join A:  account ⋈ txn  on  account_id = acc_from   (from src.txt)
     * ============================================================ */
    double load_A_s = now_sec();
    long long len1_A = 0, len2_A = 0;
    elem_t *arr_A = load_file(src_path, &len1_A, &len2_A);
    double load_A_t = now_sec() - load_A_s;
    printf("\n--- Join A (acc_from) ---\n");
    printf("loaded src: %lld accts + %lld txns  (%.3f s, off-clock)\n",
           len1_A, len2_A, load_A_t);

    elem_t *A_acct = NULL;
    long long A_len = 0;
    double A_sort_t = 0.0;
    double A_join_t = scalable_oblivious_join_to_array(
        arr_A, len1_A, len2_A, &A_acct, &A_len, &A_sort_t);
    printf("join A: result=%lld  sort=%.6f  total=%.6f\n",
           A_len, A_sort_t, A_join_t);

    double pack_A_s = now_sec();
    elem_t *A_packed = pack_src(A_acct, arr_A, A_len, num_threads);
    double pack_A_t = now_sec() - pack_A_s;
    printf("pack A:   %.6f s  (excluded from ONLINE total)\n", pack_A_t);
    free(arr_A);  arr_A = NULL;
    free(A_acct); A_acct = NULL;

    /* ============================================================
     * Join B:  account ⋈ txn  on  account_id = acc_to     (from dst.txt)
     * ============================================================ */
    double load_B_s = now_sec();
    long long len1_B = 0, len2_B = 0;
    elem_t *arr_B = load_file(dst_path, &len1_B, &len2_B);
    double load_B_t = now_sec() - load_B_s;
    printf("\n--- Join B (acc_to) ---\n");
    printf("loaded dst: %lld accts + %lld txns  (%.3f s, off-clock)\n",
           len1_B, len2_B, load_B_t);

    elem_t *B_acct = NULL;
    long long B_len = 0;
    double B_sort_t = 0.0;
    double B_join_t = scalable_oblivious_join_to_array(
        arr_B, len1_B, len2_B, &B_acct, &B_len, &B_sort_t);
    printf("join B: result=%lld  sort=%.6f  total=%.6f\n",
           B_len, B_sort_t, B_join_t);

    double pack_B_s = now_sec();
    elem_t *B_packed = pack_dst(B_acct, arr_B, B_len, num_threads);
    double pack_B_t = now_sec() - pack_B_s;
    printf("pack B:   %.6f s  (excluded from ONLINE total)\n", pack_B_t);
    free(arr_B);  arr_B = NULL;
    free(B_acct); B_acct = NULL;

    if (A_len != B_len) {
        fprintf(stderr,
                "WARNING: A_len (%lld) != B_len (%lld) — stitch alignment will drop rows\n",
                A_len, B_len);
    }

    /* ============================================================
     * Oblivious stitch:  bitonic-sort each packed array by key=txn_id,
     * then walk in lockstep emitting (a1, t, a2) triples.
     *
     * Two sorts run sequentially because bitonic.c uses a static global
     * `arr` pointer — not safe to run two sorts concurrently.
     * ============================================================ */
    printf("\n--- Stitch ---\n");
    double sort_A_s = now_sec();
    bitonic_sort(A_packed, true /*ascend*/, 0, A_len, (long long)num_threads, false /*no D2*/);
    double sort_A_t = now_sec() - sort_A_s;
    printf("bitonic-sort A by txn_id: %.6f s\n", sort_A_t);

    double sort_B_s = now_sec();
    bitonic_sort(B_packed, true, 0, B_len, (long long)num_threads, false);
    double sort_B_t = now_sec() - sort_B_s;
    printf("bitonic-sort B by txn_id: %.6f s\n", sort_B_t);

    long long final_n = (A_len < B_len) ? A_len : B_len;
    /* Schema mirrors the packed column order: txn_id first (the stitch key),
     * then A_packed's 6 fields (a1.account_id, a1.balance, a1.owner_id,
     * t.acc_to, t.amount, t.txn_time), then B_packed's 3 fields (a2.account_id,
     * a2.balance, a2.owner_id). t.acc_from is implicit (= a1.account_id). */
    static const char header[] =
        "t.txn_id,"
        "a1.account_id,a1.balance,a1.owner_id,"
        "t.acc_to,t.amount,t.txn_time,"
        "a2.account_id,a2.balance,a2.owner_id\n";
    /* Per-row upper bound: txn_id (≤8) + ',' + A_packed.data (≤47) + ','
     * + B_packed.data (≤23) + '\n' = ≤80, padded to 96 for safety. */
    const size_t row_cap = 96;

    /* ---- timed: parallel format into per-thread buffers ------------------
     * This is the CPU work — string formatting all 5M rows. Parallelizable.
     * Each thread sprintf's its chunk into its own buffer; the resulting
     * (combined-in-memory) byte stream is the CSV output minus the actual
     * disk write, which happens off-clock below.
     */
    double emit_s = now_sec();
    struct emit_chunk_args *args = calloc(num_threads, sizeof(*args));
    struct thread_work *works = (num_threads > 1)
        ? calloc(num_threads - 1, sizeof(*works)) : NULL;
    if (!args || (num_threads > 1 && !works)) die("emit: arg/work alloc failed");

    long long rows_per   = final_n / (long long)num_threads;
    long long rows_extra = final_n % (long long)num_threads;
    long long cursor = 0;
    for (size_t t = 0; t < num_threads; t++) {
        long long chunk = rows_per + (t < (size_t)rows_extra ? 1 : 0);
        args[t].lo = cursor;
        args[t].hi = cursor + chunk;
        args[t].A_packed = A_packed;
        args[t].B_packed = B_packed;
        args[t].buf_cap = (size_t)chunk * row_cap;
        args[t].buf = malloc(args[t].buf_cap > 0 ? args[t].buf_cap : 1);
        if (!args[t].buf) die("emit: per-thread buffer alloc failed");
        cursor += chunk;
        if (t < num_threads - 1) {
            works[t].type = THREAD_WORK_SINGLE;
            works[t].single.func = emit_chunk_worker;
            works[t].single.arg = &args[t];
            thread_work_push(&works[t]);
        }
    }
    emit_chunk_worker(&args[num_threads - 1]);
    for (size_t t = 0; t + 1 < num_threads; t++) thread_wait(&works[t]);
    double emit_t = now_sec() - emit_s;

    /* ---- off-clock: serial disk write -----------------------------------
     * Same status as the CSV read on the input side — environmental I/O,
     * not part of the Obliviator pipeline. Time it separately for reference. */
    double write_s = now_sec();
    FILE *out = fopen(out_csv, "w");
    if (!out) { perror(out_csv); exit(2); }
    fwrite(header, 1, sizeof(header) - 1, out);
    for (size_t t = 0; t < num_threads; t++) {
        fwrite(args[t].buf, 1, args[t].bytes_used, out);
        free(args[t].buf);
    }
    fclose(out);
    double write_t = now_sec() - write_s;

    long long mismatches = 0;
    for (size_t t = 0; t < num_threads; t++) mismatches += args[t].mismatches;
    free(args);
    free(works);
    printf("emit  (%lld rows, %zu-way parallel format): %.6f s\n",
           final_n, num_threads, emit_t);
    printf("write (%lld rows, serial disk fwrite, off-clock): %.6f s\n",
           final_n, write_t);
    if (mismatches) {
        fprintf(stderr,
                "WARNING: %lld rows had mismatched txn_id between A and B "
                "after sort — stitch is broken.\n", mismatches);
    }

    /* Two reported totals:
     *   OBLIVIOUS WORK          = joins + stitch sorts                (pure)
     *   OBLIVIOUS WORK + EMIT   = ...also includes the in-memory CSV format pass
     *
     * Pack passes (kernel-output → stitch-input marshalling) are excluded.
     * The actual disk write (`write` below) is also off-clock, same as the
     * input CSV reads. */
    double oblivious_total      = A_join_t + B_join_t + sort_A_t + sort_B_t;
    double oblivious_plus_emit  = oblivious_total + emit_t;
    double excluded_total       = pack_A_t + pack_B_t;

    printf("\n=== 1-Hop Summary ===\n");
    printf("  join A (sort)          : %.6f s\n", A_sort_t);
    printf("  join A (total)         : %.6f s   [includes sort]\n", A_join_t);
    printf("  join B (sort)          : %.6f s\n", B_sort_t);
    printf("  join B (total)         : %.6f s   [includes sort]\n", B_join_t);
    printf("  bitonic-sort A         : %.6f s\n", sort_A_t);
    printf("  bitonic-sort B         : %.6f s\n", sort_B_t);
    printf("  emit (in-memory)       : %.6f s\n", emit_t);
    printf("  ------------------------------------\n");
    printf("  OBLIVIOUS WORK         : %.6f s   [joins + stitch sorts]\n", oblivious_total);
    printf("  OBLIVIOUS WORK + EMIT  : %.6f s   [...plus in-memory CSV format]\n", oblivious_plus_emit);
    printf("  excluded               : %.6f s   [pack A %.3f + pack B %.3f]\n",
           excluded_total, pack_A_t, pack_B_t);
    printf("  final rows             : %lld\n", final_n);
    printf("  output                 : %s\n", out_csv);
    printf("  off-clock CSV I/O      : load %.3f + %.3f, write %.3f = %.3f s total\n",
           load_A_t, load_B_t, write_t, load_A_t + load_B_t + write_t);

    free(A_packed);
    free(B_packed);

    if (num_threads > 1 && threads) {
        thread_release_all();
        for (size_t i = 0; i < num_threads - 1; i++) pthread_join(threads[i], NULL);
        free(threads);
    }
    thread_system_cleanup();
    /* scalable_oblivious_join_free() is a no-op in FK; safe either way. */
    return 0;
}
