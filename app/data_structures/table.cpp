#include "table.h"
#include <stdexcept>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <thread>
#include "debug_util.h"
#include "../core_logic/core.h"
#include "../core_logic/algorithms/oblivious_waksman.h"
#include "../core_logic/algorithms/min_heap.h"
#include "entry_oblivious_ops.h"   // obligraph::o_mem_swap<entry_t> specialization
                                    // (also transitively pulls obl_building_blocks.h
                                    //  with parallel_sort, ThreadPool)

// Comparator dispatch lives in app/core_logic/operations/merge_comparators.c
extern "C" comparator_func_t get_merge_comparator(OpEcall type);

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

    comparator_func_t cmp = get_merge_comparator(op_type);
    if (!cmp) {
        throw std::runtime_error("Unknown comparator op_type for shuffle_merge_sort");
    }

    // Thread count: OBL_MWJ_SORT_THREADS overrides; default to hardware concurrency.
    // Read once per process via a magic-static initializer.
    static const size_t sort_threads = []() -> size_t {
        if (const char* e = std::getenv("OBL_MWJ_SORT_THREADS")) {
            long v = std::atol(e);
            if (v >= 1) return static_cast<size_t>(v);
        }
        unsigned hw = std::thread::hardware_concurrency();
        return hw > 0 ? hw : 1;
    }();

    // ThreadPool constructed once on first call, shared across every sort.
    static obligraph::ThreadPool pool(sort_threads);

    // Bitonic sort over the entries. parallel_sort handles arbitrary n (uses
    // greatest_power_of_two_less_than internally), so no pad / no truncate.
    // The network topology is data-independent; o_mem_swap<entry_t> is
    // branchless (AVX2 vpblendvb). Structurally oblivious — no pre-shuffle.
    obligraph::parallel_sort(
        entries.begin(), entries.end(),
        pool,
        // C-style comparator cmp returns 1 iff a < b; bitonic Compare wants a < b.
        [cmp](const Entry& a, const Entry& b) -> bool {
            return cmp(const_cast<entry_t*>(&a),
                       const_cast<entry_t*>(&b)) != 0;
        },
        sort_threads);
}

