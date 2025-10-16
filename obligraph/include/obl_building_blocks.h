#pragma once

#include <iterator>
#include <thread>
#include <vector>
#include <bit>
#include <cassert>
#include <cmath>

#include "obl_primitives.h"
#include "threadpool.h"


namespace obligraph {

std::pair<int, int> get_cutoffs_for_thread(int thread_id, int total, int n_threads);

template <typename T, typename Comparator>
inline void o_compare_and_swap(T &a, T &b, Comparator cmp, bool asc = true) {
    bool cond = !(cmp(a, b) == asc);
    T tmp = a;
    a = ObliviousChoose(cond, b, a);
    b = ObliviousChoose(cond, tmp, b);
}

template <typename T>
inline void o_mem_swap(T &a, T &b, bool cond) {
    T tmp = a;
    a = ObliviousChoose(cond, b, a);
    b = ObliviousChoose(cond, tmp, b);
}


template <typename T, typename Comparator>
inline void swap_block(T *array, std::size_t a, std::size_t b, std::size_t c,
                            Comparator cmp, bool ascend = true) {
    for (std::size_t i = a; i < b; i++) {
        o_compare_and_swap(array[i], array[i + c], cmp, ascend);
    }
}

template <typename T>
inline void swap_range(T *array1, T *array2, int swap_st, int swap_en, int offset_m1_mod, bool swap_flag) {
    for (std::size_t i = swap_st; i < swap_en; i++) {
        o_mem_swap(array1[i], array2[i], swap_flag ^ (i >= offset_m1_mod));
    }
}

template <typename T, typename Comparator>
void o_merge(T *array, std::size_t left, std::size_t right,
             Comparator cmp, ThreadPool &pool, std::size_t num_threads, bool ascend = true) {

    if (right <= 1 + left) {
        return; // Base case: single element or empty
    }

    auto mid_len = detail::greatest_power_of_two_less_than(right - left);

    if (num_threads <= 1) {
        for (auto i = left; i < right - mid_len; i++) {
            o_compare_and_swap(array[i], array[i + mid_len], cmp, ascend);
        }
        o_merge(array, left, left + mid_len, cmp, pool, 1, ascend);
        o_merge(array, left + mid_len, right, cmp, pool, 1, ascend);
        return;
    }
    // Parallel merge logic would go here
    long long index_start[num_threads + 1];
    index_start[0] = left;
    long long length_thread = (right - mid_len - left) / num_threads;
    long long length_extra = (right - mid_len - left) % num_threads;
    std::future<void> work[num_threads - 1];

    for (long long i = 0; i < num_threads; i++) {
        index_start[i + 1] = index_start[i] + length_thread + (i < length_extra);

        if (i < num_threads - 1) {
            work[i] = pool.submit([&, i]() {
                swap_block(array, index_start[i], index_start[i + 1], mid_len, cmp, ascend);
            });
        }
    }
    swap_block(array, index_start[num_threads - 1], index_start[num_threads], mid_len, cmp, ascend);
    for (long long i = 0; i < num_threads - 1; i++) {
        work[i].get();
    }
    // swap in parallel is completed, now do split merge

    int n_threads_left = num_threads / 2;
    int n_threads_right = num_threads - n_threads_left;

    auto left_task = [&]() {
        o_merge(array, left, left + mid_len, cmp, pool, n_threads_left, ascend);
    };
    auto left_future = pool.submit(left_task);
    o_merge(array, left + mid_len, right, cmp, pool, n_threads_right, ascend);

    left_future.get();
}


template <typename T, typename Comparator>
void o_sort(T *array, std::size_t left, std::size_t right, Comparator cmp, ThreadPool &pool, std::size_t num_threads, bool ascend = true) {
    if (right - left <= 1) {
        return; // Base case: single element or empty
    }

    auto mid = left + (right - left) / 2;

    if (num_threads <= 1) {
        // Fallback to sequential sort
        o_sort(array, left, mid, cmp, pool, 1, !ascend);
        o_sort(array, mid, right, cmp, pool, 1, ascend);
        o_merge(array, left, right, cmp, pool, 1, ascend);
        return;
    } 
    // Parallel sort logic would go here
    auto n_thread_left = num_threads / 2;
    auto n_thread_right = num_threads - n_thread_left;

    // Launch the left sort task
    auto left_task = [&]() {
        o_sort(array, left, mid, cmp, pool, n_thread_left, !ascend);
    };
    auto left_future = pool.submit(left_task);
    // Launch the right sort task in this thread
    o_sort(array, mid, right, cmp, pool, n_thread_right, ascend);
    // Wait for the left task to complete
    left_future.get();
    // Merge the results
    o_merge(array, left, right, cmp, pool, num_threads, ascend);
}


template <typename T>
void o_compact_pow2_inner(T *array, size_t n, int offset, uint8_t *tags, int *pref_tags) {
    if (n <= 1) return;
    if (n == 2) {
        bool cond = (tags[0] < tags[1]) ^ offset;
        o_mem_swap(array[0], array[1], cond);
        return;
    }
    int m1 = pref_tags[n / 2] - pref_tags[0];
    int offset_mod = (offset & ((n / 2) - 1));
    int offset_m1_mod = (offset + m1) & ((n / 2) - 1);
    bool offset_right = offset >= n / 2;
    bool left_wrapped = (offset_mod + m1) >= (n / 2);

    o_compact_pow2_inner(array, n / 2, offset_mod, tags, pref_tags);
    o_compact_pow2_inner(array + n / 2, n / 2, offset_m1_mod, tags + n / 2, pref_tags + n / 2);

    T *left_array = array;
    T *right_array = array + n / 2;
    bool swap_flag = left_wrapped ^ offset_right;
    int n_swap = n / 2;
    for (int i = 0; i < n_swap; i++) {
        swap_flag ^= (i == offset_m1_mod);
        o_mem_swap(left_array[i], right_array[i], swap_flag);
    }
}

template<typename T>
void o_compact_inner(T *array, size_t n, uint8_t *tags, int *pref_tags) {
    if (n <= 1) return;
    if (n == 2) {
        bool cond = tags[0] < tags[1];
        o_mem_swap(array[0], array[1], cond);
        return;
    }
    int gt_pow2 = std::bit_floor(n - 1);
    int split_index = n - gt_pow2;
    int mL = 0;
    for (int i = 0; i < split_index; i++) {
        mL += tags[i];
    }

    T *left_array = array;
    T *right_array = array + split_index;
    uint8_t *left_tags = tags;
    uint8_t *right_tags = tags + split_index;
    auto *right_pref_tags = pref_tags + split_index;

    o_compact_inner(left_array, split_index, left_tags, pref_tags);
    o_compact_pow2_inner(right_array, gt_pow2, /*offset*/ (gt_pow2 - split_index + mL) %gt_pow2, right_tags, right_pref_tags);

    right_array = array + gt_pow2;

    for (int i = 0; i < split_index; i++) {
        bool cond = i >= mL;
        o_mem_swap(left_array[i], right_array[i], cond);
    }
}


template<typename T>
void o_par_compact_inner_pow2(T *array, size_t n, int offset, uint8_t *tags, int *pref_tags, ThreadPool &pool, std::size_t num_threads) {
    if (num_threads <= 1 || n < 16) {
        o_compact_pow2_inner(array, n, offset, tags, pref_tags);
        return;
    }
    if (n <= 1) return;
    if (n == 2) {
        bool cond = (tags[0] < tags[1]) ^ offset;
        o_mem_swap(array[0], array[1], cond);
        return;
    }
    int m1 = pref_tags[n / 2] - pref_tags[0];
    int offset_mod = (offset & ((n / 2) - 1));
    int offset_m1_mod = (offset + m1) & ((n / 2) - 1);
    bool offset_right = offset >= n / 2;
    bool left_wrapped = (offset_mod + m1) >= (n / 2);

    int l_threads = num_threads / 2;
    int r_threads = num_threads - l_threads;

    // right half in separate thread
    auto right_task = [&]() {
        o_par_compact_inner_pow2(array + n / 2, n / 2, offset_m1_mod, tags + n / 2, pref_tags + n / 2, pool, r_threads);
    };
    auto fut = pool.submit(right_task);

    // left half in the main thread
    o_par_compact_inner_pow2(array, n / 2, offset_mod, tags, pref_tags, pool, l_threads);

    fut.get(); // wait for the right half

    T *left_array = array;
    T *right_array = array + n / 2;
    bool swap_flag = left_wrapped ^ offset_right;
    int n_swap = n / 2;

    std::vector<std::future<void>> futures(num_threads - 1);
    int inc = n_swap / num_threads;
    int extra = n_swap % num_threads;
    int last = 0;

    for (int i = 0; i < num_threads; i++) {
        int next = last + inc + (i < extra);
        if (i < num_threads - 1) {
            futures[i] = pool.submit([&, last, next]() {
                swap_range(left_array, right_array, last, next, offset_m1_mod, swap_flag);
            });
        }
        else {
            swap_range(left_array, right_array, last, next, offset_m1_mod, swap_flag);
        }
        last = next;
    }

    for (auto &fut : futures) {
        fut.get();
    }
}

template<typename T>
void o_par_compact(T *array, size_t n, uint8_t *tags, int *pref_tags, ThreadPool &pool, std::size_t num_threads) {
    if (num_threads <= 1 || n < 16) {
        o_compact_inner(array, n, tags, pref_tags);
        return;
    }
    int n1 = std::bit_floor(n - 1);
    int n2 = n - n1;
    int m2 = pref_tags[n] - pref_tags[0];

    auto *left_array = array;
    auto *right_array = array + n2;
    int r_threads = num_threads * n1 / n;
    int l_threads = num_threads - r_threads;

    auto right_task = [&]() {
        o_par_compact_inner_pow2(right_array, n1, (n1 - n2 + m2) % n1, tags + n2, pref_tags + n2, pool, r_threads);
    };
    auto fut = pool.submit(right_task); // launch right task parallelly
    o_par_compact(left_array, n2, tags, pref_tags, pool, l_threads);
    fut.get(); // wait for both side to be completed

    
    int num_swap = n - n1;
    int inc = num_swap / num_threads;
    int extra = num_swap % num_threads;
    int last = 0;

    right_array = array + n1;
    std::vector<std::future<void>> futures(num_threads - 1);

    for (int i = 0; i < num_threads; i++) {
        int next = last + inc + (i < extra);
        if (i < num_threads - 1) {
            futures[i] = pool.submit([&, last, next]() {
                swap_range(left_array, right_array, last, next, m2, false);
            });
        }
        else {
            swap_range(left_array, right_array, last, next, m2, false);
        }
        last = next;
    }

    for (auto &fut : futures) {
        fut.get();
    }
}

/**
 * Sorts elements in the range [begin, end) using multiple threads.
 * 
 * @param begin Iterator to the first element in the range to sort
 * @param end Iterator to one past the last element in the range
 * @param comp Comparison function object to use for sorting
 * @param num_threads Number of threads to use for parallel sorting
 */
template<typename RandomAccessIterator,
         typename Compare = std::less<typename std::iterator_traits<RandomAccessIterator>::value_type>>
void parallel_sort(RandomAccessIterator begin, 
                  RandomAccessIterator end, 
                  ThreadPool &pool,
                  Compare comp = Compare(),
                  std::size_t num_threads = 1) {

    using value_type = typename std::remove_reference<decltype(*begin)>::type;
    value_type *array = &(*begin);
    return o_sort<value_type, Compare>(array, 0, end - begin, comp, pool, num_threads);
}

template<typename RandomAccessIterator>
int parallel_o_compact(RandomAccessIterator begin, 
                RandomAccessIterator end, 
                ThreadPool &pool,
                uint8_t *tags,
                std::size_t num_threads) {

    using value_type = std::remove_reference_t<decltype(*begin)>;
    value_type *array = &(*begin);
    size_t n = end - begin;

    assert(pool.size() == num_threads);

    std::vector prefix_sum(n + 1, 0);
    for (size_t i = 0; i < n; i++) {
        prefix_sum[i + 1] = prefix_sum[i] + tags[i];
    }
    o_par_compact<value_type>(array, n, tags, prefix_sum.data(), pool, num_threads);
    return prefix_sum[n];
}


} // namespace obligraph
