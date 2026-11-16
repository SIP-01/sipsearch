#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

// A structured query, parsed the way Dsearch parses it.
//
// The distinction the whole thing rests on: a TEXT node is matched against
// the document's text (title, description, topics), a FILTER node is
// evaluated against the document's structured metadata (host, language,
// topics, dates). A relay only ever sees the raw query as a hint — this
// decides what actually matched, so a relay that has never heard of
// site: cannot produce a wrong answer.
namespace Query {

// The structured fields a filter can address.
enum class FilterField {
    Site,     // host is the value or a subdomain of it
    Domain,   // exact host only
    Title,    // the term must appear in the title
    Type,     // mime tail / document type
    Lang,     // ISO 639-1 language tag
    Tag,      // exact topic tag
    Before,   // strictly before that day
    After,    // on or after that day
};

QString filterFieldName(FilterField field);

struct FilterClause {
    FilterField field = FilterField::Site;
    QString value;
    bool negated = false;
};

// One node of the parsed query tree.
struct Node {
    enum Kind { Term, Phrase, And, Or, Not, Filter };
    Kind kind = Term;
    QString value;                 // Term, Phrase, Filter (the value)
    FilterField field = FilterField::Site;  // Filter
    QVector<Node> children;        // And, Or
    std::shared_ptr<Node> child;   // Not
    bool implicit = true;          // And: juxtaposition rather than explicit AND
};

struct Parsed {
    QString raw;
    bool hasExpr = false;
    Node expr;
    QVector<FilterClause> filters;
    bool hasBoolean = false;
    bool hasPhrases = false;
    bool hasFilters = false;
    bool hadErrors = false;
};

// Parse a raw query. Never throws — malformed input degrades to something
// useful with hadErrors set.
Parsed parse(const QString &raw);

// Every filter leaf in the tree, flattened, with its negation state.
QVector<FilterClause> collectFilters(const Node &node, bool negated = false);

// The text leaves only — terms and phrases, no filters or operators.
void collectTextLeaves(const Node &node, QStringList &terms, QStringList &phrases);

// The plain-text residue of a query, for engines that only understand text.
QString textOnly(const Parsed &parsed);

// True when the field name is one this engine understands.
bool isFilterField(const QString &name);

} // namespace Query
