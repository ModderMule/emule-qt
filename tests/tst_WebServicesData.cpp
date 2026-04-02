/// @file tst_WebServicesData.cpp
/// @brief Integration test — parse shipped data/config/webservices.dat.

#include "TestHelpers.h"
#include "utils/WebServices.h"

#include <QFile>
#include <QTest>
#include <QTextStream>

using namespace eMule;
using namespace eMule::testing;

class tst_WebServicesData : public QObject {
    Q_OBJECT

private slots:
    void loadShippedFile();
    void shippedFile_hasEmuleFaq();
    void parseComments();
    void parseFileMacros();
    void emptyAndMalformed();
};

// ---------------------------------------------------------------------------
// Test: load the shipped webservices.dat and verify it parses
// ---------------------------------------------------------------------------

void tst_WebServicesData::loadShippedFile()
{
    const QString srcPath = projectDataDir() + QStringLiteral("/config/webservices.dat");
    QVERIFY2(QFile::exists(srcPath),
             qPrintable(QStringLiteral("Missing test fixture: %1").arg(srcPath)));

    WebServices ws;
    QVERIFY(ws.loadFromFile(srcPath));
    QVERIFY2(!ws.services().empty(), "Expected at least one web service entry");
}

// ---------------------------------------------------------------------------
// Test: verify the known "eMule FAQ" entry is present
// ---------------------------------------------------------------------------

void tst_WebServicesData::shippedFile_hasEmuleFaq()
{
    const QString srcPath = projectDataDir() + QStringLiteral("/config/webservices.dat");
    WebServices ws;
    QVERIFY(ws.loadFromFile(srcPath));

    bool found = false;
    for (const auto& svc : ws.services()) {
        if (svc.label.contains(QStringLiteral("eMule FAQ"), Qt::CaseInsensitive)) {
            found = true;
            QVERIFY2(svc.urlTemplate.contains(QStringLiteral("emule-project.org")),
                     "eMule FAQ URL should point to emule-project.org");
            QVERIFY2(!svc.hasFileMacros,
                     "eMule FAQ entry should not have file macros");
            break;
        }
    }
    QVERIFY2(found, "Expected to find 'eMule FAQ' entry in webservices.dat");
}

// ---------------------------------------------------------------------------
// Test: comment lines are skipped
// ---------------------------------------------------------------------------

void tst_WebServicesData::parseComments()
{
    TempDir tmpDir;
    const QString path = tmpDir.filePath(QStringLiteral("webservices.dat"));

    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "# This is a comment\n"
            << "/ This is also a comment\n"
            << "## Another comment\n"
            << "\n"
            << "Valid Service,http://example.com/search?q=#hashid\n";
    }

    WebServices ws;
    QVERIFY(ws.loadFromFile(path));
    QCOMPARE(ws.services().size(), size_t(1));
    QCOMPARE(ws.services()[0].label, QStringLiteral("Valid Service"));
}

// ---------------------------------------------------------------------------
// Test: file macros are detected in URLs
// ---------------------------------------------------------------------------

void tst_WebServicesData::parseFileMacros()
{
    TempDir tmpDir;
    const QString path = tmpDir.filePath(QStringLiteral("webservices.dat"));

    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "With Hash,http://example.com/lookup?h=#hashid\n"
            << "With Size,http://example.com/size?s=#filesize\n"
            << "With Filename,http://example.com/name?n=#filename\n"
            << "With Clean,http://example.com/clean?n=#cleanfilename\n"
            << "No Macros,http://example.com/static\n";
    }

    WebServices ws;
    QVERIFY(ws.loadFromFile(path));
    QCOMPARE(ws.services().size(), size_t(5));

    QVERIFY(ws.services()[0].hasFileMacros);  // #hashid
    QVERIFY(ws.services()[1].hasFileMacros);  // #filesize
    QVERIFY(ws.services()[2].hasFileMacros);  // #filename
    QVERIFY(ws.services()[3].hasFileMacros);  // #cleanfilename
    QVERIFY(!ws.services()[4].hasFileMacros); // no macros
}

// ---------------------------------------------------------------------------
// Test: empty and malformed lines are handled gracefully
// ---------------------------------------------------------------------------

void tst_WebServicesData::emptyAndMalformed()
{
    TempDir tmpDir;
    const QString path = tmpDir.filePath(QStringLiteral("webservices.dat"));

    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << "\n"                          // empty line
            << "ab\n"                        // too short (< 5 chars)
            << "no comma here at all\n"      // no comma
            << ",http://missing-label.com\n" // comma at pos 0
            << "Label,\n"                    // empty URL after comma
            << "OK Service,http://ok.com\n"; // valid
    }

    WebServices ws;
    QVERIFY(ws.loadFromFile(path));
    QCOMPARE(ws.services().size(), size_t(1));
    QCOMPARE(ws.services()[0].label, QStringLiteral("OK Service"));
}

QTEST_GUILESS_MAIN(tst_WebServicesData)
#include "tst_WebServicesData.moc"
