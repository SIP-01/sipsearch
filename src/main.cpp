// sipsearch — search the decentralized SIP-01 web index.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QThread>
#include <QUrl>

#include <cstdio>
#include <cstring>

#include "backend.h"
#include "theme.h"
#include "translator.h"

namespace {

// The same job the window does, for agents and scripts: no display, JSON out.
int runHeadless(const QStringList &arguments)
{
    QString query;
    int limit = 20;
    bool refresh = false;

    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);
        if (argument == "--refresh")
            refresh = true;
        else if (argument == "--limit" && i + 1 < arguments.size())
            limit = arguments.at(++i).toInt();
        else if (!argument.startsWith("--"))
            query = query.isEmpty() ? argument : query + QLatin1Char(' ') + argument;
    }

    Backend backend;
    // The query is set on the backend so the instant answer and the filter
    // chips are built the same way the window builds them.
    backend.setQuery(query);
    if (refresh || backend.totalDocuments() == 0) {
        backend.refresh();
        // The refresh runs a child process and reports back through the
        // event loop. processEvents returns as soon as the queue is empty,
        // so the wait has to be on the process itself, not on a timer.
        for (int i = 0; i < 1800 && backend.totalDocuments() == 0; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            QThread::msleep(50);
        }
    }

    const auto rows = backend.search(query, limit);
    QJsonArray out;
    for (const auto &row : rows)
        out.append(QJsonObject::fromVariantMap(row.toMap()));
    QJsonObject document{
        {"query", query},
        {"total_documents", backend.totalDocuments()},
        {"hits", out},
    };
    // The instant answer, when the query has one — an agent should get it
    // too, not just the person looking at the window.
    const QVariantMap instant = backend.instantAnswer();
    if (!instant.isEmpty())
        document.insert("instant_answer", QJsonObject::fromVariantMap(instant));
    std::printf("%s\n", QJsonDocument(document).toJson(QJsonDocument::Compact).constData());
    return rows.isEmpty() && instant.isEmpty() ? 1 : 0;
}

} // namespace

int main(int argc, char *argv[]) {
    bool headless = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--headless") == 0)
            headless = true;

    QStringList rest;
    for (int j = 0; j < argc; ++j) {
        if (std::strcmp(argv[j], "--headless") == 0)
            continue;
        rest.append(QString::fromUtf8(argv[j]));
    }

    // A headless run needs no display, but it still needs an event loop:
    // the index refresh runs a child process and reports back through it.
    // The application object must exist before any path is resolved, so it
    // is created here rather than after the headless branch.
    if (headless) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("sipsearch");
        return runHeadless(rest);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("sipsearch");
    app.setApplicationVersion("0.1.0");

    // Matches the window to sipsearch.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("sipsearch");
    app.setWindowIcon(QIcon::fromTheme("sipsearch"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every tr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    // A query on the command line opens the window already searching for
    // it, the way a search engine handles "firefox --search terms".
    {
        QString query;
        for (int i = 1; i < argc; ++i) {
            const QString argument = QString::fromUtf8(argv[i]);
            if (argument.startsWith("--"))
                continue;
            query = query.isEmpty() ? argument : query + QLatin1Char(' ') + argument;
        }
        if (!query.isEmpty())
            backend.setQuery(query);
    }

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
