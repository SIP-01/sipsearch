#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "ranker.h"

// The app's one job: search the SIP-01 web index.
//
// The protocol lives in one place — the sip01-search CLI, which wraps the
// audited sip-01 and nostr skill scripts. This class asks that CLI to
// refresh the corpus, keeps the documents in memory, and ranks them locally
// so every keystroke is instant and works offline once the corpus is loaded.
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
    int selected() const { return m_selected; }
    void setSelected(int index);

    Q_INVOKABLE void setQuery(const QString &query);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openSelected();
    Q_INVOKABLE void openResult(int index);
    Q_INVOKABLE void copyUrl(int index);
    // Remove one filter from the query, leaving the rest intact.
    Q_INVOKABLE void removeFilter(const QString &field, const QString &value);

    // The same ranking the window shows, as JSON. An agent asks the app the
    // way a person asks the window, and gets the same answer.
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 40) const;

signals:
    void queryChanged();
    void statusChanged();
    void resultsChanged();
    void selectedChanged();

private:
    void loadCache();
    void saveCache();
    void rank();
    void onRefreshFinished(int code, QProcess::ExitStatus status);
    void onRefreshError(QProcess::ProcessError error);
    QString cachePath() const;

    QVector<IndexDocument> m_corpus;
    QVariantList m_results;
    QVariantList m_queryChips;
    QVariantMap m_instantAnswer;
    QByteArray m_output;
    QString m_query;
    QString m_status;
    int m_selected = 0;
    int m_invalidDropped = 0;
    bool m_busy = false;
    bool m_queryHadErrors = false;
    QProcess *m_process = nullptr;
};
