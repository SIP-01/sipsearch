#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "ranker.h"

// The app's one job: search the web, with the SIP-01 index at its centre.
//
// The protocol lives in one place — the sip01-search CLI, which wraps the
// audited sip-01 and nostr skill scripts. This class asks that CLI to
// refresh the corpus, keeps the documents in memory, and ranks them locally
// so every keystroke is instant and works offline once the corpus is loaded.
//
// The index is young — a few thousand documents — so it cannot answer a
// search on its own. The app therefore federates: the sipsearch-fed helper
// runs Dsearch's provider layer (DuckDuckGo, Wikipedia, Hacker News,
// Stack Overflow, SearXNG, Brave) in Node and merges those results with
// the local corpus. Both halves are optional: the index works without the
// network, and the web works without the index.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString query READ query NOTIFY queryChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(int totalDocuments READ totalDocuments NOTIFY resultsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectedChanged)
    // The parsed query's filters, as chips: {field, value, negated}.
    Q_PROPERTY(QVariantList queryChips READ queryChips NOTIFY queryChanged)
    // True when the query has a problem the user should know about.
    Q_PROPERTY(bool queryHasError READ queryHasError NOTIFY queryChanged)
    // The instant answer for the query, when there is one: {kind, title,
    // detail}. Empty otherwise.
    Q_PROPERTY(QVariantMap instantAnswer READ instantAnswer NOTIFY queryChanged)
    // How many observations the last refresh discarded because they failed
    // the SIP-01 §18 verification check.
    Q_PROPERTY(int invalidDropped READ invalidDropped NOTIFY statusChanged)
    // Which half of the search produced a result: "index", "web" or "both".
    Q_PROPERTY(QString source READ source NOTIFY resultsChanged)
    // The engines that answered, as {id, count, ok}: shown as status chips
    // so the user can see which providers are actually working.
    Q_PROPERTY(QVariantList engines READ engines NOTIFY resultsChanged)
    // True while a federated search is running.
    Q_PROPERTY(bool searching READ searching NOTIFY statusChanged)
    // Whether the clearnet is searched alongside the local index.
    Q_PROPERTY(bool webEnabled READ webEnabled WRITE setWebEnabled NOTIFY webEnabledChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    QString query() const { return m_query; }
    QString status() const { return m_status; }
    QVariantList results() const { return m_results; }
    QVariantList queryChips() const { return m_queryChips; }
    bool queryHasError() const { return m_queryHadErrors; }
    QVariantMap instantAnswer() const { return m_instantAnswer; }
    int totalDocuments() const { return m_corpus.size(); }
    int invalidDropped() const { return m_invalidDropped; }
    bool busy() const { return m_busy; }
    bool searching() const { return m_searching; }
    QString source() const { return m_source; }
    QVariantList engines() const { return m_engines; }
    int selected() const { return m_selected; }
    void setSelected(int index);

    Q_INVOKABLE void setQuery(const QString &query);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openSelected();
    Q_INVOKABLE void openResult(int index);
    Q_INVOKABLE void copyUrl(int index);
    // Remove one filter from the query, leaving the rest intact.
    Q_INVOKABLE void removeFilter(const QString &field, const QString &value);
    // Search the clearnet too, not just the local index.
    Q_INVOKABLE void setWebEnabled(bool enabled);

    // The same ranking the window shows, as JSON. An agent asks the app the
    // way a person asks the window, and gets the same answer.
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 40) const;

public:
    bool webEnabled() const { return m_webEnabled; }

signals:
    void queryChanged();
    void statusChanged();
    void resultsChanged();
    void selectedChanged();
    void webEnabledChanged();

private:
    void loadCache();
    void saveCache();
    void rank();
    void onRefreshFinished(int code, QProcess::ExitStatus status);
    void onRefreshError(QProcess::ProcessError error);
    // Run the federation helper and merge its results in.
    void searchWeb();
    void onWebFinished(int code, QProcess::ExitStatus status);
    void onWebError(QProcess::ProcessError error);
    QString cachePath() const;

    QVector<IndexDocument> m_corpus;
    QVariantList m_results;
    // The index results on their own, before the web results are merged
    // in. Merging into the merged list would interleave twice.
    QVariantList m_indexResults;
    QVariantList m_queryChips;
    QVariantList m_engines;
    QVariantMap m_instantAnswer;
    QByteArray m_output;
    QByteArray m_webOutput;
    QString m_query;
    QString m_status;
    QString m_source;
    int m_selected = 0;
    int m_invalidDropped = 0;
    bool m_busy = false;
    bool m_searching = false;
    bool m_queryHadErrors = false;
    bool m_webEnabled = true;
    QProcess *m_process = nullptr;
    QProcess *m_webProcess = nullptr;
};
