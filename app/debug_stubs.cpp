#include "debug_util.h"
#include "data_structures/table.h"
#include <vector>

/**
 * Debug stub implementations for TDX migration
 * These functions are no-ops in production builds
 * Can be implemented later if debug output is needed
 */

void debug_init_session(const char* session_name) {
    // TDX: Debug stub - no-op
    (void)session_name;
}

void debug_close_session() {
    // TDX: Debug stub - no-op
}

void debug_dump_table(const Table& table, const char* table_name, const char* phase) {
    // TDX: Debug stub - no-op
    (void)table;
    (void)table_name;
    (void)phase;
}

void debug_dump_table(const Table& table, const char* label, const char* step_name, uint32_t eid,
                      const std::vector<MetadataColumn>& metadata_columns, bool show_padding) {
    // TDX: Debug stub - no-op
    (void)table;
    (void)label;
    (void)step_name;
    (void)eid;
    (void)metadata_columns;
    (void)show_padding;
}

void debug_dump_with_mask(const Table& table, const char* label, const char* step_name,
                          uint32_t eid, uint32_t mask) {
    // TDX: Debug stub - no-op
    (void)table;
    (void)label;
    (void)step_name;
    (void)eid;
    (void)mask;
}
