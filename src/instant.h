#pragma once

#include <QString>

// A tiny safe arithmetic evaluator for instant answers.
//
// Handles + - * / % ^ (power), parentheses, unary minus and decimals, plus
// the operator symbols people actually type: x (U+00D7), / (U+00F7),
// - (U+2212), and a casual "x" ("8x5"). Implemented as a recursive-descent
// parser — never eval(), never a shell.
//
//   "2 + 2"        -> 4
//   "(3 + 4) * 5"  -> 35
//   "2^10"         -> 1024
//   "15% of 80"    -> 12
namespace Instant {

// True when the query is a pure arithmetic expression.
bool isMathQuery(const QString &query);

// Evaluate a math query. Returns false when the expression is invalid.
bool evaluateMath(const QString &query, double &value);

// The result, formatted for display: float noise trimmed, thousands
// separated.
QString formatMathResult(double value);

// What kind of thing the user typed.
enum class QueryClass {
    Text,   // ordinary search
    Math,   // an arithmetic expression
    Url,    // a URL or a domain with a path
    Nip19,  // a bech32 Nostr entity
    Nip05,  // a NIP-05 name@domain address
};

// Classify a raw query string.
QueryClass classifyQuery(const QString &query);

// The display name for a class, for the status line.
QString className(QueryClass cls);

} // namespace Instant
