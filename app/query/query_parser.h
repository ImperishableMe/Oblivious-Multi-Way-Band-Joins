#ifndef QUERY_PARSER_H
#define QUERY_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <optional>
#include "query_tokenizer.h"
#include "parsed_query.h"
#include "inequality_parser.h"
#include "condition_merger.h"

/**
 * QueryParser - Parses SQL queries into structured representation
 * 
 * Handles TPC-H style SELECT * queries with multiple tables and join conditions.
 * Uses QueryTokenizer for lexical analysis and InequalityParser for conditions.
 */
class QueryParser {
private:
    QueryTokenizer tokenizer;
    std::vector<Token> tokens;
    size_t current_token_index;
    
    /**
     * Get current token
     */
    const Token& current() const;
    
    /**
     * Get next token without consuming
     */
    const Token& peek() const;
    
    /**
     * Consume current token and move to next
     */
    void consume();
    
    /**
     * Check if current token matches expected type
     */
    bool match(TokenType type) const;
    
    /**
     * Consume token of expected type or throw error
     */
    void expect(TokenType type, const std::string& error_msg);
    
    /**
     * Check if at end of query
     */
    bool is_at_end() const;
    
    /**
     * Parse SELECT clause
     */
    void parse_select(ParsedQuery& query);
    
    /**
     * Parse FROM clause
     */
    void parse_from(ParsedQuery& query);
    
    /**
     * Parse WHERE clause
     */
    void parse_where(ParsedQuery& query);
    
    /**
     * Parse a single condition from WHERE clause
     * Returns the raw condition string
     */
    std::string parse_single_condition();
    
    /**
     * Combine all join conditions between each pair of tables into one
     * constraint.
     *
     * Conditions on the same column pair are intersected (a band join written
     * as a lower and an upper bound). Conditions on different column pairs
     * become a composite constraint: any number of equalities plus at most one
     * range. Throws ParseException for combinations the join cannot evaluate
     * (two ranges on different column pairs, contradictory bounds, '<>',
     * more equalities than MAX_JOIN_EQ allows, a column compared with another
     * column of the same table).
     */
    void merge_join_conditions(std::vector<JoinConstraint>& conditions);
    
public:
    /**
     * Parse a SQL query string
     * Returns ParsedQuery structure or throws on error
     */
    ParsedQuery parse(const std::string& sql_query);
    
    /**
     * Reset parser state
     */
    void reset();
};

/**
 * Exception class for parse errors
 */
class ParseException : public std::runtime_error {
public:
    ParseException(const std::string& msg) : std::runtime_error(msg) {}
};

#endif // QUERY_PARSER_H