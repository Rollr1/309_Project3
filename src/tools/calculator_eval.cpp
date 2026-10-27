// src/tools/calculator_eval.cpp — YOU IMPLEMENT (spec §4.6).
//
// calc_evaluate: evaluate a well-formed token list with two stacks
// (one for numbers, one for operators and '('). See spec §4.6 and the
// worked trace in Appendix C. std::stack or std::vector are both fine.
// Iterative only: no recursion, so deep nesting cannot overflow the call stack.

#include "tools/calculator.h"

#include <stack>

using Kind = CalcToken::Kind;

static bool is_operator(Kind k) {
    return k == Kind::Plus || k == Kind::Minus || k == Kind::Times || k == Kind::Divide;
}

static int precedence(Kind k) {
    return (k == Kind::Times || k == Kind::Divide) ? 2 : 1;
}

// Pops one operator and two operands (right first), pushes left op right.
static bool apply_top(std::stack<double>& values, std::stack<Kind>& ops, std::string* error) {
    if (ops.empty() || !is_operator(ops.top())) {
        *error = "malformed expression: missing operator";
        return false;
    }
    if (values.size() < 2) {
        *error = "malformed expression: missing operand";
        return false;
    }
    Kind op = ops.top();
    ops.pop();
    double right = values.top();
    values.pop();
    double left = values.top();
    values.pop();

    double result = 0.0;
    switch (op) {
        case Kind::Plus:   result = left + right; break;
        case Kind::Minus:  result = left - right; break;
        case Kind::Times:  result = left * right; break;
        case Kind::Divide:
            if (right == 0.0) {
                *error = "division by zero";
                return false;
            }
            result = left / right;
            break;
        default:
            *error = "malformed expression: unknown operator";
            return false;
    }
    values.push(result);
    return true;
}

bool calc_evaluate(const std::vector<CalcToken>& tokens, double* out, std::string* error) {
    std::stack<double> values;
    std::stack<Kind>   ops;

    for (const CalcToken& t : tokens) {
        switch (t.kind) {
            case Kind::Number:
                values.push(t.value);
                break;
            case Kind::LParen:
                ops.push(Kind::LParen);
                break;
            case Kind::RParen:
                while (!ops.empty() && ops.top() != Kind::LParen)
                    if (!apply_top(values, ops, error)) return false;
                if (ops.empty()) {
                    *error = "malformed expression: unbalanced ')'";
                    return false;
                }
                ops.pop();   // the matching '('
                break;
            case Kind::Plus:
            case Kind::Minus:
            case Kind::Times:
            case Kind::Divide:
                // >= makes equal-precedence operators apply left to right.
                while (!ops.empty() && is_operator(ops.top()) &&
                       precedence(ops.top()) >= precedence(t.kind))
                    if (!apply_top(values, ops, error)) return false;
                ops.push(t.kind);
                break;
        }
    }

    while (!ops.empty()) {
        if (ops.top() == Kind::LParen) {
            *error = "malformed expression: unbalanced '('";
            return false;
        }
        if (!apply_top(values, ops, error)) return false;
    }

    if (values.size() != 1) {
        *error = values.empty() ? "malformed expression: empty" : "malformed expression: missing operator";
        return false;
    }
    *out = values.top();
    return true;
}
