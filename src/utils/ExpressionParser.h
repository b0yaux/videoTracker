#pragma once

#include <string>

// ExpressionParser - Utility class for evaluating simple mathematical expressions
// Supports: +, -, *, / with proper precedence
// Handles negative numbers and decimal numbers
class ExpressionParser {
public:
    // Evaluate a mathematical expression string
    // Supports: +, -, *, / with proper precedence
    // Handles negative numbers and decimal numbers
    // Throws std::invalid_argument or std::runtime_error on parse errors
    static float evaluate(const std::string& expr);
    
private:
    static constexpr float EPSILON_DIVISION = 1e-9f;
};


