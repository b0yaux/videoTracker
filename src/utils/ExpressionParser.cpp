#include "ExpressionParser.h"
#include <stack>
#include <cctype>
#include <stdexcept>
#include <cmath>

float ExpressionParser::evaluate(const std::string& expr) {
    if (expr.empty()) {
        throw std::invalid_argument("Empty expression");
    }
    
    // Handle starting with '.' (treat as "0.")
    std::string processed = expr;
    if (processed[0] == '.') {
        processed = "0" + processed;
    }
    
    // Simple expression evaluator using two stacks (shunting yard algorithm simplified)
    std::stack<float> values;
    std::stack<char> ops;
    
    auto applyOp = [&](char op) {
        if (values.size() < 2) return;
        float b = values.top(); values.pop();
        float a = values.top(); values.pop();
        switch (op) {
            case '+': values.push(a + b); break;
            case '-': values.push(a - b); break;
            case '*': values.push(a * b); break;
            case '/': 
                if (std::abs(b) < EPSILON_DIVISION) throw std::runtime_error("Division by zero");
                values.push(a / b); 
                break;
        }
    };
    
    auto precedence = [](char op) -> int {
        if (op == '+' || op == '-') return 1;
        if (op == '*' || op == '/') return 2;
        return 0;
    };
    
    size_t i = 0;
    bool expectNumber = true;
    
    while (i < processed.length()) {
        // Skip whitespace
        if (std::isspace(processed[i])) {
            i++;
            continue;
        }
        
        // Handle '-' - could be negative number or subtraction
        if (processed[i] == '-') {
            if (expectNumber) {
                // Check if this is a negative number (followed by digit or '.')
                // or subtraction (not followed by digit/'.' and we have values)
                bool isNegative = false;
                if (i + 1 < processed.length() && (std::isdigit(processed[i + 1]) || processed[i + 1] == '.')) {
                    isNegative = true;
                } else if (values.empty()) {
                    // No values yet, must be negative (even if incomplete, user is typing)
                    isNegative = true;
                }
                // Otherwise, it's subtraction (handled below)
                
                if (isNegative) {
                    i++; // Consume the '-'
                    if (i >= processed.length()) {
                        // Incomplete negative - user might be typing, allow it
                        // Don't throw, just return 0 or let it be handled by caller
                        throw std::invalid_argument("Incomplete negative number");
                    }
                    
                    size_t start = i;
                    bool hasDecimal = false;
                    while (i < processed.length() && (std::isdigit(processed[i]) || processed[i] == '.')) {
                        if (processed[i] == '.') {
                            if (hasDecimal) throw std::invalid_argument("Multiple decimal points");
                            hasDecimal = true;
                        }
                        i++;
                    }
                    
                    if (i == start) throw std::invalid_argument("Invalid negative number");
                    float val = std::stof(processed.substr(start, i - start));
                    values.push(-val);
                    expectNumber = false;
                    continue;
                }
            }
            // Fall through to operator handling for subtraction
        }
        
        // Parse number (positive, starting with digit or '.')
        if (std::isdigit(processed[i]) || processed[i] == '.') {
            size_t start = i;
            bool hasDecimal = false;
            while (i < processed.length() && (std::isdigit(processed[i]) || processed[i] == '.')) {
                if (processed[i] == '.') {
                    if (hasDecimal) throw std::invalid_argument("Multiple decimal points");
                    hasDecimal = true;
                }
                i++;
            }
            float val = std::stof(processed.substr(start, i - start));
            values.push(val);
            expectNumber = false;
            continue;
        }
        
        // Handle operators (binary operations: +, -, *, /)
        if (processed[i] == '+' || processed[i] == '-' || processed[i] == '*' || processed[i] == '/') {
            if (expectNumber) {
                throw std::invalid_argument("Unexpected operator");
            }
            
            // This is a binary operator
            while (!ops.empty() && precedence(ops.top()) >= precedence(processed[i])) {
                applyOp(ops.top());
                ops.pop();
            }
            ops.push(processed[i]);
            expectNumber = true;
            i++;
            continue;
        }
        
        throw std::invalid_argument("Invalid character in expression");
    }
    
    // Apply remaining operators
    while (!ops.empty()) {
        applyOp(ops.top());
        ops.pop();
    }
    
    if (values.size() != 1) {
        throw std::invalid_argument("Invalid expression");
    }
    
    return values.top();
}


