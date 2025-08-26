#include "types.h"
#include <cstring>
#include <algorithm>

//////////////////////////////////////////////////////////////////////////////
// Entry Implementation
//////////////////////////////////////////////////////////////////////////////

Entry::Entry() 
    : field_type(SOURCE), 
      equality_type(EQ), 
      is_encrypted(false),
      join_attr(0.0), 
      original_index(0), 
      local_mult(0), 
      final_mult(0),
      foreign_sum(0), 
      local_cumsum(0), 
      local_interval(0),
      foreign_cumsum(0), 
      foreign_interval(0), 
      local_weight(0),
      copy_index(0), 
      alignment_key(0) {
}

Entry::Entry(const entry_t& c_entry) {
    from_entry_t(c_entry);
}

entry_t Entry::to_entry_t() const {
    entry_t result;
    memset(&result, 0, sizeof(entry_t));
    
    // Copy basic fields
    result.field_type = field_type;
    result.equality_type = equality_type;
    result.is_encrypted = is_encrypted;
    result.nonce = nonce;  // Don't forget the nonce!
    result.join_attr = join_attr;
    result.original_index = original_index;
    result.local_mult = local_mult;
    result.final_mult = final_mult;
    result.foreign_sum = foreign_sum;
    result.local_cumsum = local_cumsum;
    result.local_interval = local_interval;
    result.foreign_cumsum = foreign_cumsum;
    result.foreign_interval = foreign_interval;
    result.local_weight = local_weight;
    result.copy_index = copy_index;
    result.alignment_key = alignment_key;
    
    // Copy attributes (up to MAX_ATTRIBUTES)
    size_t attr_count = std::min(attributes.size(), (size_t)MAX_ATTRIBUTES);
    for (size_t i = 0; i < attr_count; i++) {
        result.attributes[i] = attributes[i];
    }
    
    // Copy column names (up to MAX_ATTRIBUTES)
    size_t col_count = std::min(column_names.size(), (size_t)MAX_ATTRIBUTES);
    for (size_t i = 0; i < col_count; i++) {
        strncpy(result.column_names[i], column_names[i].c_str(), MAX_COLUMN_NAME_LEN - 1);
        result.column_names[i][MAX_COLUMN_NAME_LEN - 1] = '\0';
    }
    
    return result;
}

void Entry::from_entry_t(const entry_t& c_entry) {
    field_type = c_entry.field_type;
    equality_type = c_entry.equality_type;
    is_encrypted = c_entry.is_encrypted;
    nonce = c_entry.nonce;  // Copy nonce too!
    join_attr = c_entry.join_attr;
    original_index = c_entry.original_index;
    local_mult = c_entry.local_mult;
    final_mult = c_entry.final_mult;
    foreign_sum = c_entry.foreign_sum;
    local_cumsum = c_entry.local_cumsum;
    local_interval = c_entry.local_interval;
    foreign_cumsum = c_entry.foreign_cumsum;
    foreign_interval = c_entry.foreign_interval;
    local_weight = c_entry.local_weight;
    copy_index = c_entry.copy_index;
    alignment_key = c_entry.alignment_key;
    
    // Clear and copy column names first to determine actual column count
    column_names.clear();
    int actual_columns = 0;
    for (int i = 0; i < MAX_ATTRIBUTES; i++) {
        if (strlen(c_entry.column_names[i]) == 0) {
            // Stop at first empty column name
            break;
        }
        column_names.push_back(std::string(c_entry.column_names[i]));
        actual_columns++;
    }
    
    // Clear and copy only the actual number of attributes
    attributes.clear();
    for (int i = 0; i < actual_columns && i < MAX_ATTRIBUTES; i++) {
        attributes.push_back(c_entry.attributes[i]);
    }
}

void Entry::clear() {
    *this = Entry();  // Reset to default values
}

void Entry::encrypt() {
    // Placeholder - actual encryption happens in SGX
    is_encrypted = true;
}

void Entry::decrypt() {
    // Placeholder - actual decryption happens in SGX
    is_encrypted = false;
}

//////////////////////////////////////////////////////////////////////////////
// Table Implementation
//////////////////////////////////////////////////////////////////////////////

Table::Table() : num_columns(0) {
}

Table::Table(const std::string& name) : table_name(name), num_columns(0) {
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
        entries[i].original_index = i;
    }
}

void Table::initialize_leaf_multiplicities() {
    for (auto& entry : entries) {
        entry.local_mult = 1;
        entry.final_mult = 1;
    }
}

std::vector<entry_t> Table::to_entry_t_vector() const {
    std::vector<entry_t> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        result.push_back(entry.to_entry_t());
    }
    return result;
}

void Table::from_entry_t_vector(const std::vector<entry_t>& c_entries) {
    entries.clear();
    entries.reserve(c_entries.size());
    for (const auto& c_entry : c_entries) {
        entries.emplace_back(c_entry);
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
    if (entries.empty()) {
        return UNENCRYPTED;  // Empty table is considered unencrypted
    }
    
    bool first_is_encrypted = entries[0].is_encrypted;
    
    for (size_t i = 1; i < entries.size(); ++i) {
        if (entries[i].is_encrypted != first_is_encrypted) {
            return MIXED;  // Found a mismatch
        }
    }
    
    return first_is_encrypted ? ENCRYPTED : UNENCRYPTED;
}

//////////////////////////////////////////////////////////////////////////////
// JoinCondition Implementation
//////////////////////////////////////////////////////////////////////////////

JoinCondition::JoinCondition() {
}

JoinCondition::JoinCondition(const std::string& parent_tbl, const std::string& child_tbl,
                           const std::string& parent_col, const std::string& child_col,
                           const Bound& lower, const Bound& upper)
    : parent_table(parent_tbl), child_table(child_tbl),
      parent_column(parent_col), child_column(child_col),
      lower_bound(lower), upper_bound(upper) {
}

JoinCondition JoinCondition::equality(const std::string& parent_tbl, const std::string& child_tbl,
                                     const std::string& parent_col, const std::string& child_col) {
    return JoinCondition(parent_tbl, child_tbl, parent_col, child_col,
                        Bound(0.0, EQ), Bound(0.0, EQ));
}

JoinCondition JoinCondition::band(const std::string& parent_tbl, const std::string& child_tbl,
                                 const std::string& parent_col, const std::string& child_col,
                                 double lower_offset, double upper_offset,
                                 bool lower_inclusive, bool upper_inclusive) {
    equality_type_t lower_eq = lower_inclusive ? EQ : NEQ;
    equality_type_t upper_eq = upper_inclusive ? EQ : NEQ;
    return JoinCondition(parent_tbl, child_tbl, parent_col, child_col,
                        Bound(lower_offset, lower_eq), Bound(upper_offset, upper_eq));
}

std::pair<Entry, Entry> JoinCondition::create_boundary_entries(const Entry& target_entry) const {
    Entry start_entry = target_entry;
    Entry end_entry = target_entry;
    
    // Set field types
    start_entry.field_type = START;
    end_entry.field_type = END;
    
    // Apply bounds
    start_entry.join_attr += lower_bound.deviation;
    start_entry.equality_type = lower_bound.equality;
    
    end_entry.join_attr += upper_bound.deviation;
    end_entry.equality_type = upper_bound.equality;
    
    return std::make_pair(start_entry, end_entry);
}