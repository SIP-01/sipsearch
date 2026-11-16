#pragma once

#include "query.h"
#include "ranker.h"

#include <QHash>
#include <QString>
#include <QStringList>

// Evaluates a parsed query against documents.
//
// Two evaluation modes, deliberately:
//
//   - PLAIN GATE — a query with no explicit boolean keeps the legacy
//     semantics: meaningful-term AND gate, phrase floors.
//
//   - STRICT TREE — explicit AND/OR/NOT and parentheses evaluate the tree
//     directly, where every word is deliberate.
//
// Filters always evaluate against structured metadata, never free text.
namespace Engine {

// A document as the evaluator sees it.
struct Doc {
    QString url;
    QString title;
    QString description;
    QStringList topics;
    QString language;
    QString type;
    QString mime;
    qint64 publishedAt = 0;
    qint64 observedAt = 0;
};

// The result of evaluating a query against one document.
struct Evaluation {
    bool match = false;
    double relevance = 0.0;
    double coverage = 0.0;
    bool phraseHit = false;
    bool titleHit = false;
    int filtersSatisfied = 0;
    int filterTotal = 0;
};

// Normalize text the way the matcher does: lowercase, letters and numbers
// only, everything else is a space.
QString normalizeText(const QString &text);

// True when the term appears in the haystack as a whole word (or its
// singular form).
bool termMatches(const QString &haystack, const QString &term);

// Parse a date boundary (YYYY, YYYY-MM, YYYY-MM-DD) to the start of that
// day in UTC. Returns -1 when the value is not a date.
qint64 parseDateBoundary(const QString &value);

// True when the document satisfies the filter clause.
bool evalFilter(const Query::FilterClause &clause, const Doc &doc);

// Evaluate a parsed query against a document.
Evaluation evaluate(const Query::Parsed &parsed, const Doc &doc);

} // namespace Engine
