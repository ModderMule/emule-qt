/// @file tst_UiState.cpp
/// @brief uistate.yml round-trip of the search/transfer keys, and the one-time
///        import from the platform QSettings store older builds wrote (issue #25).

#include "app/UiState.h"

#include <QCoreApplication>
#include <QFile>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QTreeView>
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
    void language_roundTripAndFallback();
    void legacyStore_importedThenRemoved();
    void legacyStore_yamlValueWins();
    void headerState_newColumnsStayMovable();

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

// #75: the GUI's own copy, "" (system default) kept apart from never chosen
void tst_UiState::language_roundTripAndFallback()
{
    const QString prefsLang = QStringLiteral("zh_CN");
    QTemporaryDir cfg;
    {
        UiState s;
        s.load(cfg.path());
        QCOMPARE(s.language(prefsLang), prefsLang);   // unset: preferences.yml's
        s.setLanguage(QStringLiteral("en_US"));
        s.save();
    }
    {
        UiState r;
        r.load(cfg.path());
        QCOMPARE(r.language(prefsLang), QStringLiteral("en_US"));
        r.setLanguage(QString());
        r.save();
    }
    UiState r;
    r.load(cfg.path());
    QCOMPARE(r.language(prefsLang), QString());
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

/// A layout saved before columns were added (issue #61): the new columns must sit
/// inside the header's length, or they can't be grabbed for a drag.
void tst_UiState::headerState_newColumnsStayMovable()
{
    const auto lengthOk = [](const QHeaderView* header) {
        int total = 0;
        for (int i = 0; i < header->count(); ++i)
            total += header->sectionSize(i);
        return header->length() == total;
    };

    QStandardItemModel oldModel(1, 5);
    QTreeView oldView;
    oldView.setModel(&oldModel);
    oldView.header()->moveSection(4, 1);
    const QByteArray saved = oldView.header()->saveState();

    QStandardItemModel newModel(1, 8);
    // bindColumns() hides the default-hidden columns before the restore; that
    // is what leaves Qt's length short
    const auto hideNew = [](QHeaderView* header) {
        for (const int column : {5, 6, 7})
            header->setSectionHidden(column, true);
    };

    // Plain Qt: the bug this works around (drop the check once Qt fixes it)
    QTreeView plain;
    plain.setModel(&newModel);
    hideNew(plain.header());
    QVERIFY(plain.header()->restoreState(saved));
    const QByteArray broken = plain.header()->saveState();
    if (lengthOk(plain.header()))
        qInfo("Qt appends restored sections correctly now");
    else
        QVERIFY(!QTreeView().header()->restoreState(broken));

    QTreeView view;
    view.setModel(&newModel);
    auto* header = view.header();
    hideNew(header);
    QVERIFY(UiState::restoreHeaderState(header, saved));
    QCOMPARE(header->logicalIndex(1), 4);
    QVERIFY(lengthOk(header));
    const int last = header->sectionPosition(7) + header->sectionSize(7) / 2;
    QCOMPARE(header->logicalIndexAt(last), 7);

    // Its own state, and the one an earlier build saved, restore again
    QTreeView again;
    again.setModel(&newModel);
    QVERIFY(again.header()->restoreState(header->saveState()));
    QTreeView repaired;
    repaired.setModel(&newModel);
    hideNew(repaired.header());
    QVERIFY(UiState::restoreHeaderState(repaired.header(), broken));
    QVERIFY(lengthOk(repaired.header()));
    QCOMPARE(repaired.header()->logicalIndex(1), 4);
}

QTEST_MAIN(tst_UiState)
#include "tst_UiState.moc"
