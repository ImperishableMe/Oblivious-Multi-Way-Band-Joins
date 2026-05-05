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
extern void scalable_oblivious_join(elem_t *arr, int length1, int length2,
                                    const char *output_path);
// extern int rand_init(void);
// extern void rand_free(void);

// Worker thread function - Nafis
static void *start_thread_work(void *arg) {
  (void)arg; // Unused parameter
  thread_start_work();
  return NULL;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("usage: %s num_threads input_file [output_file]\n", argv[0]);
    printf("  num_threads: number of threads to use\n");
    printf("  input_file:  input data file\n");
    printf("  output_file: output file for join results (optional, default: "
           "join_results.txt)\n");
    return 1;
  }

  size_t num_threads = atoi(argv[1]);
  char *input_file = argv[2];

  const char *output_file = (argc >= 4) ? argv[3] : "join_results.txt";

  total_num_threads = num_threads;

  printf("Threads: %zu\n", num_threads);
  printf("Input file: %s\n", input_file);

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
    if (fscanf(fp, "%d %[^\n]\n", &arr[i].key, arr[i].data) != 2) {
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
    if (fscanf(fp, "%d %[^\n]\n", &arr[i].key, arr[i].data) != 2) {
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
  const size_t MAX_REASONABLE_BUFFER = 1ULL << 30; // 1GB limit (configurable)
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

  /* Run the oblivious join. */
  scalable_oblivious_join(arr, length1, length2, output_buf);

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
