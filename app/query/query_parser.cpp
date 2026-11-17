#include "query_parser.h"
#include <sstream>
#include <algorithm>
#include <iostream>
#include <utility>

ParsedQuery QueryParser::parse(const std::string& sql_query) {
    reset();
    
    // Tokenize the query
    tokens = tokenizer.tokenize(sql_query);
    current_token_index = 0;
    
    ParsedQuery result;
    
    // Parse SELECT clause
    parse_select(result);
    
    // Parse FROM clause
    parse_from(result);
    
    // Parse WHERE clause (optional)
    if (match(TokenType::WHERE)) {
        parse_where(result);
    }
    
    // Check for semicolon or end of query
    if (!is_at_end() && !match(TokenType::SEMICOLON)) {
        // Skip to end or semicolon
        while (!is_at_end() && !match(TokenType::SEMICOLON)) {
            consume();
        }
    }
    
    // Validate the result
    if (!result.is_valid()) {
        throw ParseException("Invalid query structure");
    }
    
    return result;
}

void QueryParser::parse_select(ParsedQuery& query) {
    expect(TokenType::SELECT, "Expected SELECT keyword");
    
    // Handle SELECT *
    if (match(TokenType::STAR)) {
        query.select_columns.push_back("*");
        consume();
    } else {
        // For now, we only support SELECT *
        throw ParseException("Only SELECT * is currently supported");
    }
}

void QueryParser::parse_from(ParsedQuery& query) {
    expect(TokenType::FROM, "Expected FROM keyword");

    // Parse comma-separated table list with aliases
    // Format: table_name AS alias, table_name AS alias, ...
    do {
        if (!match(TokenType::IDENTIFIER)) {
            throw ParseException("Expected table name in FROM clause");
        }

        // Get the CSV filename (table name)
        std::string table_filename = current().value;
        consume();

        // Expect AS keyword
        if (!match(TokenType::AS)) {
            throw ParseException("Expected AS keyword after table name '" + table_filename +
                               "'. All tables must use aliases in FROM clause.");
        }
        consume();

        // Get the alias
        if (!match(TokenType::IDENTIFIER)) {
            throw ParseException("Expected alias name after AS keyword for table '" + table_filename + "'");
        }
        std::string alias = current().value;
        consume();

        // Store alias in tables vector and mapping in table_aliases
        query.tables.push_back(alias);
        query.table_aliases[alias] = table_filename;

        // Check for comma
        if (match(TokenType::COMMA)) {
            consume();
        } else {
            break;
        }
    } while (!is_at_end());
}

void QueryParser::parse_where(ParsedQuery& query) {
    expect(TokenType::WHERE, "Expected WHERE keyword");
    
    std::vector<std::string> raw_conditions;
    
    // Parse conditions separated by AND
    do {
        std::string condition = parse_single_condition();
        if (!condition.empty()) {
            raw_conditions.push_back(condition);
        }
        
        // Check for AND
        if (match(TokenType::AND)) {
            consume();
        } else {
            break;
        }
    } while (!is_at_end() && !match(TokenType::SEMICOLON));
    
    // Parse each condition and categorize as join or filter
    std::vector<JoinConstraint> all_join_conditions;
    
    for (const auto& raw_cond : raw_conditions) {
        // Try to parse as join condition
        JoinConstraint join_constraint;
        bool is_join = InequalityParser::parse(raw_cond, join_constraint);
        
        if (is_join) {
            all_join_conditions.push_back(join_constraint);
        } else {
            // Not a join condition, treat as filter
            query.filter_conditions.push_back(raw_cond);
        }
    }
    
    // Combine the join conditions of each table pair into one constraint
    merge_join_conditions(all_join_conditions);
    query.join_conditions = std::move(all_join_conditions);
}

std::string QueryParser::parse_single_condition() {
    std::stringstream condition_str;
    int paren_depth = 0;
    
    // Collect tokens until we hit AND, semicolon, or end
    while (!is_at_end()) {
        if (paren_depth == 0) {
            if (match(TokenType::AND) || match(TokenType::SEMICOLON)) {
                break;
            }
        }
        
        // Add token value to condition string
        if (!condition_str.str().empty()) {
            condition_str << " ";
        }
        condition_str << current().value;
        
        consume();
    }
    
    return condition_str.str();
}

// Sort key of a (possibly composite) constraint: its main column pair. For a
// table pair joined on a single column pair this is exactly the key the
// constraints have always been ordered by, so join-tree shape and output
// column order of such queries do not change.
static std::string constraint_order_key(const JoinConstraint& c) {
    return c.get_source_table() + "." + c.get_source_column() +
           "~" + c.get_target_table() + "." + c.get_target_column();
}

// Human-readable name of a constraint's main column pair, for error messages.
static std::string column_pair_name(const JoinConstraint& c) {
    return c.get_source_table() + "." + c.get_source_column() +
           " / " + c.get_target_table() + "." + c.get_target_column();
}

// Combine all conditions between one pair of tables (already oriented the
// same way) into a single composite constraint.
static JoinConstraint combine_table_pair_conditions(
    const std::vector<JoinConstraint>& pair_conditions) {

    // Step 1: intersect conditions on the same column pair (e.g. a lower and
    // an upper bound of one band), keeping first-seen order.
    std::vector<JoinConstraint> per_column_pair;
    for (const auto& cond : pair_conditions) {
        bool merged = false;
        for (auto& existing : per_column_pair) {
            if (!ConditionMerger::can_merge(existing, cond)) {
                continue;
            }
            JoinConstraint intersection;
            if (!ConditionMerger::merge(existing, cond, intersection)) {
                throw ParseException(
                    "Contradictory join conditions on " + column_pair_name(cond) +
                    ": no value satisfies all of them");
            }
            existing = intersection;
            merged = true;
            break;
        }
        if (!merged) {
            per_column_pair.push_back(cond);
        }
    }

    // Step 2: separate equalities (possibly with a constant offset) from ranges.
    std::vector<JoinConstraint> equalities;
    std::vector<JoinConstraint> ranges;
    for (const auto& cond : per_column_pair) {
        if (cond.is_point()) {
            equalities.push_back(cond);
        } else {
            ranges.push_back(cond);
        }
    }

    // Under the composite key (equality columns, range column) the rows
    // matching a given row form one contiguous interval only if at most one
    // column pair carries a range. Two ranges describe a rectangle, which no
    // single sort order turns into an interval.
    if (ranges.size() > 1) {
        throw ParseException(
            "Unsupported join between tables '" + ranges[0].get_source_table() +
            "' and '" + ranges[0].get_target_table() +
            "': range conditions on more than one column pair (" +
            column_pair_name(ranges[0]) + " and " + column_pair_name(ranges[1]) +
            "). A table pair may be joined on any number of equalities plus at most one range.");
    }

    // Step 3: the range - or, without one, the last equality - becomes the
    // main condition; every other equality is carried alongside it.
    JoinConstraint combined;
    if (!ranges.empty()) {
        combined = ranges[0];
    } else {
        combined = equalities.back();
        equalities.pop_back();
    }

    if (equalities.size() > MAX_JOIN_EQ) {
        throw ParseException(
            "Unsupported join between tables '" + combined.get_source_table() +
            "' and '" + combined.get_target_table() + "': " +
            std::to_string(equalities.size() + 1) + " join conditions on distinct column pairs, "
            "but at most " + std::to_string(MAX_JOIN_EQ + 1) +
            " are supported (rebuild with a larger MAX_JOIN_EQ to raise the limit).");
    }

    for (const auto& eq : equalities) {
        combined.add_equality(eq.get_source_column(), eq.get_target_column(), eq.get_deviation1());
    }
    return combined;
}

void QueryParser::merge_join_conditions(std::vector<JoinConstraint>& conditions) {
    // Group the conditions by table pair. Every condition of a pair is
    // oriented the way the pair's first condition was written, so that
    // "a.x <= b.y + 5" and "b.y <= a.x + 3" land on the same column pair.
    typedef std::pair<std::string, std::string> TablePair;
    std::vector<TablePair> pair_order;
    std::map<TablePair, std::vector<JoinConstraint>> conditions_by_pair;

    for (const auto& cond : conditions) {
        if (cond.get_source_table() == cond.get_target_table()) {
            throw ParseException(
                "Unsupported condition on " + column_pair_name(cond) +
                ": it compares two columns of the same table '" + cond.get_source_table() +
                "'. Only comparisons between two different tables (joins) or between "
                "a column and a constant (filters) are supported.");
        }
        if (!cond.is_valid()) {
            throw ParseException(
                "Unsupported join condition on " + column_pair_name(cond) +
                ": it does not describe a single non-empty range "
                "('<>' / '!=' between tables is not supported).");
        }

        TablePair as_written(cond.get_source_table(), cond.get_target_table());
        TablePair flipped(cond.get_target_table(), cond.get_source_table());
        if (conditions_by_pair.count(as_written)) {
            conditions_by_pair[as_written].push_back(cond);
        } else if (conditions_by_pair.count(flipped)) {
            conditions_by_pair[flipped].push_back(cond.reverse());
        } else {
            pair_order.push_back(as_written);
            conditions_by_pair[as_written].push_back(cond);
        }
    }

    // One (possibly composite) constraint per table pair.
    std::vector<JoinConstraint> combined_conditions;
    for (const auto& tables : pair_order) {
        combined_conditions.push_back(combine_table_pair_conditions(conditions_by_pair[tables]));
    }

    std::stable_sort(combined_conditions.begin(), combined_conditions.end(),
                     [](const JoinConstraint& a, const JoinConstraint& b) {
                         return constraint_order_key(a) < constraint_order_key(b);
                     });

    // Replace with the combined conditions
    conditions = std::move(combined_conditions);
}

const Token& QueryParser::current() const {
    if (current_token_index >= tokens.size()) {
        static Token end_token(TokenType::END_OF_QUERY, "", 0);
        return end_token;
    }
    return tokens[current_token_index];
}

const Token& QueryParser::peek() const {
    if (current_token_index + 1 >= tokens.size()) {
        static Token end_token(TokenType::END_OF_QUERY, "", 0);
        return end_token;
    }
    return tokens[current_token_index + 1];
}

void QueryParser::consume() {
    if (!is_at_end()) {
        current_token_index++;
    }
}

bool QueryParser::match(TokenType type) const {
    return current().type == type;
}

void QueryParser::expect(TokenType type, const std::string& error_msg) {
    if (!match(type)) {
        throw ParseException(error_msg + " at position " + 
                           std::to_string(current().position) + 
                           ", got: " + current().to_string());
    }
    consume();
}

bool QueryParser::is_at_end() const {
    return current().type == TokenType::END_OF_QUERY;
}

void QueryParser::reset() {
    tokens.clear();
    current_token_index = 0;
    tokenizer.reset();
}