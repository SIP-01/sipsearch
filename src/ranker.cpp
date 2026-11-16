#include "ranker.h"

#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>
#include <algorithm>

namespace {

// Words shorter than this carry no signal and only inflate the term table.
constexpr int kMinTokenLength = 2;

// Common words that match everything rank nothing. Kept small on purpose:
// an aggressive stop list hides real results.
const QStringList &stopWords()
{
    static const QStringList words{
        QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"),
        QStringLiteral("of"), QStringLiteral("and"), QStringLiteral("or"),
        QStringLiteral("to"), QStringLiteral("in"), QStringLiteral("on"),
        QStringLiteral("is"), QStringLiteral("it"), QStringLiteral("for"),
        QStringLiteral("at"), QStringLiteral("by"), QStringLiteral("with"),
        QStringLiteral("be"), QStringLiteral("as"), QStringLiteral("are"),
        QStringLiteral("this"), QStringLiteral("that"), QStringLiteral("it"),
    };
    return words;
}

QStringList tokenize(const QString &text)
{
    static const QRegularExpression separators{"[^\\p{L}\\p{N}_]+"};
    QStringList tokens;
    const auto parts = text.split(separators, Qt::SkipEmptyParts);
    tokens.reserve(parts.size());
    for (const auto &part : parts) {
        const QString token = part.toLower();
        if (token.length() >= kMinTokenLength && !stopWords().contains(token))
            tokens.append(token);
    }
    return tokens;
}

bool containsPhrase(const IndexDocument &doc, const QString &phrase)
{
    if (phrase.isEmpty())
        return false;
    const QStringList words = tokenize(phrase);
    if (words.isEmpty())
        return false;
    for (const auto &word : words) {
        if (!doc.terms.contains(word))
            return false;
    }
    return true;
}

} // namespace

void Ranker::index(IndexDocument &doc)
{
    const QStringList words = tokenize(doc.title + QLatin1Char(' ') + doc.description
                                       + QLatin1Char(' ') + doc.topics.join(QLatin1Char(' ')));
    doc.terms.clear();
    for (const auto &word : words)
        ++doc.terms[word];
    doc.length = words.size();
}

void Ranker::parse(const QString &query, QStringList &terms, QStringList &phrases)
{
    terms.clear();
    phrases.clear();

    QString current;
    bool inQuotes = false;
    const auto flush = [&] {
        if (!current.isEmpty()) {
            phrases.append(current.trimmed());
            current.clear();
        }
    };

    const auto parts = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const auto &part : parts) {
        QString word = part;
        if (word.startsWith(QLatin1Char('"'))) {
            inQuotes = true;
            word = word.mid(1);
        }
        if (word.endsWith(QLatin1Char('"'))) {
            word.chop(1);
            current += QLatin1Char(' ') + word;
            flush();
            inQuotes = false;
            continue;
        }
        if (inQuotes) {
            current += QLatin1Char(' ') + word;
            continue;
        }
        terms.append(word.toLower());
    }
    flush();

    terms.removeAll(QString());
}

double Ranker::score(const IndexDocument &doc, const QStringList &terms,
                     const QStringList &phrases)
{
    if (terms.isEmpty() && phrases.isEmpty())
        return 0.0;

    double score = 0.0;
    bool matched = false;

    for (const auto &term : terms) {
        const auto it = doc.terms.constFind(term);
        if (it == doc.terms.constEnd())
            continue;
        matched = true;

        // Term frequency, damped so one keyword-stuffed document cannot
        // dominate the whole result set.
        score += 1.0 + std::log(1.0 + it.value());

        // A title hit is worth more than a body hit.
        if (doc.title.contains(term, Qt::CaseInsensitive))
            score += 2.0;
        if (doc.topics.join(QLatin1Char(' ')).contains(term, Qt::CaseInsensitive))
            score += 1.0;
        if (doc.url.contains(term, Qt::CaseInsensitive))
            score += 0.5;
    }

    // A quoted phrase that is present is a strong signal.
    for (const auto &phrase : phrases) {
        if (containsPhrase(doc, phrase)) {
            matched = true;
            score += 3.0;
        }
    }

    // Nothing matched, so this document is not an answer, whatever else it
    // has going for it.
    if (!matched)
        return 0.0;

    // Independent indexers agreeing on this page is the protocol's core
    // ranking signal, so it breaks ties and lifts near-duplicates.
    score += std::log(1.0 + doc.indexers) * 1.5;

    // Normalize by document length, so a long page does not outrank a
    // focused one just by having more words.
    if (doc.length > 0)
        score /= std::sqrt(doc.length / 40.0);

    return score;
}

QStringList Ranker::keywords(const IndexDocument &doc, int limit)
{
    QHash<QString, int> counts = doc.terms;
    QStringList words = counts.keys();
    std::sort(words.begin(), words.end(), [&counts](const QString &a, const QString &b) {
        const int ca = counts.value(a);
        const int cb = counts.value(b);
        if (ca != cb)
            return ca > cb;
        return a < b;
    });
    if (words.size() > limit)
        words = words.mid(0, limit);
    return words;
}
