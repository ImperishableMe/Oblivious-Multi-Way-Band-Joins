#include "definitions.h"
#include "obl_primitives.h"
#include "obl_building_blocks.h"
#include "timer.h"
#include "config.h"

namespace obligraph {

    Table Table::project(const vector<string>& columnNames, ThreadPool &pool) const {
        ScopedTimer timer("Projecting Table: " + this->name);
        
        // Create new table for projection
        Table projectedTable;
        projectedTable.name = this->name + "_projected";
        projectedTable.type = this->type;
        projectedTable.rowCount = this->rowCount;
        
        // Collect requested columns only
        vector<string> finalColumnNames;
        vector<const ColumnMeta*> columnMetas;
        
        // Add requested columns
        for (const string& colName : columnNames) {
            // Check if column exists in original schema
            bool found = false;
            const ColumnMeta* foundMeta = nullptr;
            for (const auto& meta : this->schema.columnMetas) {
                if (meta.name == colName) {
                    found = true;
                    foundMeta = &meta;
                    break;
                }
            }
            
            if (!found) {
                throw runtime_error("Column '" + colName + "' not found in table schema");
            }
            
            // Check if it's already added (avoid duplicates)
            bool alreadyAdded = false;
            for (const string& existing : finalColumnNames) {
                if (existing == colName) {
                    alreadyAdded = true;
                    break;
                }
            }
            
            if (!alreadyAdded) {
                finalColumnNames.push_back(colName);
                columnMetas.push_back(foundMeta);
            }
        }
        
        // Build new schema with updated offsets
        size_t currentOffset = 0;
        for (const ColumnMeta* originalMeta : columnMetas) {
            ColumnMeta newMeta = *originalMeta;  // Copy original metadata
            newMeta.offset = currentOffset;      // Update offset for new layout
            
            // Use the same size as the original column
            newMeta.size = originalMeta->size;
            
            projectedTable.schema.columnMetas.push_back(newMeta);
            currentOffset += originalMeta->size;
        }
        
        // Copy primary keys
        projectedTable.primaryKeys = this->primaryKeys;
        projectedTable.rows.resize(this->rows.size());
        
        size_t newRowSize = currentOffset;
        // Validate that the new row size doesn't exceed maximum
        if (newRowSize > ROW_DATA_MAX_SIZE) {
            throw runtime_error("Projected row size (" + to_string(newRowSize) + " bytes) exceeds maximum allowed size (" + to_string(ROW_DATA_MAX_SIZE) + " bytes)");
        }

        auto thread_chunk = [&](int start, int end) {
            for (int i = start; i < end; i++) {
                auto &originalRow = this->rows[i];
                Row newRow;
                newRow.size = newRowSize;
                newRow.key = originalRow.key;  // Preserve row key

                // Copy data for each projected column
                size_t newOffset = 0;
                for (const ColumnMeta* originalMeta : columnMetas) {
                    // Copy data from original row to new row
                    const char* srcPtr = originalRow.data + originalMeta->offset;
                    char* destPtr = newRow.data + newOffset;
                    memcpy(destPtr, srcPtr, originalMeta->size);
                    newOffset += originalMeta->size;
                }
                projectedTable.rows[i] = newRow;
            }
        };

        int num_threads = obligraph::number_of_threads.load();
        std::vector <std::future<void>> futures;

        for (int i = 0; i < num_threads; i++) {
            auto chunks = obligraph::get_cutoffs_for_thread(i, this->rows.size(), num_threads);
            if (chunks.first == chunks.second) continue; // no work for this thread
            if (i == num_threads - 1) {
                // Last thread takes any remaining rows
                thread_chunk(chunks.first, chunks.second);
            }
            else {
                futures.push_back(pool.submit(thread_chunk, chunks.first, chunks.second));
            }
        }

        for (auto& fut : futures) {
            fut.get();
        }

        return projectedTable;
    }
    void Table::filter(const vector<Predicate>& predicates, ThreadPool& pool) {
        ScopedTimer timer("Filtering Table: " + this->name);

        vector<uint8_t> rowPasses(this->rows.size(), true);

        auto thread_chunk = [&](int start, int end) {
            for (int i = start; i < end; i++) {
                for (const auto& predicate : predicates) {
                    // TODO: find the offsets of the predicates first and then get the column value with the offsets instead
                    // of looping over everytime 

                    ColumnValue columnValue = this->rows[i].getColumnValue(predicate.column, this->schema);
                    bool passes = false;

                    switch (predicate.op) {
                        case Predicate::Cmp::EQ:
                            passes = (columnValue == predicate.constant);
                            break;
                        case Predicate::Cmp::GT:
                            passes = (columnValue > predicate.constant);
                            break;
                        case Predicate::Cmp::LT:
                            passes = (columnValue < predicate.constant);
                            break;
                        case Predicate::Cmp::GTE:
                            passes = (columnValue >= predicate.constant);
                            break;
                        case Predicate::Cmp::LTE:
                            passes = (columnValue <= predicate.constant);
                            break;
                    }
                    rowPasses[i] = rowPasses[i] && passes;  // AND with previous results
                }
            }
        };

        int num_threads = obligraph::number_of_threads.load();
        std::vector <std::future<void>> futures;

        for (int i = 0; i < num_threads; i++) {
            auto chunks = obligraph::get_cutoffs_for_thread(i, this->rows.size(), num_threads);
            if (chunks.first == chunks.second) continue; // no work for this thread
            if (i == num_threads - 1) {
                // Last thread takes any remaining rows
                thread_chunk(chunks.first, chunks.second);
            }
            else {
                futures.push_back(pool.submit(thread_chunk, chunks.first, chunks.second));
            }
        }

        for (auto& fut : futures) {
            fut.get();
        }

        int filteredCount = 0;
        {
            ScopedTimer timer("Oblivious Compact");
            filteredCount = obligraph::parallel_o_compact(this->rows.begin(), this->rows.end(), pool, rowPasses.data(), pool.size());
        }
        this->rows.resize(filteredCount);
        this->rowCount = filteredCount;
    }

    void Table::unionWith(const Table& other, ThreadPool& pool, const string& columnPrefix) {
        ScopedTimer timer("Union Table: " + this->name + " with " + other.name);
        // Verify both tables have the same number of rows
        if (this->rowCount != other.rowCount) {
            // show the table names and row counts
            cerr << "Error: Cannot union tables with different row counts: "
                 << this->name << " (" << this->rowCount << " rows) vs "
                 << other.name << " (" << other.rowCount << " rows)" << endl;
            throw runtime_error("Cannot union tables with different row counts: " +
                              to_string(this->rowCount) + " vs " + to_string(other.rowCount));
        }

        // Find columns in the other table that don't exist in this table
        // If columnPrefix is provided, ALL columns from 'other' get added with the prefix
        vector<const ColumnMeta*> newColumns;
        vector<string> newColumnNames;  // Store the final (possibly prefixed) names

        for (const auto& otherMeta : other.schema.columnMetas) {
            // Determine the column name (with optional prefix)
            string finalName = columnPrefix.empty() ? otherMeta.name : columnPrefix + "_" + otherMeta.name;

            bool existsInThis = false;
            for (const auto& thisMeta : this->schema.columnMetas) {
                if (thisMeta.name == finalName) {
                    existsInThis = true;
                    break;
                }
            }
            if (!existsInThis) {
                newColumns.push_back(&otherMeta);
                newColumnNames.push_back(finalName);
            }
        }

        // If no new columns to add, nothing to do
        if (newColumns.empty()) {
            return;
        }

        // Calculate the total size needed for the expanded rows
        size_t additionalSize = 0;
        for (const auto* newCol : newColumns) {
            additionalSize += newCol->size;
        }

        // Check if the expanded row size would exceed the maximum
        size_t currentRowSize = 0;
        if (!this->rows.empty()) {
            currentRowSize = this->rows[0].size;
        }
        size_t newRowSize = currentRowSize + additionalSize;
        
        if (newRowSize > ROW_DATA_MAX_SIZE) {
            throw runtime_error("Union would result in row size (" + to_string(newRowSize) + 
                              " bytes) exceeding maximum allowed size (" + to_string(ROW_DATA_MAX_SIZE) + " bytes)");
        }

        // Add new columns to the schema
        size_t currentOffset = currentRowSize;
        for (size_t i = 0; i < newColumns.size(); i++) {
            const auto* newCol = newColumns[i];
            ColumnMeta newMeta = *newCol;  // Copy the column metadata
            newMeta.name = newColumnNames[i];  // Use the prefixed name
            newMeta.offset = currentOffset;  // Update offset for our schema
            this->schema.columnMetas.push_back(newMeta);
            currentOffset += newCol->size;
        }

        auto thread_chunk = [&](int start, int end) {
            for (size_t rowIdx = start; rowIdx < end; ++rowIdx) {
                Row& thisRow = this->rows[rowIdx];
                const Row& otherRow = other.rows[rowIdx];

                // Expand the row data
                size_t oldSize = thisRow.size;
                thisRow.size = newRowSize;
                
                // Copy data for each new column from the corresponding row in the other table
                size_t targetOffset = oldSize;
                for (const auto* newCol : newColumns) {
                    // Find the data in the other table's row
                    const char* srcPtr = otherRow.data + newCol->offset;
                    char* destPtr = thisRow.data + targetOffset;
                    
                    // Copy the data
                    memcpy(destPtr, srcPtr, newCol->size);
                    targetOffset += newCol->size;
                }
            }
        };

        int num_threads = obligraph::number_of_threads.load();
        std::vector <std::future<void>> futures;

        for (int i = 0; i < num_threads; i++) {
            auto chunks = obligraph::get_cutoffs_for_thread(i, this->rows.size(), num_threads);
            if (chunks.first == chunks.second) continue; // no work for this thread
            if (i == num_threads - 1) {
                // Last thread takes any remaining rows
                thread_chunk(chunks.first, chunks.second);
            }
            else {
                futures.push_back(pool.submit(thread_chunk, chunks.first, chunks.second));
            }
        }

        for (auto& fut : futures) {
            fut.get();
        }
    }

}; // namespace obligraph
