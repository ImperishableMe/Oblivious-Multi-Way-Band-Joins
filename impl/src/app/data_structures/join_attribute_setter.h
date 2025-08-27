#ifndef JOIN_ATTRIBUTE_SETTER_H
#define JOIN_ATTRIBUTE_SETTER_H

#include <memory>
#include <string>
#include "join_tree_node.h"
#include "../../common/debug_util.h"

/**
 * JoinAttributeSetter - Sets join_attr field for all entries in the join tree
 * 
 * After the join tree is built, each node knows its join column name,
 * but the Entry objects don't have their join_attr values set.
 * This utility populates join_attr from the appropriate column data.
 */
class JoinAttributeSetter {
public:
    /**
     * Set join attributes for entire tree
     * @param root The root of the join tree
     */
    static void SetJoinAttributesForTree(JoinTreeNodePtr root);

private:
    /**
     * Set join attributes for a single node
     * @param node The node to process
     */
    static void SetJoinAttributesForNode(JoinTreeNodePtr node);
    
    /**
     * Extract numeric value from attribute by column name
     * @param entry The entry to extract from
     * @param column_name The column to extract
     * @return The numeric value (converts string to double if needed)
     */
    static double ExtractColumnValue(const Entry& entry, const std::string& column_name);
};

#endif // JOIN_ATTRIBUTE_SETTER_H