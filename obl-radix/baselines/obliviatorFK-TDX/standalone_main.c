#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global variables needed by the algorithms
extern size_t total_num_threads;
// int world_rank = 0;
// int world_size = 1;

// Include the core functionality
#include "common/elem_t.h"
// #include "common/timing.h"
#include "enclave/threading.h"

// Function declarations
extern int scalable_oblivious_join_init(int nthreads);
extern void scalable_oblivious_join_free(void);
extern double scalable_oblivious_join(elem_t *arr, int length1, int length2,
                                      const char *output_path,
                                      double *sort_time_out);
// extern int rand_init(void);
// extern void rand_free(void);

// Worker thread function
static void *start_thread_work(void *arg) {
  (void)arg;
  thread_start_work();
  return NULL;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("usage: %s num_threads input_file [output_file] [input_file2] "
           "[output_file2]\n",
           argv[0]);
    printf("  num_threads:  number of threads to use\n");
    printf("  input_file:   input data file (join 1)\n");
    printf("  output_file:  output file for join 1 results (default: "
           "join_results.txt)\n");
    printf("  input_file2:  input data file for join 2 (optional, for 1-hop)\n");
    printf("  output_file2: output file for join 2 results (default: "
           "join_results2.txt)\n");
    return 1;
  }

  size_t num_threads = atoi(argv[1]);
  char *input_file = argv[2];

  const char *output_file = (argc >= 4) ? argv[3] : "join_results.txt";
  const char *input_file2 = (argc >= 5) ? argv[4] : NULL;
  const char *output_file2 = (argc >= 6) ? argv[5] : "join_results2.txt";

  total_num_threads = num_threads;

  printf("Threads: %zu\n", num_threads);
  printf("Input file: %s\n", input_file);
  if (input_file2)
    printf("Input file 2: %s\n", input_file2);

  /* ------------------------- Initialise helpers ---------------------- */
  // if (rand_init() != 0) {
  //     fprintf(stderr, "Failed to initialize random number generator\n");
  //     return 1;
  // }

  if (scalable_oblivious_join_init(num_threads) != 0) {
    fprintf(stderr, "Failed to initialize join\n");
    return 1;
  }

  // Initialize threading system
  thread_system_init();

  // Create worker threads
  pthread_t *threads = NULL;
  if (num_threads > 1) {
    threads = malloc((num_threads - 1) * sizeof(pthread_t));
    if (!threads) {
      fprintf(stderr, "Failed to allocate thread array\n");
      return 1;
    }

    for (size_t i = 0; i < num_threads - 1; i++) {
      int ret = pthread_create(&threads[i], NULL, start_thread_work, NULL);
      if (ret != 0) {
        fprintf(stderr, "Failed to create thread %zu: %s\n", i, strerror(ret));
        thread_release_all();
        for (size_t j = 0; j < i; j++) {
          pthread_join(threads[j], NULL);
        }
        free(threads);
        return 1;
      }
    }
    printf("Created %zu worker threads\n", num_threads - 1);
  }

  FILE *fp = fopen(input_file, "r");
  if (!fp) {
    perror("Failed to open input file");
    return 1;
  }

  int length1, length2;
  if (fscanf(fp, "%d %d\n", &length1, &length2) != 2) {
    fprintf(stderr, "Failed to parse table lengths from %s\n", input_file);
    fclose(fp);
    return 1;
  }

  printf("Table 1: %d records, Table 2: %d records\n", length1, length2);

  elem_t *arr = calloc(length1 + length2, sizeof(*arr));
  if (!arr) {
    fprintf(stderr, "Failed to allocate array\n");
    fclose(fp);
    return 1;
  }

  // Parse table 1
  for (int i = 0; i < length1; i++) {
    if (fscanf(fp, "%lld %[^\n]\n", &arr[i].key, arr[i].data) != 2) {
      fprintf(stderr, "Error parsing table 1, record %d\n", i);
      free(arr);
      fclose(fp);
      return 1;
    }
    arr[i].data[DATA_LENGTH - 1] = '\0';
    arr[i].table_0 = true;
  }

  // Parse table 2
  for (int i = length1; i < length1 + length2; i++) {
    if (fscanf(fp, "%lld %[^\n]\n", &arr[i].key, arr[i].data) != 2) {
      fprintf(stderr, "Error parsing table 2, record %d\n", i - length1);
      free(arr);
      fclose(fp);
      return 1;
    }
    arr[i].data[DATA_LENGTH - 1] = '\0';
    arr[i].table_0 = false;
  }

  fclose(fp);

  /* Allocate a buffer large enough to hold the join results.
     The number of join results can be up to length1 * length2
     (worst case: every record in table1 matches every record in table2).*/
  size_t max_join_results = (size_t)length1 * (size_t)length2;
  size_t output_buf_size = max_join_results * 64 + 1;

  /* Safety check for very large joins to prevent excessive memory usage */
#define MAX_REASONABLE_BUFFER (1ULL << 30) // 1GB limit (configurable)
  if (output_buf_size > MAX_REASONABLE_BUFFER) {
    output_buf_size = MAX_REASONABLE_BUFFER;
    printf("\nUsing limited output buffer size: %zu bytes.\nAdjust the buffer "
           "size based on your workload, esp if you're encountering seg "
           "faults.\n\n",
           output_buf_size);
  }

  char *output_buf = malloc(output_buf_size);
  if (!output_buf) {
    fprintf(stderr, "Failed to allocate output buffer (%zu bytes)\n",
            output_buf_size);
    free(arr);
    return 1;
  }

  /* Run the oblivious join 1. */
  printf("\n--- Join 1 ---\n");
  double sort_time1 = 0.0;
  double time1 = scalable_oblivious_join(arr, length1, length2, output_buf, &sort_time1);

  /* Persist results to disk. */
  FILE *ofp = fopen(output_file, "w");
  if (!ofp) {
    perror("Failed to open output file for writing");
    free(output_buf);
    free(arr);
    return 1;
  }
  fputs(output_buf, ofp);
  fclose(ofp);
  printf("Results written to %s\n", output_file);
  free(output_buf);
  free(arr);

  /* ---- Optional second join (for 1-hop queries) ---- */
  double time2 = 0.0;
  if (input_file2) {
    FILE *fp2 = fopen(input_file2, "r");
    if (!fp2) {
      perror("Failed to open second input file");
      goto cleanup_threads;
    }

    int length3, length4;
    if (fscanf(fp2, "%d %d\n", &length3, &length4) != 2) {
      fprintf(stderr, "Failed to parse table lengths from %s\n", input_file2);
      fclose(fp2);
      goto cleanup_threads;
    }

    printf("\n--- Join 2 ---\n");
    printf("Table 1: %d records, Table 2: %d records\n", length3, length4);

    elem_t *arr2 = calloc(length3 + length4, sizeof(*arr2));
    if (!arr2) {
      fprintf(stderr, "Failed to allocate array for join 2\n");
      fclose(fp2);
      goto cleanup_threads;
    }

    for (int i = 0; i < length3; i++) {
      if (fscanf(fp2, "%lld %[^\n]\n", &arr2[i].key, arr2[i].data) != 2) {
        fprintf(stderr, "Error parsing join2 table 1, record %d\n", i);
        free(arr2);
        fclose(fp2);
        goto cleanup_threads;
      }
      arr2[i].data[DATA_LENGTH - 1] = '\0';
      arr2[i].table_0 = true;
    }

    for (int i = length3; i < length3 + length4; i++) {
      if (fscanf(fp2, "%lld %[^\n]\n", &arr2[i].key, arr2[i].data) != 2) {
        fprintf(stderr, "Error parsing join2 table 2, record %d\n", i - length3);
        free(arr2);
        fclose(fp2);
        goto cleanup_threads;
      }
      arr2[i].data[DATA_LENGTH - 1] = '\0';
      arr2[i].table_0 = false;
    }
    fclose(fp2);

    size_t max_join_results2 = (size_t)length3 * (size_t)length4;
    size_t output_buf_size2 = max_join_results2 * 64 + 1;
    if (output_buf_size2 > MAX_REASONABLE_BUFFER) {
      output_buf_size2 = MAX_REASONABLE_BUFFER;
      printf("\nUsing limited output buffer size: %zu bytes.\n",
             output_buf_size2);
    }

    char *output_buf2 = malloc(output_buf_size2);
    if (!output_buf2) {
      fprintf(stderr, "Failed to allocate output buffer 2\n");
      free(arr2);
      goto cleanup_threads;
    }

    double sort_time2 = 0.0;
    time2 = scalable_oblivious_join(arr2, length3, length4, output_buf2, &sort_time2);

    FILE *ofp2 = fopen(output_file2, "w");
    if (!ofp2) {
      perror("Failed to open second output file");
    } else {
      fputs(output_buf2, ofp2);
      fclose(ofp2);
      printf("Results written to %s\n", output_file2);
    }
    free(output_buf2);
    free(arr2);

    printf("\n=== 1-Hop Summary ===\n");
    printf("Join 1: sort=%.6f total=%.6f seconds\n", sort_time1, time1);
    printf("Join 2: sort=%.6f total=%.6f seconds\n", sort_time2, time2);
    printf("Total sort time: %.6f seconds\n", sort_time1 + sort_time2);
    printf("Total 1-hop join time: %.6f seconds\n", time1 + time2);
  }

cleanup_threads:
  // Signal threads to stop and wait for them to finish
  if (num_threads > 1 && threads) {
    thread_release_all();
    for (size_t i = 0; i < num_threads - 1; i++) {
      pthread_join(threads[i], NULL);
    }
    free(threads);
  }
  thread_system_cleanup();

  return 0;
}
