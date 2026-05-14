#include "table.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <climits>
#include <cstring>
#include "debug_util.h"
#include "../core_logic/core.h"
#include "../core_logic/algorithms/oblivious_waksman.h"
#include "../core_logic/algorithms/min_heap.h"
#include "obl_building_blocks.h"  // obligraph::o_sort (parallel oblivious bitonic sort);
                                  // also transitively includes obligraph::ThreadPool

// Comparator dispatch lives in app/core_logic/operations/merge_comparators.c
extern "C" comparator_func_t get_merge_comparator(OpEcall type);

// Process-wide profile of shuffle_merge_sort. Reset / printed by ObliviousJoin.
namespace {
// Comparator op_types occupy values [0..N_OP_BUCKETS). Per-op counters let us
// see how much of total sort time is in each comparator family.
constexpr int N_OP_BUCKETS = 16;

struct ShuffleSortProfile {
    size_t calls = 0;
    size_t input_rows = 0;   // sum of pre-pad sizes (old path) / pre-sort sizes (new path)
    size_t padded_rows = 0;  // sum of post-pad sizes (old path); == input_rows for new path
    double total_s = 0.0;
    double pad_s = 0.0;
    double waksman_s = 0.0;
    double heap_sort_s = 0.0;
    double osort_s = 0.0;         // time inside obligraph::o_sort path
    double truncate_s = 0.0;
    size_t per_op_calls[N_OP_BUCKETS] = {};
    double per_op_time_s[N_OP_BUCKETS] = {};
    size_t per_op_rows[N_OP_BUCKETS] = {};
    size_t osort_calls = 0;       // sort calls that took the o_sort path
};
ShuffleSortProfile g_sms_profile;

using SmsClock = std::chrono::steady_clock;
inline double sms_dur(SmsClock::time_point a, SmsClock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

// Env-var switch: OBL_OSORT_THREADS=N (N >= 1) routes JOIN_ATTR sorts through
// obligraph::o_sort with N threads. N=0 or unset keeps the old Waksman path.
// Read once per process.
size_t osort_thread_count() {
    static size_t n = []() -> size_t {
        const char* e = std::getenv("OBL_OSORT_THREADS");
        if (!e) return 0;
        long v = std::atol(e);
        if (v < 0) v = 0;
        return static_cast<size_t>(v);
    }();
    return n;
}

// ThreadPool singleton sized to OBL_OSORT_THREADS. Constructed lazily on
// first use so size matches the env var read above.
obligraph::ThreadPool& spike_pool() {
    static obligraph::ThreadPool pool(std::max<size_t>(1, osort_thread_count()));
    return pool;
}
}

void Table::reset_shuffle_merge_sort_profile() {
    g_sms_profile = ShuffleSortProfile{};
}

void Table::print_shuffle_merge_sort_profile() {
    const auto& p = g_sms_profile;
    double stages = p.pad_s + p.waksman_s + p.heap_sort_s + p.osort_s + p.truncate_s;
    printf("SHUFFLE_MERGE_SORT_TIMING: calls=%zu osort_calls=%zu input_rows=%zu padded_rows=%zu "
           "total=%.6fs pad=%.6fs waksman=%.6fs heap_sort=%.6fs osort=%.6fs truncate=%.6fs stages=%.6fs\n",
           p.calls, p.osort_calls, p.input_rows, p.padded_rows, p.total_s,
           p.pad_s, p.waksman_s, p.heap_sort_s, p.osort_s, p.truncate_s, stages);
    static const char* op_names[N_OP_BUCKETS] = {
        "JOIN_ATTR", "PAIRWISE", "END_FIRST", "JOIN_THEN_OTHER",
        "ORIGINAL_INDEX", "ALIGNMENT_KEY", "PADDING_LAST", "DISTRIBUTE",
        "op8","op9","op10","op11","op12","op13","op14","op15"
    };
    for (int i = 0; i < N_OP_BUCKETS; ++i) {
        if (p.per_op_calls[i] == 0) continue;
        printf("SHUFFLE_MERGE_SORT_PER_OP: op=%s calls=%zu rows=%zu time=%.6fs\n",
               op_names[i], p.per_op_calls[i], p.per_op_rows[i], p.per_op_time_s[i]);
    }
}

// Constructor with required schema
Table::Table(const std::string& name, const std::vector<std::string>& schema)
    : table_name(name), num_columns(schema.size()), schema_column_names(schema) {
    if (schema.empty()) {
        throw std::runtime_error("Table '" + name + "' cannot be created with empty schema");
    }
    if (schema.size() > MAX_ATTRIBUTES) {
        throw std::runtime_error("Table '" + name + "' schema has " + std::to_string(schema.size()) +
                               " columns, exceeds MAX_ATTRIBUTES=" + std::to_string(MAX_ATTRIBUTES));
    }
}

// Copy constructor - deep copy for table aliasing
Table::Table(const Table& other)
    : entries(other.entries),
      table_name(other.table_name),
      num_columns(other.num_columns),
      schema_column_names(other.schema_column_names) {
    // Deep copy of entries vector is handled by std::vector copy constructor
    // All other fields are primitive or strings, copied automatically
}

void Table::add_entry(const Entry& entry) {
    entries.push_back(entry);
}

Entry& Table::get_entry(size_t index) {
    return entries[index];
}

const Entry& Table::get_entry(size_t index) const {
    return entries[index];
}

void Table::set_entry(size_t index, const Entry& entry) {
    if (index < entries.size()) {
        entries[index] = entry;
    }
}

Entry& Table::operator[](size_t index) {
    return entries[index];
}

const Entry& Table::operator[](size_t index) const {
    return entries[index];
}

size_t Table::size() const {
    return entries.size();
}

void Table::clear() {
    entries.clear();
}

void Table::set_all_field_type(entry_type_t type) {
    for (auto& entry : entries) {
        entry.field_type = type;
    }
}

void Table::initialize_original_indices() {
    for (size_t i = 0; i < entries.size(); i++) {
        entries[i].original_index = static_cast<int32_t>(i);
    }
}

void Table::initialize_leaf_multiplicities() {
    for (auto& entry : entries) {
        entry.local_mult = 1;
        entry.final_mult = 1;
    }
}

void Table::set_table_name(const std::string& name) {
    table_name = name;
}

std::string Table::get_table_name() const {
    return table_name;
}

void Table::set_num_columns(size_t n) {
    num_columns = n;
}

size_t Table::get_num_columns() const {
    return num_columns;
}

// Schema management methods for slim mode migration
void Table::set_schema(const std::vector<std::string>& columns) {
    schema_column_names = columns;
    // Also update num_columns to match
    if (num_columns == 0 || num_columns != columns.size()) {
        num_columns = columns.size();
    }
}

std::vector<std::string> Table::get_schema() const {
    return schema_column_names;
}

size_t Table::get_column_index(const std::string& col_name) const {
    for (size_t i = 0; i < schema_column_names.size(); i++) {
        if (schema_column_names[i] == col_name) {
            return i;
        }
    }
    // Column not found in schema
    throw std::runtime_error("Column not found: " + col_name);
}

bool Table::has_column(const std::string& col_name) const {
    // Check schema only
    for (const auto& name : schema_column_names) {
        if (name == col_name) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> Table::generate_generic_schema(size_t num_columns) {
    std::vector<std::string> schema;
    for (size_t i = 0; i < num_columns; i++) {
        schema.push_back("col" + std::to_string(i + 1));
    }
    return schema;
}

int32_t Table::get_attribute(size_t row, const std::string& col_name) const {
    if (row >= entries.size()) {
        throw std::out_of_range("Row index out of bounds");
    }
    size_t col_index = get_column_index(col_name);
    const Entry& entry = entries[row];
    if (col_index >= MAX_ATTRIBUTES) {
        throw std::out_of_range("Column index out of bounds");
    }
    return entry.attributes[col_index];
}

void Table::set_attribute(size_t row, const std::string& col_name, int32_t value) {
    if (row >= entries.size()) {
        throw std::out_of_range("Row index out of bounds");
    }
    size_t col_index = get_column_index(col_name);
    Entry& entry = entries[row];
    if (col_index >= MAX_ATTRIBUTES) {
        throw std::out_of_range("Column index exceeds MAX_ATTRIBUTES");
    }
    // Just set the value - column_names should already be set
    entry.attributes[col_index] = value;
}

std::vector<Entry>::iterator Table::begin() {
    return entries.begin();
}

std::vector<Entry>::iterator Table::end() {
    return entries.end();
}

std::vector<Entry>::const_iterator Table::begin() const {
    return entries.begin();
}

std::vector<Entry>::const_iterator Table::end() const {
    return entries.end();
}

Table::EncryptionStatus Table::get_encryption_status() const {
    // TDX migration: All data is now unencrypted (no app-level encryption)
    return UNENCRYPTED;
}

// ============================================================================
// Direct Operations Implementation (TDX - no batching needed)
// ============================================================================

// Helper: Get single-entry operation function
typedef void (*single_op_fn)(entry_t*);
typedef void (*single_op_with_params_fn)(entry_t*, int32_t);
typedef void (*two_param_op_fn)(entry_t*, int32_t, equality_type_t);
typedef void (*dual_entry_op_fn)(entry_t*, entry_t*);

static single_op_fn get_single_op_function(OpEcall op_type) {
    switch(op_type) {
        case OP_ECALL_TRANSFORM_SET_LOCAL_MULT_ONE:
            return transform_set_local_mult_one_op;
        case OP_ECALL_TRANSFORM_ADD_METADATA:
            return transform_add_metadata_op;
        case OP_ECALL_TRANSFORM_INIT_LOCAL_TEMPS:
            return transform_init_local_temps_op;
        case OP_ECALL_TRANSFORM_INIT_FINAL_MULT:
            return transform_init_final_mult_op;
        case OP_ECALL_TRANSFORM_INIT_FOREIGN_TEMPS:
            return transform_init_foreign_temps_op;
        case OP_ECALL_TRANSFORM_TO_SOURCE:
            return transform_to_source_op;
        case OP_ECALL_TRANSFORM_SET_SORT_PADDING:
            return transform_set_sort_padding_op;
        case OP_ECALL_TRANSFORM_INIT_DST_IDX:
            return transform_init_dst_idx_op;
        case OP_ECALL_TRANSFORM_INIT_INDEX:
            return transform_init_index_op;
        case OP_ECALL_TRANSFORM_MARK_ZERO_MULT_PADDING:
            return transform_mark_zero_mult_padding_op;
        case OP_ECALL_TRANSFORM_CREATE_DIST_PADDING:
            return transform_create_dist_padding_op;
        case OP_ECALL_TRANSFORM_INIT_COPY_INDEX:
            return transform_init_copy_index_op;
        case OP_ECALL_TRANSFORM_COMPUTE_ALIGNMENT_KEY:
            return transform_compute_alignment_key_op;
        default:
            return nullptr;
    }
}

static dual_entry_op_fn get_dual_entry_op_function(OpEcall op_type) {
    switch(op_type) {
        // Comparators
        case OP_ECALL_COMPARATOR_JOIN_ATTR:
            return comparator_join_attr_op;
        case OP_ECALL_COMPARATOR_PAIRWISE:
            return comparator_pairwise_op;
        case OP_ECALL_COMPARATOR_END_FIRST:
            return comparator_end_first_op;
        case OP_ECALL_COMPARATOR_JOIN_THEN_OTHER:
            return comparator_join_then_other_op;
        case OP_ECALL_COMPARATOR_ORIGINAL_INDEX:
            return comparator_original_index_op;
        case OP_ECALL_COMPARATOR_ALIGNMENT_KEY:
            return comparator_alignment_key_op;
        case OP_ECALL_COMPARATOR_PADDING_LAST:
            return comparator_padding_last_op;
        case OP_ECALL_COMPARATOR_DISTRIBUTE:
            return comparator_distribute_op;
        // Window operations
        case OP_ECALL_WINDOW_SET_ORIGINAL_INDEX:
            return window_set_original_index_op;
        case OP_ECALL_WINDOW_COMPUTE_LOCAL_SUM:
            return window_compute_local_sum_op;
        case OP_ECALL_WINDOW_COMPUTE_LOCAL_INTERVAL:
            return window_compute_local_interval_op;
        case OP_ECALL_WINDOW_COMPUTE_FOREIGN_SUM:
            return window_compute_foreign_sum_op;
        case OP_ECALL_WINDOW_COMPUTE_FOREIGN_INTERVAL:
            return window_compute_foreign_interval_op;
        case OP_ECALL_WINDOW_PROPAGATE_FOREIGN_INTERVAL:
            return window_propagate_foreign_interval_op;
        case OP_ECALL_WINDOW_COMPUTE_DST_IDX:
            return window_compute_dst_idx_op;
        case OP_ECALL_WINDOW_INCREMENT_INDEX:
            return window_increment_index_op;
        case OP_ECALL_WINDOW_EXPAND_COPY:
            return window_expand_copy_op;
        case OP_ECALL_WINDOW_UPDATE_COPY_INDEX:
            return window_update_copy_index_op;
        // Update operations
        case OP_ECALL_UPDATE_TARGET_MULTIPLICITY:
            return update_target_multiplicity_op;
        case OP_ECALL_UPDATE_TARGET_FINAL_MULTIPLICITY:
            return update_target_final_multiplicity_op;
        default:
            return nullptr;
    }
}

// Apply a single-entry transform op over entries[begin..end), in place.
// Shared by map_inplace and append_transformed - keeps the op-dispatch
// switch in one place.
static void apply_single_op_range(entry_t* data, size_t begin, size_t end,
                                   OpEcall op_type, int32_t* params) {
    if (op_type == OP_ECALL_TRANSFORM_TO_START || op_type == OP_ECALL_TRANSFORM_TO_END) {
        const int32_t deviation = params ? params[0] : 0;
        const equality_type_t equality = params ? (equality_type_t)params[1] : EQ;
        if (op_type == OP_ECALL_TRANSFORM_TO_START) {
            for (size_t i = begin; i < end; i++) transform_to_start_op(&data[i], deviation, equality);
        } else {
            for (size_t i = begin; i < end; i++) transform_to_end_op(&data[i], deviation, equality);
        }
    } else if (op_type == OP_ECALL_TRANSFORM_SET_INDEX ||
               op_type == OP_ECALL_TRANSFORM_SET_JOIN_ATTR ||
               op_type == OP_ECALL_INIT_METADATA_NULL) {
        const int32_t param = params ? params[0] : 0;
        if (op_type == OP_ECALL_TRANSFORM_SET_INDEX) {
            for (size_t i = begin; i < end; i++) transform_set_index_op(&data[i], (uint32_t)param);
        } else if (op_type == OP_ECALL_TRANSFORM_SET_JOIN_ATTR) {
            for (size_t i = begin; i < end; i++) transform_set_join_attr_op(&data[i], param);
        } else {
            for (size_t i = begin; i < end; i++) transform_init_metadata_null_op(&data[i], (uint32_t)param);
        }
    } else {
        single_op_fn func = get_single_op_function(op_type);
        if (!func) {
            throw std::runtime_error("Unknown single-entry operation type");
        }
        for (size_t i = begin; i < end; i++) func(&data[i]);
    }
}

void Table::map_inplace(OpEcall op_type, int32_t* params) {
    DEBUG_TRACE("Table::map_inplace: %zu entries, op_type=%d", entries.size(), op_type);
    apply_single_op_range(entries.data(), 0, entries.size(), op_type, params);
}

void Table::reserve(size_t n) {
    entries.reserve(n);
}

void Table::append_transformed(const Table& src, OpEcall op_type, int32_t* params) {
    const size_t start = entries.size();
    // One bulk insert: one allocation (if reserve was called ahead, none) +
    // one memcpy of src.entries.
    entries.insert(entries.end(), src.entries.begin(), src.entries.end());
    apply_single_op_range(entries.data(), start, entries.size(), op_type, params);
}

Table Table::map(OpEcall op_type, int32_t* params) const {
    // One bulk vector deep-copy (single allocation + memcpy of all entries)
    // followed by in-place transform - avoids the old push_back-with-growth
    // loop, which paid a per-entry reallocation cost on top of two 316-byte
    // copies per entry (entry->tmp->push_back). Now: ~316B per entry, once.
    Table result(*this);
    result.map_inplace(op_type, params);
    return result;
}

void Table::linear_pass(OpEcall op_type, int32_t* /* params */) {
    if (entries.size() < 2) return;

    DEBUG_TRACE("Table::linear_pass: Starting with %zu entries, op_type=%d", entries.size(), op_type);

    dual_entry_op_fn func = get_dual_entry_op_function(op_type);
    if (!func) {
        throw std::runtime_error("Unknown dual-entry operation type for linear_pass");
    }

    // Window operation: process adjacent pairs (direct pointers, zero copy)
    for (size_t i = 0; i < entries.size() - 1; i++) {
        func(&entries[i], &entries[i+1]);
    }

    DEBUG_TRACE("Table::linear_pass: Complete");
}

void Table::parallel_pass(Table& other, OpEcall op_type, int32_t* params) {
    if (entries.size() != other.entries.size()) {
        throw std::runtime_error("Tables must have same size for parallel_pass");
    }

    DEBUG_TRACE("Table::parallel_pass: Starting with %zu entries, op_type=%d", entries.size(), op_type);

    // Handle concat operation specially (has extra parameters)
    if (op_type == OP_ECALL_CONCAT_ATTRIBUTES) {
        int32_t left_attr_count = params ? params[0] : 0;
        int32_t right_attr_count = params ? params[1] : 0;

        for (size_t i = 0; i < entries.size(); i++) {
            concat_attributes_op(&entries[i], &other.entries[i], left_attr_count, right_attr_count);
        }
    } else {
        dual_entry_op_fn func = get_dual_entry_op_function(op_type);
        if (!func) {
            throw std::runtime_error("Unknown dual-entry operation type for parallel_pass");
        }

        for (size_t i = 0; i < entries.size(); i++) {
            func(&entries[i], &other.entries[i]);
        }
    }

    DEBUG_TRACE("Table::parallel_pass: Complete");
}

void Table::distribute_pass(size_t distance, OpEcall op_type, int32_t* /* params */) {
    DEBUG_TRACE("Table::distribute_pass: Starting with distance %zu, op_type=%d", distance, op_type);

    dual_entry_op_fn func = get_dual_entry_op_function(op_type);
    if (!func) {
        throw std::runtime_error("Unknown dual-entry operation type for distribute_pass");
    }

    // Process pairs at given distance (right to left to avoid underflow)
    for (size_t i = entries.size() - distance; i > 0; i--) {
        func(&entries[i - 1], &entries[i - 1 + distance]);
    }

    // Handle i = 0 separately
    if (distance < entries.size()) {
        func(&entries[0], &entries[distance]);
    }

    DEBUG_TRACE("Table::distribute_pass: Complete");
}

void Table::add_padding(size_t count, OpEcall padding_op) {
    if (count == 0) return;

    DEBUG_TRACE("Table::add_padding: Adding %zu padding entries", count);

    entries.reserve(entries.size() + count);

    single_op_fn func = get_single_op_function(padding_op);
    if (!func) {
        throw std::runtime_error("Unknown padding operation type");
    }

    for (size_t i = 0; i < count; i++) {
        entry_t padding_entry;
        memset(&padding_entry, 0, sizeof(entry_t));
        func(&padding_entry);
        entries.push_back(padding_entry);
    }

    DEBUG_TRACE("Table::add_padding: Complete - added %zu entries", count);
}


void Table::pad_to_shuffle_size() {
    size_t current_size = entries.size();
    size_t target_size = calculate_shuffle_padding(current_size);

    if (target_size > current_size) {
        size_t padding_count = target_size - current_size;
        DEBUG_INFO("Table::pad_to_shuffle_size: Padding from %zu to %zu (adding %zu entries)",
                   current_size, target_size, padding_count);

        // Add padding entries for shuffle sort
        add_padding(padding_count, OP_ECALL_TRANSFORM_SET_SORT_PADDING);
    }
}

size_t Table::calculate_shuffle_padding(size_t n) {
    // Waksman needs a power-of-2 array; no k-way decomposition any more.
    return next_power_of_two(n);
}

bool Table::is_valid_shuffle_size(size_t n) {
    // Valid shuffle size is any power of 2 (>= 1).
    return n > 0 && (n & (n - 1)) == 0;
}

void Table::shuffle_merge_sort(OpEcall op_type) {
    if (entries.size() <= 1) return;

    const size_t original_size = entries.size();
    DEBUG_INFO("Table::shuffle_merge_sort: Starting with %zu entries, op_type=%d",
               original_size, op_type);

    auto sms_t0 = SmsClock::now();
    g_sms_profile.calls += 1;
    g_sms_profile.input_rows += original_size;
    int op_bucket = (static_cast<int>(op_type) < N_OP_BUCKETS) ? static_cast<int>(op_type) : N_OP_BUCKETS - 1;
    g_sms_profile.per_op_calls[op_bucket] += 1;
    g_sms_profile.per_op_rows[op_bucket] += original_size;

    // SPIKE PATH: obligraph::o_sort for JOIN_ATTR (single-threaded). Bitonic
    // sort is itself oblivious (o_mem_swap is a CMOV-style conditional swap;
    // access pattern depends only on public n), so we skip the pad + Waksman
    // prefix entirely. The comparator (compare_join_attr) is already
    // branchless (oblivious_sign + arithmetic masks - see merge_comparators.c).
    size_t osort_T = (op_type == OP_ECALL_COMPARATOR_JOIN_ATTR) ? osort_thread_count() : 0;
    if (osort_T >= 1) {
        comparator_func_t cmp = get_merge_comparator(op_type);
        if (!cmp) {
            throw std::runtime_error("Unknown comparator op_type for shuffle_merge_sort");
        }
        // INDIRECT BITONIC: sort a uint32_t[] permutation, comparator
        // dereferences entries[i]. Swap cost is 4 B (uint32_t) instead of
        // sizeof(entry_t) ~316 B. After sort, materialize entries in place
        // via cycle-following (same trick as heap_sort).
        if (entries.size() > 0x7FFFFFFFu) {
            throw std::runtime_error("indirect o_sort spike: N exceeds 2^31");
        }
        const size_t N = entries.size();
        auto t = SmsClock::now();
        std::vector<uint32_t> perm(N);
        for (size_t i = 0; i < N; ++i) perm[i] = (uint32_t)i;

        entry_t* arr = entries.data();
        obligraph::o_sort<uint32_t>(
            perm.data(), 0, N,
            [cmp, arr](uint32_t i, uint32_t j) -> bool {
                return cmp(arr + i, arr + j) != 0;
            },
            spike_pool(), osort_T);

        // Cycle-following materialization. perm[i] = source slot for sorted
        // position i. Visited marker in high bit (N <= 2^31 enforced above).
        constexpr uint32_t VISITED = 0x80000000u;
        entry_t scratch;
        for (size_t i = 0; i < N; ++i) {
            if (perm[i] & VISITED) continue;
            if (perm[i] == (uint32_t)i) { perm[i] |= VISITED; continue; }
            size_t cur = i;
            scratch = arr[i];
            while (true) {
                uint32_t src = perm[cur];
                perm[cur] = src | VISITED;
                if ((size_t)src == i) { arr[cur] = scratch; break; }
                arr[cur] = arr[src];
                cur = src;
            }
        }
        double dt = sms_dur(t, SmsClock::now());

        // Sanity (cheap): verify sortedness.
        bool sorted_ok = true;
        for (size_t i = 1; i < N; ++i) {
            if (cmp(&entries[i], &entries[i-1]) != 0) { sorted_ok = false; break; }
        }
        if (!sorted_ok) {
            printf("OSORT_SANITY_FAIL (indirect): N=%zu T=%zu\n", N, osort_T);
        }

        g_sms_profile.osort_s += dt;
        g_sms_profile.osort_calls += 1;
        g_sms_profile.padded_rows += original_size; // no padding on this path
        g_sms_profile.total_s += sms_dur(sms_t0, SmsClock::now());
        g_sms_profile.per_op_time_s[op_bucket] += sms_dur(sms_t0, SmsClock::now());
        return;
    }

    // Phase 1: Pad once to next power of 2 (Waksman requirement).
    auto t = SmsClock::now();
    pad_to_shuffle_size();
    g_sms_profile.pad_s += sms_dur(t, SmsClock::now());
    g_sms_profile.padded_rows += entries.size();
    DEBUG_INFO("Table::shuffle_merge_sort: Padded to %zu entries", entries.size());

    // Phase 2: One in-place Waksman shuffle over the whole table.
    // Obliviousness: switch bits depend only on (rng_seed, level, position) -
    // all functions of public n. Failure here is non-recoverable.
    t = SmsClock::now();
    if (oblivious_2way_waksman(entries.data(), entries.size()) != 0) {
        throw std::runtime_error("oblivious_2way_waksman failed for n=" +
                                 std::to_string(entries.size()));
    }
    g_sms_profile.waksman_s += sms_dur(t, SmsClock::now());
    DEBUG_INFO("Table::shuffle_merge_sort: Shuffle phase complete");

    // Phase 3: One in-place comparison sort over the shuffled table.
    // Safe (info-theoretically oblivious) because input is now a uniformly
    // random permutation conditioned on public n.
    comparator_func_t cmp = get_merge_comparator(op_type);
    if (!cmp) {
        throw std::runtime_error("Unknown comparator op_type for shuffle_merge_sort");
    }
    t = SmsClock::now();
    heap_sort(entries.data(), entries.size(), cmp);
    g_sms_profile.heap_sort_s += sms_dur(t, SmsClock::now());
    DEBUG_INFO("Table::shuffle_merge_sort: Sort phase complete");

    // Phase 4: Drop padding (SORT_PADDING entries have JOIN_ATTR_POS_INF and
    // sort to the high end under join-attr-asc comparators).
    t = SmsClock::now();
    if (entries.size() > original_size) {
        entries.resize(original_size);
    }
    g_sms_profile.truncate_s += sms_dur(t, SmsClock::now());

    g_sms_profile.total_s += sms_dur(sms_t0, SmsClock::now());
    g_sms_profile.per_op_time_s[op_bucket] += sms_dur(sms_t0, SmsClock::now());

    DEBUG_INFO("Table::shuffle_merge_sort: Complete with %zu entries", entries.size());
}

