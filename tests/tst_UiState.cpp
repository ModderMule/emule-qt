/// @file tst_UiState.cpp
/// @brief uistate.yml round-trip of the search/transfer keys, and the one-time
///        import from the platform QSettings store older builds wrote (issue #25).

#include "app/UiState.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;

class tst_UiState : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void roundTrip_searchAndTransferKeys();
    void fallbacks_whenNeverSaved();
    void legacyStore_importedThenRemoved();
    void legacyStore_yamlValueWins();

private:
    QTemporaryDir m_legacyDir;   ///< stands in for ~/.config
};

void tst_UiState::initTestCase()
{
    QVERIFY(m_legacyDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("eMule"));
    QCoreApplication::setApplicationName(QStringLiteral("GUI"));
    // Same code path on every OS, and never the real plist / registry / ~/.config
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_legacyDir.path());
}

void tst_UiState::roundTrip_searchAndTransferKeys()
{
    QTemporaryDir cfg;
    {
        UiState s;
        s.load(cfg.path());
        s.setSearchLastMethod(1);
        s.setSearchHistory({QStringLiteral("ubuntu"), QStringLiteral("a, b: \"c\"")});
        s.setTransferViews(2, 3);
        s.save();
    }
    UiState r;
    r.load(cfg.path());
    QCOMPARE(r.searchLastMethod(9), 1);
    QCOMPARE(r.searchHistory(), (QStringList{QStringLiteral("ubuntu"), QStringLiteral("a, b: \"c\"")}));
    QCOMPARE(r.transferTopView(9), 2);
    QCOMPARE(r.transferBottomView(9), 3);
}

void tst_UiState::fallbacks_whenNeverSaved()
{
    QTemporaryDir cfg;
    UiState s;
    s.load(cfg.path());
    QCOMPARE(s.searchLastMethod(3), 3);
    QCOMPARE(s.transferTopView(0), 0);
    QCOMPARE(s.transferBottomView(1), 1);
    QVERIFY(s.searchHistory().isEmpty());
}

void tst_UiState::legacyStore_importedThenRemoved()
{
    QString legacyFile;
    {
        QSettings old;
        old.setValue(QStringLiteral("search/lastMethodType"), 2);
        old.setValue(QStringLiteral("search/history"), QStringList{QStringLiteral("matrix")});
        old.setValue(QStringLiteral("transfer/topView"), 1);
        old.setValue(QStringLiteral("transfer/bottomView"), 2);
        old.sync();
        legacyFile = old.fileName();
    }
    QVERIFY(QFile::exists(legacyFile));

    QTemporaryDir cfg;
    {
        UiState s;
        s.load(cfg.path());
        QCOMPARE(s.searchLastMethod(9), 2);
        QCOMPARE(s.searchHistory(), QStringList{QStringLiteral("matrix")});
        QCOMPARE(s.transferTopView(9), 1);
        QCOMPARE(s.transferBottomView(9), 2);
    }
    QVERIFY(QSettings().allKeys().isEmpty());
#ifdef Q_OS_LINUX
    QVERIFY(!QFile::exists(legacyFile));
#endif

    // Written to uistate.yml at once, so a second start still has it
    UiState again;
    again.load(cfg.path());
    QCOMPARE(again.searchHistory(), QStringList{QStringLiteral("matrix")});
}

void tst_UiState::legacyStore_yamlValueWins()
{
    QTemporaryDir cfg;
    {
        UiState s;
        s.load(cfg.path());
        s.setSearchHistory({QStringLiteral("new")});
        s.save();
    }
    {
        QSettings old;
        old.setValue(QStringLiteral("search/history"), QStringList{QStringLiteral("old")});
        old.sync();
    }
    UiState s;
    s.load(cfg.path());
    QCOMPARE(s.searchHistory(), QStringList{QStringLiteral("new")});
    QVERIFY(QSettings().allKeys().isEmpty());
}

QTEST_MAIN(tst_UiState)
#include "tst_UiState.moc"
