#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// A document in the SIP-01 web index, as observed by one or more indexers.
struct IndexDocument {
    QString id;        // the "d" tag, e.g. widx:47ed46c7…
    QString url;       // the "u" tag
    QString title;
    QString description;
    QString lang;
    QStringList topics;
    qint64 createdAt = 0;
    int indexers = 1;   // distinct authors that observed this page

    // Term frequencies, keyed by lowercased token.
    QHash<QString, int> terms;
    int length = 0;     // tokens in title + description + topics
};

// Ranks documents against a query the way a search engine does: terms are
// matched on the document's own text, not on whatever the relay happened to
// return, and documents observed by more indexers rank higher because
// independent agreement is the core signal of the protocol.
class Ranker {
public:
    // Build the term table for a document. Called once per observation.
    static void index(IndexDocument &doc);

    // Score every document against the query terms.
    // Terms are matched as whole words; a quoted phrase is matched as a
    // contiguous run of words.
    static double score(const IndexDocument &doc, const QStringList &terms,
                        const QStringList &phrases);

    // Split a query into loose terms and quoted phrases.
    static void parse(const QString &query, QStringList &terms, QStringList &phrases);

    // The document's distinctive terms, for "why this result" explanations.
    static QStringList keywords(const IndexDocument &doc, int limit = 6);
};
