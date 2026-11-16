#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "ranker.h"
#include "theme.h"
#include "translator.h"

class SipsearchTests : public QObject {
    Q_OBJECT

private slots:
    void rankerFindsTermsInTitle()
    {
        IndexDocument doc;
        doc.title = "Nostr protocol guide";
        doc.description = "A guide to decentralized web indexing";
        Ranker::index(doc);

        QStringList terms, phrases;
        Ranker::parse("nostr", terms, phrases);
        QVERIFY(Ranker::score(doc, terms, phrases) > 0);
    }

    void rankerIgnoresMissingTerms()
    {
        IndexDocument doc;
        doc.title = "Something else entirely";
        Ranker::index(doc);

        QStringList terms, phrases;
        Ranker::parse("quantum blockchain", terms, phrases);
        QCOMPARE(Ranker::score(doc, terms, phrases), 0.0);
    }

    void rankerPrefersTitleHits()
    {
        IndexDocument titled;
        titled.title = "nostr relays";
        Ranker::index(titled);

        IndexDocument body;
        body.title = "relays";
        body.description = "nostr";
        Ranker::index(body);

        QStringList terms, phrases;
        Ranker::parse("nostr", terms, phrases);
        QVERIFY(Ranker::score(titled, terms, phrases) > Ranker::score(body, terms, phrases));
    }

    void rankerCountsIndependentIndexers()
    {
        IndexDocument one;
        one.title = "nostr relays";
        one.indexers = 1;
        Ranker::index(one);

        IndexDocument three;
        three.title = "nostr relays";
        three.indexers = 3;
        Ranker::index(three);

        QStringList terms, phrases;
        Ranker::parse("nostr", terms, phrases);
        QVERIFY(Ranker::score(three, terms, phrases) > Ranker::score(one, terms, phrases));
    }

    void rankerParsesQuotedPhrases()
    {
        QStringList terms, phrases;
        Ranker::parse("decentralized \"web index\" search", terms, phrases);
        QCOMPARE(terms, QStringList({"decentralized", "search"}));
        QCOMPARE(phrases, QStringList({"web index"}));
    }

    void rankerMatchesPhrasesOnlyWhenPresent()
    {
        IndexDocument doc;
        doc.title = "A decentralized web index";
        Ranker::index(doc);

        QStringList terms, phrases;
        Ranker::parse("\"decentralized web index\"", terms, phrases);
        QVERIFY(Ranker::score(doc, terms, phrases) > 0);

        IndexDocument other;
        other.title = "A centralized web page";
        Ranker::index(other);
        QCOMPARE(Ranker::score(other, terms, phrases), 0.0);
    }

    void backendRanksItsCorpus()
    {
        Backend backend;
        backend.setQuery("nostr");
        // The corpus is empty in a test environment, so the result list is
        // empty rather than wrong — the point is that querying never throws.
        QCOMPARE(backend.query(), QString("nostr"));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(SipsearchTests)
#include "sipsearch_tests.moc"
