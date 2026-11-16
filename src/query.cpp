#include "query.h"

#include <QRegularExpression>

namespace Query {

namespace {

struct Token {
    enum Kind { Word, Phrase, And, Or, Not, LParen, RParen };
    Kind kind = Word;
    QString text;
};

// The filter fields this engine understands.
const QStringList &filterFieldNames()
{
    static const QStringList names{
        QStringLiteral("site"), QStringLiteral("domain"), QStringLiteral("title"),
        QStringLiteral("type"), QStringLiteral("lang"), QStringLiteral("tag"),
        QStringLiteral("before"), QStringLiteral("after"),
    };
    return names;
}

// Split raw input into tokens. AND/OR/NOT are operators only when the whole
// token is uppercase, so "rock and roll" stays ordinary words.
QVector<Token> lex(const QString &raw, bool &hadErrors)
{
    QVector<Token> tokens;
    const int n = raw.size();
    int i = 0;

    auto isBoundary = [](QChar c) {
        return c == QLatin1Char(' ') || c == QLatin1Char('\t')
            || c == QLatin1Char('\n') || c == QLatin1Char('\r')
            || c == QLatin1Char('(') || c == QLatin1Char(')');
    };

    while (i < n) {
        const QChar c = raw.at(i);

        if (c == QLatin1Char(' ') || c == QLatin1Char('\t')
            || c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            ++i;
            continue;
        }
        if (c == QLatin1Char('(')) {
            tokens.append({Token::LParen, QString()});
            ++i;
            continue;
        }
        if (c == QLatin1Char(')')) {
            tokens.append({Token::RParen, QString()});
            ++i;
            continue;
        }

        // Quoted phrase — runs to the closing quote, or to the end when
        // unclosed (graceful, flagged).
        if (c == QLatin1Char('"')) {
            const int end = raw.indexOf(QLatin1Char('"'), i + 1);
            if (end == -1) {
                tokens.append({Token::Phrase, raw.mid(i + 1)});
                hadErrors = true;
                i = n;
            } else {
                tokens.append({Token::Phrase, raw.mid(i + 1, end - i - 1)});
                i = end + 1;
            }
            continue;
        }

        // A leading '-' is a NOT prefix when something follows it.
        if (c == QLatin1Char('-') && i + 1 < n && !isBoundary(raw.at(i + 1))
            && raw.at(i + 1) != QLatin1Char('-')) {
            tokens.append({Token::Not, QString()});
            ++i;
            continue;
        }

        // A word runs to the next boundary. An embedded quote after a known
        // "field:" prefix pulls in the quoted section as the value.
        int j = i;
        QString text;
        while (j < n && !isBoundary(raw.at(j))) {
            if (raw.at(j) == QLatin1Char('"')) {
                const QRegularExpression fieldRe(QStringLiteral("^([a-z]+):$"),
                                                 QRegularExpression::CaseInsensitiveOption);
                const auto match = fieldRe.match(text);
                if (match.hasMatch() && isFilterField(match.captured(1))) {
                    const int end = raw.indexOf(QLatin1Char('"'), j + 1);
                    if (end == -1) {
                        text += raw.mid(j + 1);
                        hadErrors = true;
                        j = n;
                    } else {
                        text += raw.mid(j + 1, end - j - 1);
                        j = end + 1;
                    }
                    continue;
                }
            }
            text += raw.at(j);
            ++j;
        }
        i = j;

        if (text.isEmpty())
            continue;

        if (text == QLatin1String("AND")) {
            tokens.append({Token::And, QString()});
            continue;
        }
        if (text == QLatin1String("OR")) {
            tokens.append({Token::Or, QString()});
            continue;
        }
        if (text == QLatin1String("NOT")) {
            tokens.append({Token::Not, QString()});
            continue;
        }

        tokens.append({Token::Word, text});
    }

    return tokens;
}

struct State {
    QVector<Token> tokens;
    int pos = 0;
    bool hadErrors = false;

    const Token *peek() const
    {
        return pos < tokens.size() ? &tokens.at(pos) : nullptr;
    }

    bool startsPrimary() const
    {
        const Token *t = peek();
        return t && (t->kind == Token::Word || t->kind == Token::Phrase
                     || t->kind == Token::LParen || t->kind == Token::Not);
    }
};

// Recursive descent, precedence NOT > AND > OR. Juxtaposition is an
// implicit AND.
bool parseOr(State &s, Node &out);
bool parseAnd(State &s, Node &out);
bool parseUnary(State &s, Node &out);
bool parsePrimary(State &s, Node &out);

bool parsePrimary(State &s, Node &out)
{
    const Token *t = s.peek();
    if (!t) {
        s.hadErrors = true;
        return false;
    }

    if (t->kind == Token::LParen) {
        ++s.pos;
        Node inner;
        if (!parseOr(s, inner)) {
            s.hadErrors = true; // empty parens
            return false;
        }
        if (s.peek() && s.peek()->kind == Token::RParen)
            ++s.pos;
        else
            s.hadErrors = true; // unclosed paren — accept what we have
        out = inner;
        return true;
    }

    if (t->kind == Token::Phrase) {
        ++s.pos;
        const QString value = t->text.trimmed();
        if (value.isEmpty()) {
            s.hadErrors = true;
            return false;
        }
        out = Node();
        out.kind = Node::Phrase;
        out.value = value;
        return true;
    }

    if (t->kind == Token::Word) {
        ++s.pos;
        // A "field:value" word is a filter; anything else is a text term.
        static const QRegularExpression fieldRe(QStringLiteral("^([a-z]+):(.*)$"),
                                                 QRegularExpression::CaseInsensitiveOption);
        const auto match = fieldRe.match(t->text);
        if (match.hasMatch() && isFilterField(match.captured(1))) {
            const QString value = match.captured(2).trimmed();
            if (value.isEmpty()) {
                s.hadErrors = true; // "site:" with no value
                return false;
            }
            out = Node();
            out.kind = Node::Filter;
            out.value = value;
            const QString field = match.captured(1).toLower();
            if (field == QLatin1String("site")) out.field = FilterField::Site;
            else if (field == QLatin1String("domain")) out.field = FilterField::Domain;
            else if (field == QLatin1String("title")) out.field = FilterField::Title;
            else if (field == QLatin1String("type")) out.field = FilterField::Type;
            else if (field == QLatin1String("lang")) out.field = FilterField::Lang;
            else if (field == QLatin1String("tag")) out.field = FilterField::Tag;
            else if (field == QLatin1String("before")) out.field = FilterField::Before;
            else out.field = FilterField::After;
            return true;
        }
        out = Node();
        out.kind = Node::Term;
        out.value = t->text;
        return true;
    }

    // A stray ')' or an operator where a primary belongs.
    s.hadErrors = true;
    ++s.pos;
    return false;
}

bool parseUnary(State &s, Node &out)
{
    const Token *t = s.peek();
    if (t && t->kind == Token::Not) {
        ++s.pos;
        auto child = std::make_shared<Node>();
        if (!parseUnary(s, *child)) {
            s.hadErrors = true; // dangling NOT
            return false;
        }
        out = Node();
        out.kind = Node::Not;
        out.child = child;
        return true;
    }
    return parsePrimary(s, out);
}

bool parseAnd(State &s, Node &out)
{
    QVector<Node> children;
    bool implicit = true;

    Node child;
    if (parseUnary(s, child))
        children.append(child);

    for (;;) {
        const Token *t = s.peek();
        if (t && t->kind == Token::And) {
            implicit = false;
            ++s.pos;
            Node next;
            if (!parseUnary(s, next)) {
                s.hadErrors = true; // dangling AND
                break;
            }
            children.append(next);
            continue;
        }
        if (s.startsPrimary()) {
            Node next;
            if (parseUnary(s, next))
                children.append(next);
            continue;
        }
        break;
    }

    if (children.isEmpty())
        return false;
    if (children.size() == 1) {
        out = children.first();
        return true;
    }
    out = Node();
    out.kind = Node::And;
    out.children = children;
    out.implicit = implicit;
    return true;
}

bool parseOr(State &s, Node &out)
{
    QVector<Node> children;

    Node child;
    if (parseAnd(s, child))
        children.append(child);

    while (s.peek() && s.peek()->kind == Token::Or) {
        ++s.pos;
        Node next;
        if (!parseAnd(s, next)) {
            s.hadErrors = true; // dangling OR
            break;
        }
        children.append(next);
    }

    if (children.isEmpty())
        return false;
    if (children.size() == 1) {
        out = children.first();
        return true;
    }
    out = Node();
    out.kind = Node::Or;
    out.children = children;
    return true;
}

void treeFlags(const Node &node, bool &hasBoolean, bool &hasPhrase)
{
    if (node.kind == Node::And || node.kind == Node::Or) {
        // An implicit AND (juxtaposition) is not an explicit boolean.
        if (node.kind == Node::Or || !node.implicit)
            hasBoolean = true;
        for (const Node &child : node.children)
            treeFlags(child, hasBoolean, hasPhrase);
    } else if (node.kind == Node::Not) {
        hasBoolean = true;
        if (node.child)
            treeFlags(*node.child, hasBoolean, hasPhrase);
    } else if (node.kind == Node::Phrase) {
        hasPhrase = true;
    }
}

} // namespace

bool isFilterField(const QString &name)
{
    return filterFieldNames().contains(name.toLower());
}

QString filterFieldName(FilterField field)
{
    switch (field) {
    case FilterField::Site: return QStringLiteral("site");
    case FilterField::Domain: return QStringLiteral("domain");
    case FilterField::Title: return QStringLiteral("title");
    case FilterField::Type: return QStringLiteral("type");
    case FilterField::Lang: return QStringLiteral("lang");
    case FilterField::Tag: return QStringLiteral("tag");
    case FilterField::Before: return QStringLiteral("before");
    case FilterField::After: return QStringLiteral("after");
    }
    return QString();
}

Parsed parse(const QString &raw)
{
    Parsed parsed;
    parsed.raw = raw;

    bool lexErrors = false;
    State state;
    state.tokens = lex(raw, lexErrors);
    state.hadErrors = lexErrors;

    Node expr;
    if (parseOr(state, expr)) {
        parsed.hasExpr = true;
        parsed.expr = expr;
    }
    if (state.pos < state.tokens.size())
        state.hadErrors = true; // trailing junk

    parsed.filters = collectFilters(parsed.expr);
    treeFlags(parsed.expr, parsed.hasBoolean, parsed.hasPhrases);
    parsed.hasFilters = !parsed.filters.isEmpty();
    parsed.hadErrors = state.hadErrors;
    return parsed;
}

QVector<FilterClause> collectFilters(const Node &node, bool negated)
{
    QVector<FilterClause> out;
    if (node.kind == Node::Filter) {
        out.append({node.field, node.value, negated});
    } else if (node.kind == Node::Not) {
        if (node.child)
            out += collectFilters(*node.child, !negated);
    } else if (node.kind == Node::And || node.kind == Node::Or) {
        for (const Node &child : node.children)
            out += collectFilters(child, negated);
    }
    return out;
}

void collectTextLeaves(const Node &node, QStringList &terms, QStringList &phrases)
{
    if (node.kind == Node::Term) {
        terms.append(node.value);
    } else if (node.kind == Node::Phrase) {
        phrases.append(node.value);
    } else if (node.kind == Node::Not) {
        if (node.child)
            collectTextLeaves(*node.child, terms, phrases);
    } else if (node.kind == Node::And || node.kind == Node::Or) {
        for (const Node &child : node.children)
            collectTextLeaves(child, terms, phrases);
    }
}

QString textOnly(const Parsed &parsed)
{
    QStringList terms, phrases;
    if (parsed.hasExpr)
        collectTextLeaves(parsed.expr, terms, phrases);
    QStringList parts = terms;
    for (const QString &phrase : phrases)
        parts.append(QLatin1Char('"') + phrase + QLatin1Char('"'));
    return parts.join(QLatin1Char(' ')).trimmed();
}

} // namespace Query
