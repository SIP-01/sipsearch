// sipsearch — search the decentralized SIP-01 web index.

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
    if (refresh || backend.totalDocuments() == 0) {
        backend.refresh();
        // The refresh is a child process; wait for it to land.
        for (int i = 0; i < 900 && backend.totalDocuments() == 0; ++i)
            QThread::msleep(100);
    }

    const auto rows = backend.search(query, limit);
    QJsonArray out;
    for (const auto &row : rows)
        out.append(QJsonObject::fromVariantMap(row.toMap()));
    const QJsonObject result{
        {"query", query},
        {"total_documents", backend.totalDocuments()},
        {"hits", out},
    };
    std::printf("%s\n", QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    return rows.isEmpty() ? 1 : 0;
}

} // namespace

int main(int argc, char *argv[]) {
    // A headless run needs no display, so it must not create a GUI app first.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") != 0)
            continue;
        QStringList rest;
        for (int j = 0; j < argc; ++j) {
            if (std::strcmp(argv[j], "--headless") == 0)
                continue;
            rest.append(QString::fromUtf8(argv[j]));
        }
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
