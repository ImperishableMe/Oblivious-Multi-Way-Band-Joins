/*
 * Debug function stubs
 * These functions are declared in debug_util.h but were never fully implemented.
 * Providing empty stubs to allow linking.
 */

#include "debug_util.h"
#include "data_structures/table.h"
#include "join/join_tree_node.h"
#include <vector>
#include <string>

// Session management stubs
void debug_init_session(const char* session_name) {
    (void)session_name;
    // No-op stub
}

void debug_close_session() {
    // No-op stub
}

// Table dumping stubs
void debug_dump_table(const Table& table, const char* label, const char* step_name, uint32_t eid,
                      const std::vector<MetadataColumn>& columns, bool include_attributes) {
    (void)table;
    (void)label;
    (void)step_name;
    (void)eid;
    (void)columns;
    (void)include_attributes;
    // No-op stub
}

void debug_dump_entry(const Entry& entry, const char* label, uint32_t eid) {
    (void)entry;
    (void)label;
    (void)eid;
    // No-op stub
}

void debug_dump_selected_columns(const Table& table, const char* label, const char* step_name,
                                 uint32_t eid, const std::vector<std::string>& columns) {
    (void)table;
    (void)label;
    (void)step_name;
    (void)eid;
    (void)columns;
    // No-op stub
}

void debug_dump_with_mask(const Table& table, const char* label, const char* step_name,
                          uint32_t eid, uint32_t column_mask) {
    (void)table;
    (void)label;
    (void)step_name;
    (void)eid;
    (void)column_mask;
    // No-op stub
}

// File output stubs
void debug_to_file(const char* filename, const char* content) {
    (void)filename;
    (void)content;
    // No-op stub
}

void debug_append_to_file(const char* filename, const char* content) {
    (void)filename;
    (void)content;
    // No-op stub
}

// Encryption consistency assertions
uint8_t AssertConsistentEncryption(const Table& table) {
    // Check if all entries have the same encryption status
    if (table.size() == 0) return 0;

    uint8_t first_status = table.get_entry(0).is_encrypted ? 1 : 0;
    for (size_t i = 1; i < table.size(); i++) {
        uint8_t current_status = table.get_entry(i).is_encrypted ? 1 : 0;
        if (current_status != first_status) {
            throw std::runtime_error("Inconsistent encryption status in table");
        }
    }
    return first_status;
}

void AssertTreeConsistentEncryption(JoinTreeNodePtr root) {
    if (!root) return;

    // Recursively check all nodes
    AssertConsistentEncryption(root->get_table());

    for (const auto& child : root->get_children()) {
        if (child) {
            AssertTreeConsistentEncryption(child);
        }
    }
}
