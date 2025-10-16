#include "obl_building_blocks.h"
#include <math.h>

namespace  obligraph
{
    std::pair<int, int> get_cutoffs_for_thread(int thread_id, int total, int n_threads) {
        int chunks = floor(total / n_threads);
        int start = chunks*thread_id;
        int end = start+chunks;
        if (thread_id + 1 == n_threads) {
            end = total;
        }
        // printf("[t %d] bounds: [%d, %d)\n", thread_id, start, end);
        return std::make_pair(start, end);
    }
} // namespace  obligraph

