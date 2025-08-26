#ifndef PHASE3_DISTRIBUTE_EXPAND_H
#define PHASE3_DISTRIBUTE_EXPAND_H

#include <vector>
#include "../types.h"
#include "../../enclave/enclave_types.h"

/**
 * PHASE 3: DISTRIBUTION AND EXPANSION
 * ====================================
 * 
 * From thesis section 4.5:
 * "Each tuple must be replicated according to its final multiplicity ±_final. We use
 * the oblivious distribute-and-expand technique from ODBJ [Krastnikov et al. 2020],
 * which creates exactly ±_final copies of each tuple while maintaining oblivious
 * access patterns."
 * 
 * KEY PROPERTY:
 * "This technique first distributes tuples to their target positions, then expands
 * them to fill the required space. The key property is that the expansion is
 * data-oblivious: the access pattern depends only on the multiplicities, not on
 * the actual data values."
 * 
 * OBLIVIOUS PROPERTIES:
 * The expansion maintains obliviousness through:
 * 1. Fixed access patterns based only on multiplicities
 * 2. No data-dependent branching
 * 3. Predetermined distribution positions
 * 4. Systematic gap-filling strategy
 */

/**
 * OBLIVIOUS EXPAND PRIMITIVE
 * 
 * From ODBJ framework:
 * "ObliviousExpand duplicates each tuple according to its multiplicity, creating an
 * expanded table where each original tuple appears the specified number of times."
 * 
 * INPUT: Augmented table R where each tuple t has t.final_mult set
 * OUTPUT: Expanded table R_exp where each tuple t appears exactly t.final_mult times
 * 
 * ALGORITHM OVERVIEW:
 * 1. Compute output size: |R_exp| = £{t  R} t.final_mult
 * 2. Allocate output array of size |R_exp|
 * 3. Distribute phase: Place one copy of each tuple at strategic positions
 * 4. Expand phase: Fill gaps between distributed copies
 * 
 * The distribution strategy ensures that copies of the same tuple are spread evenly
 * across the output array, enabling parallel expansion without conflicts.
 * 
 * COMPLEXITY: O(|R| + |R_exp|) where |R_exp| = £ t.final_mult
 */
Table oblivious_expand(const Table& table);

/**
 * COPY INDEX ASSIGNMENT
 * 
 * During expansion, each copy of a tuple is assigned a copy_index:
 * - Original tuple t has final_mult copies
 * - Each copy gets copy_index  [0, final_mult - 1]
 * - The copy_index is crucial for alignment in Phase 4
 * 
 * Example: If tuple t has final_mult = 3:
 * - First copy: t with copy_index = 0
 * - Second copy: t with copy_index = 1
 * - Third copy: t with copy_index = 2
 * 
 * This function assigns copy indices to all entries in an expanded table
 */
void assign_copy_indices(Table& expanded_table);

/**
 * Phase 3 main function: Expand all tables in the join tree
 * 
 * Prerequisites from Phase 2:
 * - Each tuple has final_mult computed
 * - Metadata fields are preserved during expansion
 * 
 * Preparation for Phase 4:
 * - copy_index assigned to each expanded tuple
 * - foreign_sum preserved for alignment computation
 * - local_mult preserved for alignment formula
 * 
 * MEMORY REQUIREMENTS:
 * The expanded tables can be significantly larger than input tables:
 * - Input size: £_{v  V} |R_v|
 * - Output size: |Result| = £_{v  V} £_{t  R_v} t.final_mult
 * 
 * The output size equals the size of the final join result, which can be
 * as large as the Cartesian product in worst case.
 */
void distribute_expand_phase(std::vector<Table>& tables);

/**
 * Helper: Compute total size after expansion
 * Returns £{t  table} t.final_mult
 */
size_t compute_expanded_size(const Table& table);

/**
 * Helper: Distribute initial copies
 * Places one copy of each tuple at strategic positions
 * to enable efficient parallel expansion
 */
void distribute_tuples(const Table& input, Table& output);

/**
 * Helper: Expand to fill gaps
 * Fills remaining positions with appropriate tuple copies
 * maintaining oblivious access patterns
 */
void expand_gaps(Table& table);

#endif // PHASE3_DISTRIBUTE_EXPAND_H