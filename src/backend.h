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

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    QString query() const { return m_query; }
    QString status() const { return m_status; }
    QVariantList results() const { return m_results; }
    int totalDocuments() const { return m_corpus.size(); }
    bool busy() const { return m_busy; }
    int selected() const { return m_selected; }
    void setSelected(int index);

    Q_INVOKABLE void setQuery(const QString &query);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openSelected();
    Q_INVOKABLE void openResult(int index);
    Q_INVOKABLE void copyUrl(int index);

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
    QByteArray m_output;
    QString m_query;
    QString m_status;
    int m_selected = 0;
    bool m_busy = false;
    QProcess *m_process = nullptr;
};
