/* multiway_main.c — Obliviator multi-way join driver (NFK kernel).
 *
 * Same plan format and CLI as the FK driver, but calls the non-foreign-key
 * kernel (scalable_oblivious_join_to_array from obliviatorNFK-TDX), which
 * computes the full equi-join cross-product per key group. This lets us
 * chain pairwise joins into a multi-way query even when neither side has
 * unique keys — a situation that arises at step 2+ of any k-hop chain with
 * k≥2 (the intermediate accumulates duplicate join keys from prior joins).
 *
 * CLI:
 *     ./multiway_main <num_threads> <plan_file> <output_csv>
 *
 * See obliviatorFK-TDX/multiway_main.c for the plan-file grammar; it is
 * kernel-agnostic. The only difference at runtime is which kernel function
 * is called per step.
 *
 * Threat model notes are unchanged from the FK driver: intermediate result
 * sizes leak (accepted for the Obliviator baseline); single-table filters
 * are deferred to the final pairwise join's output only; base CSV read and
 * final CSV write are out of the reported timing.
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
extern double scalable_oblivious_join_to_array(elem_t *arr, int length1, int length2,
                                                elem_t **arr1_out, elem_t **arr2_out,
                                                int *result_len,
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

static void *start_thread_work(void *arg) { (void)arg; thread_start_work(); return NULL; }
static void die(const char *msg) { fprintf(stderr, "%s\n", msg); exit(2); }

/* -------------------------- plan parser ------------------------- */
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
            if (sscanf(s, "num_steps %d", &p->num_steps) != 1 ||
                p->num_steps < 1 || p->num_steps > MAX_STEPS) {
                fprintf(stderr, "plan:%d: bad num_steps\n", lineno); fclose(fp); return -1;
            }
        } else if (!strncmp(s, "step ", 5)) {
            int idx = -1, nkc = -1;
            char side0[300] = {0}, side1[300] = {0};
            if (sscanf(s, "step %d side0=%299s side1=%299s next_key_col=%d",
                       &idx, side0, side1, &nkc) != 4 || idx < 0 || idx >= p->num_steps) {
                fprintf(stderr, "plan:%d: bad step line\n", lineno); fclose(fp); return -1;
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
            if (p->num_filters >= MAX_FILTERS) die("too many filters");
            struct filter *f = &p->filters[p->num_filters];
            if (sscanf(s, "final_filter %d %3s %lld", &f->col, f->op, &f->val) != 3) {
                fprintf(stderr, "plan:%d: bad final_filter\n", lineno); fclose(fp); return -1;
            }
            p->num_filters++;
        } else if (!strncmp(s, "schema ", 7)) {
            if (p->num_schema >= MAX_SCHEMA) die("too many schema entries");
            int col; char name[64] = {0};
            if (sscanf(s, "schema %d %63s", &col, name) != 2) {
                fprintf(stderr, "plan:%d: bad schema line\n", lineno); fclose(fp); return -1;
            }
            p->schema_col[p->num_schema] = col;
            strncpy(p->schema_name[p->num_schema], name, 63);
            p->num_schema++;
        } else {
            fprintf(stderr, "plan:%d: unknown directive: %s", lineno, s); fclose(fp); return -1;
        }
    }
    fclose(fp);

    for (int i = 0; i < p->num_steps; i++) {
        if (p->steps[i].index != i) { fprintf(stderr, "plan: step %d missing\n", i); return -1; }
    }
    return 0;
}

/* -------------------------- side-file loader (off-clock) -------- */
static elem_t *load_side_file(const char *path, int *len_out, int table_0) {
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); exit(2); }

    long long n_ll;
    if (fscanf(fp, "%lld\n", &n_ll) != 1) {
        fprintf(stderr, "%s: bad header (expected row count)\n", path); exit(2);
    }
    if (n_ll > (long long)(1U << 31) - 1) {
        fprintf(stderr, "%s: row count %lld exceeds NFK int range\n", path, n_ll); exit(2);
    }
    int n = (int)n_ll;

    elem_t *arr = calloc((size_t)n, sizeof(*arr));
    if (!arr) { fprintf(stderr, "calloc %d elem_t failed\n", n); exit(2); }

    for (int i = 0; i < n; i++) {
        long long k;
        if (fscanf(fp, "%lld %[^\n]\n", &k, arr[i].data) != 2) {
            fprintf(stderr, "%s: parse error at row %d\n", path, i); exit(2);
        }
        arr[i].data[DATA_LENGTH - 1] = '\0';
        arr[i].key = (int)k;
        arr[i].table_0 = (bool)table_0;
    }
    fclose(fp);
    *len_out = n;
    return arr;
}

/* -------------------------- column helpers ---------------------- */
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
 * L[i].data + "," + R[i].data into a fresh elem_t, pulling the new .key
 * from column `key_col` of that concatenation.
 * Takes ownership of L, R and frees them.
 *
 * Convention: L = table_0=true side from prior kernel output (i.e. arr1_prev),
 *             R = table_0=false side from prior kernel output (i.e. arr2_prev).
 * So the concat preserves schema order: prior intermediate's columns, then
 * the new base table's columns. The concat becomes the prior-columns-carrier
 * for the next step.
 */
static elem_t *build_next_side0(elem_t *L, elem_t *R, int n, int key_col) {
    elem_t *out = calloc((size_t)n, sizeof(*out));
    if (!out) { perror("calloc"); exit(2); }

    for (int i = 0; i < n; i++) {
        int w = snprintf(out[i].data, DATA_LENGTH, "%s,%s", L[i].data, R[i].data);
        if (w < 0 || w >= DATA_LENGTH) {
            fprintf(stderr,
                "row %d: concat exceeds DATA_LENGTH=%d (needed %d bytes).\n"
                "  Recompile with a larger -DDATA_LENGTH=N.\n", i, DATA_LENGTH, w);
            exit(3);
        }
        out[i].table_0 = true;

        const char *p = nth_column(out[i].data, key_col);
        if (!p) {
            fprintf(stderr, "row %d: next_key_col=%d past end of payload\n", i, key_col);
            exit(3);
        }
        out[i].key = (int)strtoll(p, NULL, 10);
    }

    free(L);
    free(R);
    return out;
}

/* -------------------------- final filter ------------------------ */
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

static int apply_final_filters(elem_t *L, elem_t *R, int n, const struct plan *p) {
    if (p->num_filters == 0) return n;
    int keep = 0;
    char concat[DATA_LENGTH * 2 + 2];
    for (int i = 0; i < n; i++) {
        snprintf(concat, sizeof(concat), "%s,%s", L[i].data, R[i].data);
        if (row_passes(p, concat)) {
            if (keep != i) { L[keep] = L[i]; R[keep] = R[i]; }
            keep++;
        }
    }
    return keep;
}

/* -------------------------- CSV emitter (off-clock) ------------- */
static void write_output_csv(const char *path, elem_t *L, elem_t *R, int n,
                              const struct plan *p) {
    FILE *fp = fopen(path, "w");
    if (!fp) { perror(path); exit(2); }

    if (p->num_schema > 0) {
        for (int i = 0; i < p->num_schema; i++) {
            fprintf(fp, "%s%c", p->schema_name[i], (i + 1 == p->num_schema) ? '\n' : ',');
        }
    }
    for (int i = 0; i < n; i++) {
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
    printf("Kernel: NFK (non-foreign-key, general equi-join)\n");
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
    /* After each NFK step: arr1 = table_0=true side, arr2 = table_0=false side. */
    elem_t *arr1_prev = NULL, *arr2_prev = NULL;
    int prev_len = 0;

    double total_sort = 0.0;
    double total_online = 0.0;

    for (int i = 0; i < plan.num_steps; i++) {
        struct step *st = &plan.steps[i];

        elem_t *side0 = NULL;
        int len0 = 0;
        struct timeval bld_s, bld_e;
        double build_time = 0.0;

        if (st->side0_is_prev) {
            int prev_nkc = plan.steps[i - 1].next_key_col;
            if (prev_nkc < 0) die("internal: step with side0=prev after last step");
            gettimeofday(&bld_s, NULL);
            side0 = build_next_side0(arr1_prev, arr2_prev, prev_len, prev_nkc);
            gettimeofday(&bld_e, NULL);
            build_time = (bld_e.tv_sec - bld_s.tv_sec) + (bld_e.tv_usec - bld_s.tv_usec) / 1e6;
            len0 = prev_len;
            arr1_prev = arr2_prev = NULL;
        } else {
            side0 = load_side_file(st->side0_path, &len0, 1);
        }

        int len1 = 0;
        elem_t *side1 = load_side_file(st->side1_path, &len1, 0);

        /* merge side0 + side1 into kernel input buffer (on-clock) */
        struct timeval mrg_s, mrg_e;
        gettimeofday(&mrg_s, NULL);
        int total = len0 + len1;
        elem_t *arr = calloc((size_t)total, sizeof(*arr));
        if (!arr) die("kernel input calloc failed");
        memcpy(arr,        side0, (size_t)len0 * sizeof(*arr));
        memcpy(arr + len0, side1, (size_t)len1 * sizeof(*arr));
        free(side0);
        free(side1);
        gettimeofday(&mrg_e, NULL);
        double merge_time = (mrg_e.tv_sec - mrg_s.tv_sec) + (mrg_e.tv_usec - mrg_s.tv_usec) / 1e6;

        /* pairwise join */
        elem_t *arr1_new = NULL;
        elem_t *arr2_new = NULL;
        int result_len = 0;
        double sort_t = 0.0;
        double step_t = scalable_oblivious_join_to_array(arr, len0, len1,
                                                          &arr1_new, &arr2_new,
                                                          &result_len, &sort_t);

        free(arr);  /* caller-owned kernel input buffer — safe to free after the call */

        printf("step %d: input=%d+%d=%d  matched=%d  "
               "sort=%.6f join_total=%.6f merge=%.6f build_side0=%.6f\n",
               i, len0, len1, total, result_len,
               sort_t, step_t, merge_time, build_time);

        total_sort   += sort_t;
        total_online += step_t + merge_time + build_time;

        arr1_prev = arr1_new;
        arr2_prev = arr2_new;
        prev_len  = result_len;
    }

    /* -------- final filter + CSV emit ---------------------------- */
    struct timeval flt_s, flt_e;
    gettimeofday(&flt_s, NULL);
    int final_len = apply_final_filters(arr1_prev, arr2_prev, prev_len, &plan);
    gettimeofday(&flt_e, NULL);
    double filter_time = (flt_e.tv_sec - flt_s.tv_sec) + (flt_e.tv_usec - flt_s.tv_usec) / 1e6;

    if (plan.num_filters > 0) {
        printf("final_filter: %d -> %d rows (%.6f s)\n", prev_len, final_len, filter_time);
    }
    total_online += filter_time;

    write_output_csv(out_csv, arr1_prev, arr2_prev, final_len, &plan);

    printf("\n=== Multi-way Summary (NFK) ===\n");
    printf("steps            = %d\n", plan.num_steps);
    printf("total_sort_sec   = %.6f\n", total_sort);
    printf("total_online_sec = %.6f    (kernel + merge + side0-build + final-filter)\n", total_online);
    printf("final_rows       = %d\n", final_len);
    printf("output_csv       = %s\n", out_csv);

    free(arr1_prev);
    free(arr2_prev);

    if (num_threads > 1 && threads) {
        thread_release_all();
        for (size_t i = 0; i < num_threads - 1; i++) pthread_join(threads[i], NULL);
        free(threads);
    }
    thread_system_cleanup();
    /* scalable_oblivious_join_free() intentionally not called here: it calls
     * aggregation_tree_free() which is already invoked by the kernel core at
     * the end of each step; a trailing call would double-free. This matches
     * upstream standalone_main.c which also skips it. */
    return 0;
}
