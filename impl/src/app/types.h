#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <vector>
#include <string>
#include <map>
#include <utility>
#include "../common/constants.h"
#include "../common/types_common.h"
#include "../enclave/enclave_types.h"

/**
 * Table Type Definitions and Schema Evolution
 * 
 * From thesis section 4.1.2 (Table Type Definitions):
 * "Following Krastnikov et al.'s terminology, we distinguish between different types
 * of tables based on their state in the algorithm:
 * 
 * - INPUT TABLES: Original unmodified tables {R1, R2, ..., Rk} as provided to the algorithm
 * - AUGMENTED TABLES: Input tables extended with persistent multiplicity metadata
 * - COMBINED TABLES: Arrays of entries from multiple augmented tables with temporary metadata,
 *   sorted by join attribute for dual-entry processing
 * - EXPANDED TABLES: Augmented tables where each tuple appears exactly final_mult times
 * - ALIGNED TABLES: Expanded tables reordered to enable correct concatenation for join result
 * 
 * Table Schema Evolution (from Table 4.1):
 * +----------+------------------+------------------+------------------+
 * | Type     | Original Attrs   | Persistent Meta  | Temporary Meta   |
 * +----------+------------------+------------------+------------------+
 * | R_input  | {a1, a2, ..., an}| No              | No               |
 * | R_aug    | {a1, a2, ..., an}| Yes             | No               |
 * | R_comb   | {type,a,data}    | Yes             | Yes              |
 * | R_exp    | {a1, a2, ..., an}| Yes             | No               |
 * | R_align  | {a1, a2, ..., an}| Yes             | No               |
 * +----------+------------------+------------------+------------------+
 * 
 * Persistent Meta: field_index, local_mult, final_mult, foreign_sum
 * Temporary Meta: local_cumsum OR foreign_cumsum (not both simultaneously)
 * 
 * Note: Combined tables have a special dual-entry structure where original
 * attributes are transformed into {field_type, join_attr, field_data} format"
 */

// Forward declaration
class Entry;
class Table;
class JoinCondition;

// C++ wrapper for entry_t
class Entry {
public:
    // Entry metadata
    entry_type_t field_type;
    equality_type_t equality_type;
    bool is_encrypted;
    
    // Encryption nonce for AES-CTR mode
    uint64_t nonce;
    
    // Join attribute
    int32_t join_attr;
    
    // Persistent metadata
    uint32_t original_index;
    uint32_t local_mult;
    uint32_t final_mult;
    uint32_t foreign_sum;
    
    // Temporary metadata
    uint32_t local_cumsum;
    uint32_t local_interval;
    uint32_t foreign_cumsum;
    uint32_t foreign_interval;
    uint32_t local_weight;
    
    // Expansion metadata
    uint32_t copy_index;
    uint32_t alignment_key;
    
    // Data attributes (all integers - using vector for flexibility)
    std::vector<int32_t> attributes;
    std::vector<std::string> column_names;
    
    // Constructors
    Entry();
    Entry(const entry_t& c_entry);
    
    // Conversion methods
    entry_t to_entry_t() const;
    void from_entry_t(const entry_t& c_entry);
    
    // Utility methods
    void clear();
    void encrypt();
    void decrypt();
    
    // Attribute access by column name
    int32_t get_attribute(const std::string& column_name) const;
    void set_attribute(const std::string& column_name, int32_t value);
    bool has_column(const std::string& column_name) const;
    
    // Add new attribute with column name
    void add_attribute(const std::string& column_name, int32_t value);
    
    // Get all attributes as a map (for convenience)
    std::map<std::string, int32_t> get_attributes_map() const;
    
    // Utility setters/getters
    void set_is_encrypted(bool encrypted) { is_encrypted = encrypted; }
    bool get_is_encrypted() const { return is_encrypted; }
};

// Table class for managing collections of entries
class Table {
private:
    std::vector<Entry> entries;
    std::string table_name;
    size_t num_columns;
    
public:
    // Constructors
    Table();
    Table(const std::string& name);
    
    // Entry management
    void add_entry(const Entry& entry);
    Entry& get_entry(size_t index);
    const Entry& get_entry(size_t index) const;
    void set_entry(size_t index, const Entry& entry);
    Entry& operator[](size_t index);
    const Entry& operator[](size_t index) const;
    size_t size() const;
    void clear();
    
    // Batch operations
    void set_all_field_type(entry_type_t type);
    void initialize_original_indices();
    void initialize_leaf_multiplicities();
    
    // Conversion for SGX processing
    std::vector<entry_t> to_entry_t_vector() const;
    void from_entry_t_vector(const std::vector<entry_t>& c_entries);
    
    // Table metadata
    void set_table_name(const std::string& name);
    std::string get_table_name() const;
    void set_num_columns(size_t n);
    size_t get_num_columns() const;
    
    // Iterator support
    std::vector<Entry>::iterator begin();
    std::vector<Entry>::iterator end();
    std::vector<Entry>::const_iterator begin() const;
    std::vector<Entry>::const_iterator end() const;
    
    // Encryption status
    enum EncryptionStatus {
        UNENCRYPTED,  // All entries have is_encrypted = false
        ENCRYPTED,    // All entries have is_encrypted = true
        MIXED         // Entries have different encryption states
    };
    EncryptionStatus get_encryption_status() const;
};

/**
 * Join Condition Encoding
 * 
 * From thesis section 4.1.4 (Join Condition Encoding):
 * "Any join condition between columns can be expressed as an interval constraint.
 * Specifically, a condition between parent column v.join_attr and child column
 * c.join_attr can be parsed as: c.join_attr in v.join_attr + [x, y], where the
 * interval [x, y] may use open or closed boundaries and x, y in R U {±∞}.
 * 
 * Sample join predicates map to intervals as follows:
 * - Equality: v.join_attr = c.join_attr maps to c.join_attr in v.join_attr + [0, 0]
 * - Inequality: v.join_attr > c.join_attr maps to c.join_attr in v.join_attr + (-∞, 0)
 * - Band constraint: v.join_attr >= c.join_attr - 1 maps to c.join_attr in v.join_attr + [-1, ∞)
 * 
 * When multiple conditions constrain the same join, we compute their interval intersection.
 * For instance, combining v.join_attr > c.join_attr (yielding (-∞, 0)) with
 * v.join_attr <= c.join_attr + 1 (yielding [-1, ∞)) produces the final interval [-1, 0).
 * 
 * The constraint function operationalizes this interval representation by mapping each
 * parent-child relationship to boundary parameters ((deviation1, equality1), (deviation2, equality2)).
 * This encoding is fundamental to the dual-entry technique used throughout the algorithm."
 */

// Join condition class (app-side only)
class JoinCondition {
public:
    // Interval bounds for band join
    struct Bound {
        double deviation;           // Offset from join attribute
        equality_type_t equality;   // EQ for closed, NEQ for open
        
        Bound() : deviation(0.0), equality(NONE) {}
        Bound(double d, equality_type_t e) : deviation(d), equality(e) {}
    };
    
private:
    std::string parent_table;
    std::string child_table;
    std::string parent_column;
    std::string child_column;
    Bound lower_bound;    // Start boundary
    Bound upper_bound;    // End boundary
    
public:
    // Constructors
    JoinCondition();
    JoinCondition(const std::string& parent_tbl, const std::string& child_tbl,
                  const std::string& parent_col, const std::string& child_col,
                  const Bound& lower, const Bound& upper);
    
    // Factory methods for common conditions
    static JoinCondition equality(const std::string& parent_tbl, const std::string& child_tbl,
                                  const std::string& parent_col, const std::string& child_col);
    static JoinCondition band(const std::string& parent_tbl, const std::string& child_tbl,
                             const std::string& parent_col, const std::string& child_col,
                             double lower_offset, double upper_offset,
                             bool lower_inclusive = true, bool upper_inclusive = true);
    
    // Getters
    const Bound& get_lower_bound() const { return lower_bound; }
    const Bound& get_upper_bound() const { return upper_bound; }
    std::string get_parent_table() const { return parent_table; }
    std::string get_child_table() const { return child_table; }
    std::string get_parent_column() const { return parent_column; }
    std::string get_child_column() const { return child_column; }
    
    // Apply condition to create START/END entries
    std::pair<Entry, Entry> create_boundary_entries(const Entry& target_entry) const;
};

#endif // APP_TYPES_H