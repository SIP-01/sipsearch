#include "engine.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <QUrl>

namespace Engine {

namespace {

// Naive singular folding: "wallets" -> "wallet", "queries" -> "query".
QString foldPlural(const QString &term)
{
    if (term.size() > 4 && term.endsWith(QLatin1String("ies")))
        return term.left(term.size() - 3) + QLatin1String("y");
    if (term.size() > 4
        && (term.endsWith(QLatin1String("ches")) || term.endsWith(QLatin1String("shes"))
            || term.endsWith(QLatin1String("sses")) || term.endsWith(QLatin1String("xes"))
            || term.endsWith(QLatin1String("zes")))) {
        return term.left(term.size() - 2);
    }
    if (term.size() > 2 && term.endsWith(QLatin1Char('s')) && !term.endsWith(QLatin1String("ss")))
        return term.left(term.size() - 1);
    return term;
}

QStringList splitWords(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

// The document as the evaluator sees it, normalized once per query.
struct Ctx {
    QString haystack;   // space-padded normalized text
    QString title;      // space-padded normalized title
    QString host;       // lowercase hostname
    QSet<QString> topics;
};

Ctx buildCtx(const Doc &doc)
{
    Ctx ctx;
    QStringList parts;
    if (!doc.title.isEmpty()) parts << doc.title;
    if (!doc.description.isEmpty()) parts << doc.description;
    if (!doc.url.isEmpty()) parts << doc.url;
    parts << doc.topics;
    ctx.haystack = QLatin1Char(' ') + normalizeText(parts.join(QLatin1Char(' ')))
        + QLatin1Char(' ');
    ctx.title = doc.title.isEmpty()
        ? QString()
        : QLatin1Char(' ') + normalizeText(doc.title) + QLatin1Char(' ');

    if (!doc.url.isEmpty()) {
        const QUrl url(doc.url);
        if (url.isValid())
            ctx.host = url.host().toLower();
    }

    for (const QString &topic : doc.topics)
        ctx.topics.insert(topic.toLower());
    return ctx;
}

QStringList termWords(const QString &value)
{
    QStringList words;
    for (const QString &word : splitWords(normalizeText(value)))
        if (word.size() >= 2)
            words.append(word);
    return words;
}

// A term matches when every word it carries matches.
bool evalTerm(const QString &value, const Ctx &ctx)
{
    const QStringList words = termWords(value);
    if (words.isEmpty())
        return true; // punctuation only — no constraint
    for (const QString &word : words)
        if (!termMatches(ctx.haystack, word))
            return false;
    return true;
}

// A phrase matches when its normalized form appears contiguously.
bool evalPhrase(const QString &value, const Ctx &ctx)
{
    const QString phrase = normalizeText(value).trimmed();
    if (phrase.size() < 2)
        return true;
    return ctx.haystack.contains(phrase);
}

// Normalize a site:/domain: value to a host.
QString normalizeHostValue(const QString &value)
{
    QString v = value.trimmed().toLower();
    if (v.isEmpty())
        return QString();
    static const QRegularExpression scheme(QStringLiteral("^[a-z][a-z0-9+.-]*://"));
    v.remove(scheme);
    v = v.section(QLatin1Char('/'), 0, 0);
    v = v.section(QLatin1Char('?'), 0, 0);
    v = v.section(QLatin1Char('#'), 0, 0);
    if (v.startsWith(QLatin1String("*.")))
        v = v.mid(2);
    static const QRegularExpression port(QStringLiteral(":\\d+$"));
    v.remove(port);
    if (v.endsWith(QLatin1Char('.')))
        v.chop(1);
    static const QRegularExpression valid(QStringLiteral("^[\\w.-]+$"));
    return valid.match(v).hasMatch() ? v : QString();
}

// Evaluate one node of the tree.
bool evalNode(const Query::Node &node, const Doc &doc, const Ctx &ctx)
{
    switch (node.kind) {
    case Query::Node::Term:
        return evalTerm(node.value, ctx);
    case Query::Node::Phrase:
        return evalPhrase(node.value, ctx);
    case Query::Node::Filter:
        return evalFilter({node.field, node.value, false}, doc);
    case Query::Node::Not:
        return node.child ? !evalNode(*node.child, doc, ctx) : true;
    case Query::Node::And:
        for (const Query::Node &child : node.children)
            if (!evalNode(child, doc, ctx))
                return false;
        return true;
    case Query::Node::Or:
        for (const Query::Node &child : node.children)
            if (evalNode(child, doc, ctx))
                return true;
        return false;
    }
    return false;
}

} // namespace

QString normalizeText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text.toLower()) {
        if (c.isLetterOrNumber())
            out += c;
        else if (!out.isEmpty() && !out.endsWith(QLatin1Char(' ')))
            out += QLatin1Char(' ');
    }
    return out.trimmed();
}

bool termMatches(const QString &haystack, const QString &term)
{
    if (term.isEmpty())
        return false;
    const QString needle = QLatin1Char(' ') + term + QLatin1Char(' ');
    if (haystack.contains(needle))
        return true;
    const QString folded = foldPlural(term);
    if (folded != term && haystack.contains(QLatin1Char(' ') + folded + QLatin1Char(' ')))
        return true;
    return false;
}

qint64 parseDateBoundary(const QString &value)
{
    static const QRegularExpression re(QStringLiteral("^(\\d{4})(?:-(\\d{2}))?(?:-(\\d{2}))?$"));
    const auto match = re.match(value.trimmed());
    if (!match.hasMatch())
        return -1;
    const int year = match.captured(1).toInt();
    const int month = match.captured(2).isEmpty() ? 1 : match.captured(2).toInt();
    const int day = match.captured(3).isEmpty() ? 1 : match.captured(3).toInt();
    if (month < 1 || month > 12 || day < 1 || day > 31)
        return -1;
    const QDate date(year, month, day);
    if (!date.isValid())
        return -1;
    return date.startOfDay(QTimeZone::UTC).toSecsSinceEpoch();
}

bool evalFilter(const Query::FilterClause &clause, const Doc &doc)
{
    switch (clause.field) {
    case Query::FilterField::Site:
    case Query::FilterField::Domain: {
        const QString want = normalizeHostValue(clause.value);
        if (want.isEmpty())
            return false;
        const QUrl url(doc.url);
        const QString host = url.isValid() ? url.host().toLower() : QString();
        if (host.isEmpty())
            return false;
        if (clause.field == Query::FilterField::Domain)
            return host == want;
        // site: matches the host or a subdomain of it — the dot boundary
        // stops "evilgithub.com" from matching "github.com".
        return host == want || host.endsWith(QLatin1Char('.') + want);
    }

    case Query::FilterField::Title: {
        const QString value = normalizeText(clause.value).trimmed();
        if (value.isEmpty())
            return true;
        const QString title = QLatin1Char(' ') + normalizeText(doc.title) + QLatin1Char(' ');
        if (value.contains(QLatin1Char(' ')))
            return title.contains(value);
        return termMatches(title, value);
    }

    case Query::FilterField::Type: {
        const QString value = clause.value.trimmed().toLower();
        if (value.isEmpty())
            return true;
        // An unknown type must fail rather than falsely match.
        if (doc.type.isEmpty() && doc.mime.isEmpty())
            return false;
        const QString type = doc.type.toLower();
        const QString mime = doc.mime.toLower();
        return type == value || mime == value
            || (!mime.isEmpty() && mime.endsWith(QLatin1Char('/') + value));
    }

    case Query::FilterField::Lang: {
        const QString value = clause.value.trimmed().toLower();
        if (!QRegularExpression(QStringLiteral("^[a-z]{2}$")).match(value).hasMatch())
            return true; // invalid language is a no-op
        // Most indexers do not tag a language, so an unknown one passes.
        if (doc.language.isEmpty())
            return true;
        return doc.language.toLower() == value;
    }

    case Query::FilterField::Tag: {
        const QString value = clause.value.trimmed().toLower();
        if (value.isEmpty())
            return true;
        for (const QString &topic : doc.topics)
            if (topic.toLower() == value)
                return true;
        return false;
    }

    case Query::FilterField::Before:
    case Query::FilterField::After: {
        const qint64 boundary = parseDateBoundary(clause.value);
        if (boundary < 0)
            return true; // invalid date is a no-op
        const qint64 stamp = doc.publishedAt ? doc.publishedAt : doc.observedAt;
        if (stamp == 0)
            return true; // freshness cannot be disproven
        return clause.field == Query::FilterField::Before ? stamp < boundary : stamp >= boundary;
    }
    }
    return false;
}

Evaluation evaluate(const Query::Parsed &parsed, const Doc &doc)
{
    Evaluation result;
    if (!parsed.hasExpr)
        return result;

    const Ctx ctx = buildCtx(doc);
    result.filterTotal = parsed.filters.size();
    for (const Query::FilterClause &filter : parsed.filters)
        if (evalFilter(filter, doc))
            ++result.filtersSatisfied;

    QStringList terms, phrases;
    Query::collectTextLeaves(parsed.expr, terms, phrases);

    bool matched = false;
    if (parsed.hasBoolean) {
        matched = evalNode(parsed.expr, doc, ctx);
    } else {
        // Plain query: every meaningful term must match, and a quoted phrase
        // must appear.
        for (const QString &term : terms) {
            if (!evalTerm(term, ctx)) {
                matched = false;
                goto settled;
            }
        }
        for (const QString &phrase : phrases) {
            if (!evalPhrase(phrase, ctx)) {
                matched = false;
                goto settled;
            }
        }
        // Filters AND into the text gate.
        for (const Query::FilterClause &filter : parsed.filters) {
            const bool satisfied = evalFilter(filter, doc);
            if (filter.negated ? satisfied : !satisfied) {
                matched = false;
                goto settled;
            }
        }
        matched = true;
    }

settled:
    result.match = matched;
    if (!matched)
        return result;

    // Relevance: word coverage of the query's text, plus a title bonus.
    QSet<QString> queryWords;
    for (const QString &term : terms)
        for (const QString &word : termWords(term))
            queryWords.insert(word);
    for (const QString &phrase : phrases)
        for (const QString &word : termWords(phrase))
            queryWords.insert(word);

    int hits = 0;
    for (const QString &word : queryWords)
        if (termMatches(ctx.haystack, word))
            ++hits;
    result.coverage = queryWords.isEmpty() ? 1.0 : double(hits) / double(queryWords.size());

    for (const QString &phrase : phrases) {
        const QString normalized = normalizeText(phrase).trimmed();
        if (normalized.size() >= 2 && ctx.haystack.contains(normalized))
            result.phraseHit = true;
    }
    for (const QString &term : terms) {
        const QString normalized = normalizeText(term).trimmed();
        if (normalized.size() >= 2 && ctx.title.contains(normalized))
            result.titleHit = true;
    }

    result.relevance = result.coverage;
    if (result.titleHit)
        result.relevance += 0.5;
    if (result.phraseHit)
        result.relevance += 0.5;
    return result;
}

} // namespace Engine
