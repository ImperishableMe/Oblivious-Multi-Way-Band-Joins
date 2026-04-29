/* multiway_main.c — Obliviator multi-way join driver.
 *
 * Executes a multi-way join as a sequence of pairwise FK joins by repeatedly
 * calling scalable_oblivious_join_to_array, chaining each step's output into
 * the next step's "left" (table_0=true, index) side in memory — no text
 * round-trip between steps, no intermediate CSV.
 *
 * CLI:
 *     ./multiway_main <num_threads> <plan_file> <output_csv>
 *
 * Plan file format (text; see scripts/convert_banking_multiway.py for a
 * generator). All column indices refer to positions in the comma-separated
 * `.data` payload, which is "L[i].data + ',' + R[i].data" after each step.
 *
 *     num_steps <N>
 *     step 0 side0=base:<path> side1=base:<path> next_key_col=<col|-1>
 *     step 1 side0=prev        side1=base:<path> next_key_col=<col|-1>
 *     ...
 *     step N-1 side0=prev      side1=base:<path> next_key_col=-1
 *     final_filter <col> <eq|gt|ge|lt|le> <int32>    (0+ lines, AND-joined)
 *     schema <col> <name>                             (0+ lines, for CSV header)
 *
 * Side files referenced by "base:<path>" are one-sided elem_t streams:
 *     <num_rows>
 *     <key> <data>
 *     <key> <data>
 *     ...
 *
 * Threat model notes (see docs/experiments.md):
 *   - Intermediate pairwise result sizes leak (accepted for the Obliviator
 *     baseline); that's what scalable_oblivious_join compaction already does.
 *   - Single-table filters are deferred to the FINAL step's output only.
 *     Filtering between steps would leak extra value-dependent information.
 *   - Base CSV read and final CSV write are OUT of the reported timing.
 *     The per-step breakdown below covers in-memory work only.
 */

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
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
extern double scalable_oblivious_join_to_array(elem_t *arr, long long length1, long long length2,
                                               elem_t **arr_out, long long *result_len,
                                               double *sort_time_out);

#define MAX_STEPS    32
#define MAX_FILTERS  16
#define MAX_SCHEMA  128
#define PATH_MAX_LEN 256

struct step {
    int  index;
    int  side0_is_prev;          /* 1 if side0=prev, 0 if side0=base */
    char side0_path[PATH_MAX_LEN];
    char side1_path[PATH_MAX_LEN];
    int  next_key_col;           /* -1 on last step */
};

struct filter {
    int         col;
    char        op[4];           /* eq, gt, ge, lt, le */
    long long   val;
};

struct plan {
    int  num_steps;
    struct step steps[MAX_STEPS];

    int  num_filters;
    struct filter filters[MAX_FILTERS];

    int  num_schema;
    int  schema_col[MAX_SCHEMA];
    char schema_name[MAX_SCHEMA][64];
};

/* -------------------------- threading --------------------------- */
static void *start_thread_work(void *arg) { (void)arg; thread_start_work(); return NULL; }

/* -------------------------- plan parser ------------------------- */
static void die(const char *msg) { fprintf(stderr, "%s\n", msg); exit(2); }

static int parse_plan(const char *path, struct plan *p) {
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); return -1; }

    memset(p, 0, sizeof(*p));

    char line[1024];
    int lineno = 0;
    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *s = line;
        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s) continue;

        if (!strncmp(s, "num_steps", 9)) {
            if (sscanf(s, "num_steps %d", &p->num_steps) != 1) {
                fprintf(stderr, "plan:%d: bad num_steps line\n", lineno); fclose(fp); return -1;
            }
            if (p->num_steps < 1 || p->num_steps > MAX_STEPS) {
                fprintf(stderr, "plan:%d: num_steps out of range\n", lineno); fclose(fp); return -1;
            }
        } else if (!strncmp(s, "step ", 5)) {
            int idx = -1, nkc = -1;
            char side0[300] = {0}, side1[300] = {0};
            if (sscanf(s, "step %d side0=%299s side1=%299s next_key_col=%d",
                       &idx, side0, side1, &nkc) != 4) {
                fprintf(stderr, "plan:%d: bad step line\n", lineno); fclose(fp); return -1;
            }
            if (idx < 0 || idx >= p->num_steps) {
                fprintf(stderr, "plan:%d: step index %d out of range\n", lineno, idx); fclose(fp); return -1;
            }
            struct step *st = &p->steps[idx];
            st->index = idx;
            st->next_key_col = nkc;
            if (!strcmp(side0, "prev")) {
                st->side0_is_prev = 1;
            } else if (!strncmp(side0, "base:", 5)) {
                st->side0_is_prev = 0;
                strncpy(st->side0_path, side0 + 5, PATH_MAX_LEN - 1);
            } else {
                fprintf(stderr, "plan:%d: side0 must be 'prev' or 'base:<path>'\n", lineno); fclose(fp); return -1;
            }
            if (strncmp(side1, "base:", 5)) {
                fprintf(stderr, "plan:%d: side1 must be 'base:<path>'\n", lineno); fclose(fp); return -1;
            }
            strncpy(st->side1_path, side1 + 5, PATH_MAX_LEN - 1);
        } else if (!strncmp(s, "final_filter", 12)) {
            if (p->num_filters >= MAX_FILTERS) {
                fprintf(stderr, "plan:%d: too many filters\n", lineno); fclose(fp); return -1;
            }
            struct filter *f = &p->filters[p->num_filters];
            if (sscanf(s, "final_filter %d %3s %lld", &f->col, f->op, &f->val) != 3) {
                fprintf(stderr, "plan:%d: bad final_filter line\n", lineno); fclose(fp); return -1;
            }
            p->num_filters++;
        } else if (!strncmp(s, "schema ", 7)) {
            if (p->num_schema >= MAX_SCHEMA) {
                fprintf(stderr, "plan:%d: too many schema entries\n", lineno); fclose(fp); return -1;
            }
            int col; char name[64] = {0};
            if (sscanf(s, "schema %d %63s", &col, name) != 2) {
                fprintf(stderr, "plan:%d: bad schema line\n", lineno); fclose(fp); return -1;
            }
            p->schema_col[p->num_schema]  = col;
            strncpy(p->schema_name[p->num_schema], name, 63);
            p->num_schema++;
        } else {
            fprintf(stderr, "plan:%d: unknown directive: %s", lineno, s); fclose(fp); return -1;
        }
    }
    fclose(fp);

    /* sanity check: every step populated */
    for (int i = 0; i < p->num_steps; i++) {
        if (p->steps[i].index != i) {
            fprintf(stderr, "plan: step %d missing\n", i); return -1;
        }
    }
    return 0;
}

/* -------------------------- side-file loader (off-clock) -------- */
static elem_t *load_side_file(const char *path, long long *len, int table_0) {
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); exit(2); }

    long long n;
    if (fscanf(fp, "%lld\n", &n) != 1) {
        fprintf(stderr, "%s: bad header (expected row count)\n", path); exit(2);
    }
    elem_t *arr = calloc((size_t)n, sizeof(*arr));
    if (!arr) { fprintf(stderr, "calloc %lld elem_t failed\n", n); exit(2); }

    for (long long i = 0; i < n; i++) {
        if (fscanf(fp, "%lld %[^\n]\n", &arr[i].key, arr[i].data) != 2) {
            fprintf(stderr, "%s: parse error at row %lld\n", path, i); exit(2);
        }
        arr[i].data[DATA_LENGTH - 1] = '\0';
        arr[i].table_0 = (bool)table_0;
    }
    fclose(fp);
    *len = n;
    return arr;
}

/* -------------------------- column helpers ---------------------- */
/* Find the start of column `col` within `buf` (comma-separated).
 * Returns NULL if the column is beyond the end of the string. */
static const char *nth_column(const char *buf, int col) {
    if (col < 0) return NULL;
    const char *p = buf;
    int cur = 0;
    while (*p && cur < col) {
        if (*p == ',') cur++;
        p++;
    }
    return (cur == col) ? p : NULL;
}

/* Build the next step's side0 (table_0=true) by concatenating
 * L[i].data + "," + R[i].data into a fresh elem_t, and pulling the
 * new .key from column `key_col` of that concatenation.
 *
 * Takes ownership of L and R and frees them.
 */
static elem_t *build_next_side0(elem_t *L, elem_t *R, long long n, int key_col) {
    elem_t *out = calloc((size_t)n, sizeof(*out));
    if (!out) { perror("calloc"); exit(2); }

    for (long long i = 0; i < n; i++) {
        int w = snprintf(out[i].data, DATA_LENGTH, "%s,%s", L[i].data, R[i].data);
        if (w < 0 || w >= DATA_LENGTH) {
            fprintf(stderr,
                "row %lld: concat exceeds DATA_LENGTH=%d (needed %d bytes).\n"
                "  Recompile with a larger -DDATA_LENGTH=N.\n", i, DATA_LENGTH, w);
            exit(3);
        }
        out[i].table_0 = true;

        const char *p = nth_column(out[i].data, key_col);
        if (!p) {
            fprintf(stderr, "row %lld: next_key_col=%d past end of payload\n", i, key_col);
            exit(3);
        }
        out[i].key = strtoll(p, NULL, 10);
    }

    free(L);
    free(R);
    return out;
}

/* -------------------------- final filter ------------------------ */
/* Plain non-oblivious final-stage filter. Sound under the Obliviator threat
 * model because the final output size already leaks. If uniform access
 * patterns are desired in a future revision, swap this for oblivious_compact.
 */
static int row_passes(const struct plan *p, const char *concat) {
    for (int f = 0; f < p->num_filters; f++) {
        const struct filter *flt = &p->filters[f];
        const char *col = nth_column(concat, flt->col);
        if (!col) return 0;
        long long v = strtoll(col, NULL, 10);
        int pass = 0;
        if      (!strcmp(flt->op, "eq")) pass = (v == flt->val);
        else if (!strcmp(flt->op, "gt")) pass = (v >  flt->val);
        else if (!strcmp(flt->op, "ge")) pass = (v >= flt->val);
        else if (!strcmp(flt->op, "lt")) pass = (v <  flt->val);
        else if (!strcmp(flt->op, "le")) pass = (v <= flt->val);
        else { fprintf(stderr, "unknown filter op: %s\n", flt->op); exit(3); }
        if (!pass) return 0;
    }
    return 1;
}

/* Runs all final_filters and compacts L/R in place. Returns the new length. */
static long long apply_final_filters(elem_t *L, elem_t *R, long long n, const struct plan *p) {
    if (p->num_filters == 0) return n;
    long long keep = 0;
    char concat[DATA_LENGTH * 2 + 2];
    for (long long i = 0; i < n; i++) {
        snprintf(concat, sizeof(concat), "%s,%s", L[i].data, R[i].data);
        if (row_passes(p, concat)) {
            if (keep != i) { L[keep] = L[i]; R[keep] = R[i]; }
            keep++;
        }
    }
    return keep;
}

/* -------------------------- CSV emitter (off-clock) ------------- */
static void write_output_csv(const char *path, elem_t *L, elem_t *R, long long n,
                              const struct plan *p) {
    FILE *fp = fopen(path, "w");
    if (!fp) { perror(path); exit(2); }

    if (p->num_schema > 0) {
        for (int i = 0; i < p->num_schema; i++) {
            fprintf(fp, "%s%c", p->schema_name[i], (i + 1 == p->num_schema) ? '\n' : ',');
        }
    }
    for (long long i = 0; i < n; i++) {
        fprintf(fp, "%s,%s\n", L[i].data, R[i].data);
    }
    fclose(fp);
}

/* -------------------------- main -------------------------------- */
int main(int argc, char **argv) {
    if (argc != 4) {
        printf("usage: %s <num_threads> <plan_file> <output_csv>\n", argv[0]);
        return 1;
    }
    size_t num_threads = (size_t)atoi(argv[1]);
    const char *plan_path = argv[2];
    const char *out_csv   = argv[3];
    total_num_threads = num_threads;

    struct plan plan;
    if (parse_plan(plan_path, &plan) != 0) return 1;

    printf("Threads: %zu\n", num_threads);
    printf("Plan: %s  (%d steps, %d filters, %d schema cols)\n",
           plan_path, plan.num_steps, plan.num_filters, plan.num_schema);

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
        printf("Created %zu worker threads\n", num_threads - 1);
    }

    /* -------- chained pairwise joins (in-memory) ----------------- */
    elem_t *L_prev = NULL, *R_prev = NULL; /* output of prior step: L=table_0=true, R=table_0=false */
    long long prev_len = 0;

    double total_sort = 0.0;
    double total_join = 0.0;

    for (int i = 0; i < plan.num_steps; i++) {
        struct step *st = &plan.steps[i];

        /* side0 (table_0=true): either loaded from file, or built from prior step */
        elem_t *side0 = NULL;
        long long len0 = 0;
        struct timeval bld_s, bld_e;
        double build_time = 0.0;

        if (st->side0_is_prev) {
            int prev_nkc = plan.steps[i - 1].next_key_col;
            if (prev_nkc < 0) die("internal: step with side0=prev after last step");
            gettimeofday(&bld_s, NULL);
            side0 = build_next_side0(L_prev, R_prev, prev_len, prev_nkc);
            gettimeofday(&bld_e, NULL);
            build_time = (bld_e.tv_sec - bld_s.tv_sec) + (bld_e.tv_usec - bld_s.tv_usec) / 1e6;
            len0 = prev_len;
            L_prev = R_prev = NULL;
        } else {
            /* loader is off-clock per the timing contract */
            side0 = load_side_file(st->side0_path, &len0, 1);
        }

        /* side1 (table_0=false): always loaded from file (off-clock) */
        long long len1 = 0;
        elem_t *side1 = load_side_file(st->side1_path, &len1, 0);

        /* merge into kernel input buffer (on-clock: allocation + copy counts as
         * "intermediate processing" per the timing contract) */
        struct timeval mrg_s, mrg_e;
        gettimeofday(&mrg_s, NULL);
        long long total = len0 + len1;
        elem_t *arr = calloc((size_t)total, sizeof(*arr));
        if (!arr) die("kernel input calloc failed");
        memcpy(arr,        side0, (size_t)len0 * sizeof(*arr));
        memcpy(arr + len0, side1, (size_t)len1 * sizeof(*arr));
        free(side0);
        free(side1);
        gettimeofday(&mrg_e, NULL);
        double merge_time = (mrg_e.tv_sec - mrg_s.tv_sec) + (mrg_e.tv_usec - mrg_s.tv_usec) / 1e6;

        /* pairwise join */
        elem_t *arr_index = NULL;
        long long result_len = 0;
        double sort_t = 0.0;
        double step_t = scalable_oblivious_join_to_array(arr, len0, len1,
                                                          &arr_index, &result_len, &sort_t);

        printf("step %d: input=%lld+%lld=%lld  matched=%lld  "
               "sort=%.6f join_total=%.6f merge=%.6f build_side0=%.6f\n",
               i, len0, len1, total, result_len,
               sort_t, step_t, merge_time, build_time);

        total_sort += sort_t;
        total_join += step_t + merge_time + build_time;

        /* hand off to next step */
        L_prev   = arr_index;   /* table_0=true (index)  side */
        R_prev   = arr;         /* table_0=false (probe) side, compacted in place */
        prev_len = result_len;
    }

    /* -------- final filter + CSV emit ---------------------------- */
    struct timeval flt_s, flt_e;
    gettimeofday(&flt_s, NULL);
    long long final_len = apply_final_filters(L_prev, R_prev, prev_len, &plan);
    gettimeofday(&flt_e, NULL);
    double filter_time = (flt_e.tv_sec - flt_s.tv_sec) + (flt_e.tv_usec - flt_s.tv_usec) / 1e6;

    if (plan.num_filters > 0) {
        printf("final_filter: %lld -> %lld rows (%.6f s)\n", prev_len, final_len, filter_time);
    }
    total_join += filter_time;

    write_output_csv(out_csv, L_prev, R_prev, final_len, &plan);

    printf("\n=== Multi-way Summary ===\n");
    printf("steps            = %d\n", plan.num_steps);
    printf("total_sort_sec   = %.6f\n", total_sort);
    printf("total_online_sec = %.6f    (kernel + merge + side0-build + final-filter)\n", total_join);
    printf("final_rows       = %lld\n", final_len);
    printf("output_csv       = %s\n", out_csv);

    /* cleanup */
    free(L_prev);
    free(R_prev);

    if (num_threads > 1 && threads) {
        thread_release_all();
        for (size_t i = 0; i < num_threads - 1; i++) pthread_join(threads[i], NULL);
        free(threads);
    }
    thread_system_cleanup();
    scalable_oblivious_join_free();
    return 0;
}
