#include "backend.h"

#include "engine.h"
#include "instant.h"
#include "query.h"
#include "ranker.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtGlobal>
#include <QUrl>
#include <algorithm>

namespace {

// The CLI that owns the protocol. It wraps the audited sip-01 and nostr
// skill scripts, so the app never reimplements the wire format.
QString searchCli()
{
    const QString custom = qEnvironmentVariable("SIP01_SEARCH");
    if (!custom.isEmpty())
        return custom;
    // QStandardPaths::HomeLocation is not the home directory on every
    // platform and under every application setup, so the passwd entry is
    // what the shell would resolve too.
    const QString home = QDir::homePath();
    return home + "/.local/bin/sip01-search";
}

QString cacheFile()
{
    // Not QStandardPaths::AppDataLocation: that path depends on the
    // application name, so a headless run would look in a different place
    // than the window and start from an empty corpus.
    const QString dir = QDir::homePath() + "/.local/share/sipsearch";
    QDir().mkpath(dir);
    return dir + "/corpus.json";
}

} // namespace

Backend::Backend(QObject *parent) : QObject(parent)
{
    loadCache();
    m_status = m_corpus.isEmpty()
        ? tr("No index yet — press R to fetch the SIP-01 index")
        : tr("%1 documents indexed").arg(m_corpus.size());
    rank();
}

Backend::~Backend()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

QString Backend::cachePath() const
{
    return cacheFile();
}

void Backend::loadCache()
{
    QFile file(cacheFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray rows = QJsonDocument::fromJson(file.readAll()).array();
    m_corpus.reserve(rows.size());
    for (const auto &row : rows) {
        const QJsonObject object = row.toObject();
        IndexDocument doc;
        doc.id = object.value("id").toString();
        doc.url = object.value("url").toString();
        doc.title = object.value("title").toString();
        doc.description = object.value("description").toString();
        doc.lang = object.value("lang").toString();
        for (const auto &topic : object.value("topics").toArray())
            doc.topics.append(topic.toString());
        doc.createdAt = static_cast<qint64>(object.value("created_at").toDouble());
        doc.indexers = object.value("indexers").toInt(1);
        if (doc.title.isEmpty())
            continue;
        Ranker::index(doc);
        m_corpus.append(doc);
    }
}

void Backend::saveCache()
{
    QJsonArray rows;
    for (const auto &doc : m_corpus) {
        QJsonObject object;
        object["id"] = doc.id;
        object["url"] = doc.url;
        object["title"] = doc.title;
        object["description"] = doc.description;
        object["lang"] = doc.lang;
        object["topics"] = QJsonArray::fromStringList(doc.topics);
        object["created_at"] = static_cast<double>(doc.createdAt);
        object["indexers"] = doc.indexers;
        rows.append(object);
    }
    QFile file(cacheFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(rows).toJson(QJsonDocument::Compact));
}

void Backend::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    m_selected = 0;

    // Parse once here so the chips and the error flag match what the
    // ranking will actually enforce.
    const Query::Parsed parsed = Query::parse(query);
    m_queryHadErrors = parsed.hadErrors;
    m_queryChips.clear();
    for (const Query::FilterClause &filter : parsed.filters) {
        QVariantMap chip;
        chip["field"] = Query::filterFieldName(filter.field);
        chip["value"] = filter.value;
        chip["negated"] = filter.negated;
        m_queryChips.append(chip);
    }

    // Some queries are answered outright rather than searched for.
    m_instantAnswer.clear();
    const Instant::QueryClass cls = Instant::classifyQuery(query);
    if (cls == Instant::QueryClass::Math) {
        double value = 0.0;
        if (Instant::evaluateMath(query, value)) {
            m_instantAnswer["kind"] = QStringLiteral("math");
            m_instantAnswer["title"] = Instant::formatMathResult(value);
            m_instantAnswer["detail"] = query.trimmed();
        }
    } else if (cls == Instant::QueryClass::Url) {
        const QString url = query.trimmed();
        m_instantAnswer["kind"] = QStringLiteral("url");
        m_instantAnswer["title"] = url;
        m_instantAnswer["detail"] = tr("Is this page in the index?");
    } else if (cls == Instant::QueryClass::Nip19) {
        m_instantAnswer["kind"] = QStringLiteral("nip19");
        m_instantAnswer["title"] = query.trimmed();
        m_instantAnswer["detail"] = tr("A Nostr entity");
    } else if (cls == Instant::QueryClass::Nip05) {
        m_instantAnswer["kind"] = QStringLiteral("nip05");
        m_instantAnswer["title"] = query.trimmed();
        m_instantAnswer["detail"] = tr("A NIP-05 address");
    }

    emit queryChanged();
    emit selectedChanged();
    rank();
}

void Backend::removeFilter(const QString &field, const QString &value)
{
    // Drop the matching "field:value" token and leave everything else.
    const QString needle = field + QLatin1Char(':') + value;
    QStringList kept;
    const QStringList parts = m_query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part.compare(needle, Qt::CaseInsensitive) == 0)
            continue;
        kept.append(part);
    }
    setQuery(kept.join(QLatin1Char(' ')));
}

void Backend::rank()
{
    m_results = search(m_query, 40);
    emit resultsChanged();
}

void Backend::setSelected(int index)
{
    if (index < 0 || index >= m_results.size())
        return;
    if (m_selected == index)
        return;
    m_selected = index;
    emit selectedChanged();
}

void Backend::refresh()
{
    if (m_busy)
        return;

    const QString cli = searchCli();
    if (!QFile::exists(cli)) {
        m_status = tr("sip01-search is not installed — see README");
        emit statusChanged();
        return;
    }

    m_busy = true;
    m_status = tr("Fetching the SIP-01 index…");
    emit statusChanged();

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, &Backend::onRefreshFinished);
    connect(m_process, &QProcess::errorOccurred, this, &Backend::onRefreshError);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        const QByteArray error = m_process->readAllStandardError();
        if (!error.isEmpty())
            qWarning("sip01-search: %s", error.constData());
    });
    // The dump is a few megabytes of JSON, more than a pipe buffer holds, so
    // drain it as it arrives or the child blocks writing and never finishes.
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_output += m_process->readAllStandardOutput();
    });
    // The index is a few thousand documents across four relays, so this takes
    // longer than a normal query.
    m_process->start(cli, {"--dump", "--timeout", "60"});
}

void Backend::onRefreshFinished(int code, QProcess::ExitStatus status)
{
    m_busy = false;
    const QByteArray error = m_process->readAllStandardError();
    if (code != 0 || status != QProcess::NormalExit) {
        m_status = tr("The index could not be reached");
        if (!error.isEmpty())
            qWarning("sip01-search failed: %s", error.constData());
        emit statusChanged();
        return;
    }

    const QByteArray output = m_output.isEmpty() ? m_process->readAllStandardOutput() : m_output;
    m_output.clear();
    const QJsonDocument document = QJsonDocument::fromJson(output);
    const QJsonObject root = document.object();
    const QJsonArray rows = root.value("hits").toArray();
    if (rows.isEmpty()) {
        m_status = tr("The index could not be read");
        emit statusChanged();
        return;
    }

    m_corpus.clear();
    m_corpus.reserve(rows.size());
    for (const auto &row : rows) {
        const QJsonObject object = row.toObject();
        IndexDocument doc;
        doc.id = object.value("d").toString();
        doc.url = object.value("url").toString();
        doc.title = object.value("title").toString();
        doc.description = object.value("description").toString();
        doc.lang = object.value("lang").toString();
        for (const auto &topic : object.value("topics").toArray())
            doc.topics.append(topic.toString());
        doc.createdAt = static_cast<qint64>(object.value("created_at").toDouble());
        // Independent indexers observing the same page are agreement, so the
        // distinct-author count is the core ranking signal.
        doc.indexers = object.value("indexers").toInt(1);
        if (doc.title.isEmpty())
            continue;
        Ranker::index(doc);
        m_corpus.append(doc);
    }

    saveCache();

    // Observations that failed the SIP-01 §18 check were discarded by the
    // CLI. Say so, so a clean run and a lossy run do not look alike.
    const int invalid = root.value("invalid_dropped").toInt(0);
    m_invalidDropped = invalid;
    m_status = invalid > 0
        ? tr("%1 documents indexed (%2 unverified discarded)").arg(m_corpus.size()).arg(invalid)
        : tr("%1 documents indexed").arg(m_corpus.size());
    emit statusChanged();
    rank();
}

void Backend::onRefreshError(QProcess::ProcessError error)
{
    m_busy = false;
    m_status = error == QProcess::FailedToStart
        ? tr("sip01-search could not be started")
        : tr("The index could not be reached");
    emit statusChanged();
}

void Backend::openSelected()
{
    if (m_selected >= 0 && m_selected < m_results.size())
        openResult(m_selected);
}

void Backend::openResult(int index)
{
    if (index < 0 || index >= m_results.size())
        return;
    const QString url = m_results.at(index).toMap().value("url").toString();
    if (url.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl(url));
}

void Backend::copyUrl(int index)
{
    if (index < 0 || index >= m_results.size())
        return;
    const QString url = m_results.at(index).toMap().value("url").toString();
    if (url.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(url);
}

QVariantList Backend::search(const QString &query, int limit) const
{
    QStringList terms, phrases;
    Ranker::parse(query, terms, phrases);

    // The structured query decides what matches; the ranker decides the
    // order. Filters and booleans are enforced here, so a document the
    // query does not actually match can never show up as an answer.
    //
    // The engine runs for plain queries too, not just structured ones. A
    // plain query is an implicit AND: "decentralized index" means a page
    // about both words, not a page about either. The ranker alone is an
    // OR — it ranks any page mentioning one of the terms — so skipping the
    // engine would let "index" match a page about helicopters.
    const Query::Parsed parsed = Query::parse(query);

    struct Scored {
        double score = 0;
        int index = 0;
    };

    QVector<Scored> scored;
    scored.reserve(m_corpus.size());
    for (int i = 0; i < m_corpus.size(); ++i) {
        const IndexDocument &doc = m_corpus.at(i);

        Engine::Doc view;
        view.url = doc.url;
        view.title = doc.title;
        view.description = doc.description;
        view.topics = doc.topics;
        view.language = doc.lang;
        view.observedAt = doc.createdAt;
        const Engine::Evaluation evaluation = Engine::evaluate(parsed, view);
        if (!evaluation.match)
            continue;

        const double value = Ranker::score(doc, terms, phrases);
        if (value <= 0.0)
            continue;
        scored.append({value, i});
    }
    std::sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.index < b.index;
    });

    QVariantList rows;
    for (int i = 0; i < scored.size() && i < limit; ++i) {
        const IndexDocument &doc = m_corpus.at(scored.at(i).index);
        QVariantMap row;
        row["title"] = doc.title;
        row["url"] = doc.url;
        row["description"] = doc.description;
        row["lang"] = doc.lang;
        row["topics"] = doc.topics;
        row["indexers"] = doc.indexers;
        row["score"] = scored.at(i).score;
        row["id"] = doc.id;
        rows.append(row);
    }
    return rows;
}
