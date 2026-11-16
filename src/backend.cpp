#include "backend.h"

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
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return home + "/.local/bin/sip01-search";
}

QString cacheFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
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
    emit queryChanged();
    emit selectedChanged();
    rank();
}

void Backend::rank()
{
    QStringList terms, phrases;
    Ranker::parse(m_query, terms, phrases);

    struct Scored {
        double score = 0;
        int index = 0;
    };

    QVector<Scored> scored;
    if (!terms.isEmpty() || !phrases.isEmpty()) {
        scored.reserve(m_corpus.size());
        for (int i = 0; i < m_corpus.size(); ++i) {
            const double value = Ranker::score(m_corpus.at(i), terms, phrases);
            // A candidate that does not match locally is dropped, even when a
            // relay returned it. Showing unrelated pages as answers is worse
            // than showing nothing.
            if (value <= 0.0)
                continue;
            scored.append({value, i});
        }
        std::sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
            if (a.score != b.score)
                return a.score > b.score;
            return a.index < b.index;
        });
    }

    m_results.clear();
    const int limit = 40;
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
        m_results.append(row);
    }

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
    if (code != 0 || status != QProcess::NormalExit) {
        m_status = tr("The index could not be reached");
        emit statusChanged();
        return;
    }

    const QByteArray output = m_output.isEmpty() ? m_process->readAllStandardOutput() : m_output;
    m_output.clear();
    const QJsonArray rows = QJsonDocument::fromJson(output)
                                .object().value("hits").toArray();

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
    m_status = tr("%1 documents indexed").arg(m_corpus.size());
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
