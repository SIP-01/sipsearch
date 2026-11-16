#include "instant.h"

#include <QRegularExpression>
#include <QStringList>

namespace Instant {

namespace {

// Normalize the operator symbols humans type into ASCII.
QString normalizeMathInput(const QString &input)
{
    QString out = input;
    out.replace(QChar(0x00D7), QLatin1Char('*'));  // multiplication sign
    out.replace(QChar(0x00F7), QLatin1Char('/'));  // division sign
    out.replace(QChar(0x2212), QLatin1Char('-'));  // minus sign
    // A casual "x" between two operands is a multiplication.
    static const QRegularExpression casual(QStringLiteral("([\\d)])\\s*[xX]\\s*(?=[\\d(])"));
    out.replace(casual, QStringLiteral("\\1*"));
    return out;
}

struct Token {
    bool isNumber = false;
    double number = 0.0;
    QChar op;
};

bool tokenize(const QString &input, QVector<Token> &tokens)
{
    const int n = input.size();
    int i = 0;
    while (i < n) {
        const QChar ch = input.at(i);
        if (ch.isSpace()) {
            ++i;
            continue;
        }
        if (ch.isDigit() || ch == QLatin1Char('.')) {
            int start = i;
            bool seenDot = false;
            while (i < n && (input.at(i).isDigit() || (input.at(i) == QLatin1Char('.') && !seenDot))) {
                if (input.at(i) == QLatin1Char('.'))
                    seenDot = true;
                ++i;
            }
            bool ok = false;
            const double value = input.mid(start, i - start).toDouble(&ok);
            if (!ok)
                return false;
            tokens.append({true, value, QChar()});
            continue;
        }
        if (QStringLiteral("+-*/%^()").contains(ch)) {
            tokens.append({false, 0.0, ch});
            ++i;
            continue;
        }
        return false;
    }
    return true;
}

class Parser {
public:
    explicit Parser(const QVector<Token> &tokens) : m_tokens(tokens) {}

    bool parseExpression(double &value)
    {
        double left = 0.0;
        if (!parseTerm(left))
            return false;
        while (m_pos < m_tokens.size()) {
            const QChar op = m_tokens.at(m_pos).op;
            if (op != QLatin1Char('+') && op != QLatin1Char('-'))
                break;
            ++m_pos;
            double right = 0.0;
            if (!parseTerm(right))
                return false;
            left = op == QLatin1Char('+') ? left + right : left - right;
        }
        value = left;
        return true;
    }

    bool atEnd() const { return m_pos >= m_tokens.size(); }

private:
    bool parseTerm(double &value)
    {
        double left = 0.0;
        if (!parseFactor(left))
            return false;
        while (m_pos < m_tokens.size()) {
            const QChar op = m_tokens.at(m_pos).op;
            if (op != QLatin1Char('*') && op != QLatin1Char('/') && op != QLatin1Char('%'))
                break;
            ++m_pos;
            double right = 0.0;
            if (!parseFactor(right))
                return false;
            if (op == QLatin1Char('*')) {
                left *= right;
            } else if (op == QLatin1Char('/')) {
                if (qFuzzyIsNull(right))
                    return false; // division by zero
                left /= right;
            } else {
                if (qFuzzyIsNull(right))
                    return false;
                left = std::fmod(left, right);
            }
        }
        value = left;
        return true;
    }

    bool parseFactor(double &value)
    {
        // Unary minus.
        if (m_pos < m_tokens.size() && !m_tokens.at(m_pos).isNumber
            && m_tokens.at(m_pos).op == QLatin1Char('-')) {
            ++m_pos;
            double inner = 0.0;
            if (!parseFactor(inner))
                return false;
            value = -inner;
            return true;
        }
        if (m_pos < m_tokens.size() && !m_tokens.at(m_pos).isNumber
            && m_tokens.at(m_pos).op == QLatin1Char('+')) {
            ++m_pos;
            return parseFactor(value);
        }
        if (m_pos >= m_tokens.size())
            return false;

        // Power is right-associative: 2^3^2 is 2^(3^2).
        if (m_tokens.at(m_pos).isNumber) {
            const double base = m_tokens.at(m_pos).number;
            ++m_pos;
            if (m_pos < m_tokens.size() && m_tokens.at(m_pos).op == QLatin1Char('^')) {
                ++m_pos;
                double exponent = 0.0;
                if (!parseFactor(exponent))
                    return false;
                value = std::pow(base, exponent);
            } else {
                value = base;
            }
            return true;
        }

        if (m_tokens.at(m_pos).op == QLatin1Char('(')) {
            ++m_pos;
            double inner = 0.0;
            if (!parseExpression(inner))
                return false;
            if (m_pos >= m_tokens.size() || m_tokens.at(m_pos).op != QLatin1Char(')'))
                return false;
            ++m_pos;
            value = inner;
            return true;
        }

        return false;
    }

    QVector<Token> m_tokens;
    int m_pos = 0;
};

} // namespace

bool isMathQuery(const QString &query)
{
    const QString q = normalizeMathInput(query.trimmed());
    if (q.size() < 3 || q.size() > 64)
        return false;

    // Percent-of sugar: "15% of 80".
    static const QRegularExpression percentOf(
        QStringLiteral("^([\\d.]+)\\s*%\\s*of\\s*([\\d.]+)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (percentOf.match(q).hasMatch())
        return true;

    // Only arithmetic characters allowed.
    static const QRegularExpression allowed(QStringLiteral("^[\\d\\s.+\\-*/()%^]+$"));
    if (!allowed.match(q).hasMatch())
        return false;

    // Must contain a binary operator between digits or parentheses.
    static const QRegularExpression binary(QStringLiteral("\\d\\s*[+\\-*/%^]\\s*[\\d(]"));
    static const QRegularExpression afterClose(QStringLiteral("\\)\\s*[+\\-*/%^]"));
    if (!binary.match(q).hasMatch() && !afterClose.match(q).hasMatch())
        return false;

    // Must contain a digit.
    return q.contains(QRegularExpression(QStringLiteral("\\d")));
}

bool evaluateMath(const QString &query, double &value)
{
    const QString q = normalizeMathInput(query.trimmed());

    // Percent-of sugar: "15% of 80" -> (15 / 100) * 80.
    static const QRegularExpression percentOf(
        QStringLiteral("^([\\d.]+)\\s*%\\s*of\\s*([\\d.]+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = percentOf.match(q);
    if (match.hasMatch()) {
        bool percentOk = false;
        bool baseOk = false;
        const double percent = match.captured(1).toDouble(&percentOk);
        const double base = match.captured(2).toDouble(&baseOk);
        if (!percentOk || !baseOk)
            return false;
        value = (percent / 100.0) * base;
        return true;
    }

    QVector<Token> tokens;
    if (!tokenize(q, tokens))
        return false;
    Parser parser(tokens);
    double result = 0.0;
    if (!parser.parseExpression(result))
        return false;
    if (!parser.atEnd())
        return false; // trailing garbage
    if (!std::isfinite(result))
        return false;
    value = result;
    return true;
}

QString formatMathResult(double value)
{
    // Round to 10 significant decimals to kill float noise.
    const double rounded = std::round(value * 1e10) / 1e10;

    // Only whole numbers get the thousands separator. A value like 0.3 is
    // not an integer, even though it has no fractional part in its text.
    if (std::trunc(rounded) == rounded && std::abs(rounded) < 1e15) {
        const qint64 whole = static_cast<qint64>(rounded);
        const QString digits = QString::number(qAbs(whole));
        QString grouped;
        int count = 0;
        for (int i = digits.size() - 1; i >= 0; --i) {
            grouped.prepend(digits.at(i));
            if (++count % 3 == 0 && i > 0)
                grouped.prepend(QLatin1Char(','));
        }
        return (whole < 0 ? QStringLiteral("-") : QString()) + grouped;
    }

    // The digit count is checked on a copy — remove() mutates in place, so
    // doing it on the returned string would strip the decimal point.
    QString text = QString::number(rounded);
    if (QString(text).remove(QLatin1Char('-')).remove(QLatin1Char('.')).size() > 12)
        text = QString::number(rounded, 'g', 10);
    return text;
}

QueryClass classifyQuery(const QString &query)
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return QueryClass::Text;

    // A NIP-19 bech32 entity. The checksum is not verified here — a
    // bech32-looking string that fails to decode still falls through.
    static const QRegularExpression nip19(
        QStringLiteral("^(npub1|nprofile1|note1|nevent1|naddr1)[02-9ac-hj-np-z]+$"),
        QRegularExpression::CaseInsensitiveOption);
    if (nip19.match(q).hasMatch())
        return QueryClass::Nip19;

    // NIP-05 address: name@domain.tld.
    static const QRegularExpression nip05(
        QStringLiteral("^[\\w.+-]+@[\\w-]+(\\.[\\w-]+)+$"));
    if (nip05.match(q).hasMatch())
        return QueryClass::Nip05;

    // A URL with a scheme, or a domain with a path. A bare domain stays
    // text — searching "wikipedia.org" should still run the search.
    static const QRegularExpression url(
        QStringLiteral("^(https?:\\/\\/\\S+|\\b[\\w-]+(\\.[\\w-]+)+\\/[^\\s]*)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (url.match(q).hasMatch())
        return QueryClass::Url;

    if (isMathQuery(q))
        return QueryClass::Math;

    return QueryClass::Text;
}

QString className(QueryClass cls)
{
    switch (cls) {
    case QueryClass::Text: return QStringLiteral("text");
    case QueryClass::Math: return QStringLiteral("math");
    case QueryClass::Url: return QStringLiteral("url");
    case QueryClass::Nip19: return QStringLiteral("nip19");
    case QueryClass::Nip05: return QStringLiteral("nip05");
    }
    return QStringLiteral("text");
}

} // namespace Instant
