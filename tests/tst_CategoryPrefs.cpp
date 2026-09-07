/// @file tst_CategoryPrefs.cpp
/// @brief The `categories:` block, and the invariants the rest of the core
///        assumes about it.
///
/// A category's index is its identity — `part.met` stores it as `FT_CATEGORY` —
/// and its incoming directory decides where a finished download lands and which
/// folders are shared. Three things can go silently wrong there, so each has a
/// test here:
///
///   - **Index 0 must always exist and must never carry a path.** Everything
///     resolves an unset category to the global incoming dir by way of index 0;
///     a list that lost it, or one whose entry 0 acquired a folder, sends every
///     uncategorised download somewhere the user did not choose.
///   - **An unusable path must degrade, not disappear.** MFC falls back to the
///     global incoming dir rather than dropping the category
///     (srchybrid/Preferences.cpp:2478) — dropping it would renumber every
///     category behind it and re-file live downloads.
///   - **Resolution is a runtime question.** `incomingDirForCategory()` checks
///     that the folder still exists, because the user can delete or unmount it
///     between the load and the download completing.

#include "prefs/Preferences.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;

namespace {

DownloadCategory makeCategory(const QString& title, const QString& incoming = {})
{
    DownloadCategory cat;
    cat.title = title;
    cat.incomingPath = incoming;
    return cat;
}

QString readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace

class tst_CategoryPrefs : public QObject {
    Q_OBJECT

private slots:
    void init();

    void defaultsToOneAllCategory();
    void categoriesRoundTrip();
    void indexZeroNeverKeepsAPath();
    void missingFolderIsCreated();
    void reservedFolderFallsBackToIncoming();
    void listIsCapped();
    void untitledCategoryKeepsItsSlot();
    void incomingDirForCategoryResolves();
    void incomingDirForCategoryFollowsAVanishedFolder();
    void allIncomingDirsDeduplicates();

private:
    /// A Preferences with the temp dir as its config/incoming/temp roots, so
    /// isShareableDirectory() has something real to judge against.
    void setUpDirs(Preferences& p);

    QTemporaryDir m_dir;
    QString m_file;
    QString m_incoming;
};

void tst_CategoryPrefs::init()
{
    QVERIFY(m_dir.isValid());
    m_file = m_dir.filePath(QStringLiteral("preferences-%1.yml")
                                .arg(QTest::currentTestFunction()));
    m_incoming = m_dir.filePath(QStringLiteral("Incoming"));
    QVERIFY(QDir().mkpath(m_incoming));
}

void tst_CategoryPrefs::setUpDirs(Preferences& p)
{
    p.setConfigDir(m_dir.filePath(QStringLiteral("Config")));
    p.setIncomingDir(m_incoming);
    p.setTempDirs({m_dir.filePath(QStringLiteral("Temp"))});
}

void tst_CategoryPrefs::defaultsToOneAllCategory()
{
    Preferences p;
    QCOMPARE(p.categoryCount(), 1);
    QCOMPARE(p.categories().size(), 1);
    QVERIFY(p.categories().at(0).incomingPath.isEmpty());

    // An out-of-range index answers with a default-constructed category rather
    // than crashing, which is the whole reason category() returns by value.
    QVERIFY(p.category(7).title.isEmpty());
    QVERIFY(p.category(-1).title.isEmpty());
}

void tst_CategoryPrefs::categoriesRoundTrip()
{
    const QString movies = m_dir.filePath(QStringLiteral("Movies"));
    QVERIFY(QDir().mkpath(movies));

    {
        Preferences p;
        setUpDirs(p);

        DownloadCategory cat = makeCategory(QStringLiteral("Movies"), movies);
        cat.comment = QStringLiteral("Films and series");
        cat.autocat = QStringLiteral("mkv|avi");
        cat.autocatIsRegexp = false;
        cat.color = 0x00FF8800u;
        cat.prio = 2;
        cat.regexp = QStringLiteral("^S[0-9]+");
        // Persisted but not yet consulted — they must survive anyway, or the
        // view-filter modes could not land without a file migration.
        cat.filter = 18;
        cat.filterNeg = true;
        cat.care4all = true;
        cat.downloadInAlphabeticalOrder = true;

        p.setCategories({makeCategory(QStringLiteral("All")), cat});
        QVERIFY(p.saveTo(m_file));
    }

    Preferences p2;
    QVERIFY(p2.load(m_file));
    QCOMPARE(p2.categoryCount(), 2);

    const auto cat = p2.category(1);
    QCOMPARE(cat.title, QStringLiteral("Movies"));
    QCOMPARE(cat.incomingPath, QDir::cleanPath(movies));
    QCOMPARE(cat.comment, QStringLiteral("Films and series"));
    QCOMPARE(cat.autocat, QStringLiteral("mkv|avi"));
    QCOMPARE(cat.color, 0x00FF8800u);
    QCOMPARE(cat.prio, quint8(2));
    QCOMPARE(cat.regexp, QStringLiteral("^S[0-9]+"));
    QCOMPARE(cat.filter, 18);
    QVERIFY(cat.filterNeg);
    QVERIFY(cat.care4all);
    QVERIFY(cat.downloadInAlphabeticalOrder);

    // The list is a sequence, not a map: the order is what part.met refers to.
    QVERIFY(readAll(m_file).contains(QStringLiteral("categories:")));
}

void tst_CategoryPrefs::indexZeroNeverKeepsAPath()
{
    const QString elsewhere = m_dir.filePath(QStringLiteral("Elsewhere"));
    QVERIFY(QDir().mkpath(elsewhere));

    Preferences p;
    setUpDirs(p);
    p.setCategories({makeCategory(QStringLiteral("All"), elsewhere)});

    QCOMPARE(p.categoryCount(), 1);
    QVERIFY(p.categories().at(0).incomingPath.isEmpty());
    // ...so it still resolves to the global incoming dir.
    QCOMPARE(p.incomingDirForCategory(0), m_incoming);

    // An empty list is not representable either: something has to own the
    // downloads that carry category 0.
    p.setCategories({});
    QCOMPARE(p.categoryCount(), 1);
}

void tst_CategoryPrefs::missingFolderIsCreated()
{
    const QString fresh = m_dir.filePath(QStringLiteral("Fresh/Nested"));
    QVERIFY(!QDir(fresh).exists());

    Preferences p;
    setUpDirs(p);
    p.setCategories({makeCategory(QStringLiteral("All")),
                     makeCategory(QStringLiteral("Fresh"), fresh)});

    QVERIFY(QDir(fresh).exists());
    QCOMPARE(p.category(1).incomingPath, QDir::cleanPath(fresh));
    QCOMPARE(p.incomingDirForCategory(1), QDir::cleanPath(fresh));
}

void tst_CategoryPrefs::reservedFolderFallsBackToIncoming()
{
    Preferences p;
    setUpDirs(p);

    // eMule's own storage may not double as a category's incoming folder, and
    // neither may the global incoming dir itself. The category survives; only
    // its path is dropped, so it goes on resolving to the global folder.
    p.setCategories({makeCategory(QStringLiteral("All")),
                     makeCategory(QStringLiteral("Config"),
                                  m_dir.filePath(QStringLiteral("Config"))),
                     makeCategory(QStringLiteral("Temp"),
                                  m_dir.filePath(QStringLiteral("Temp"))),
                     makeCategory(QStringLiteral("Incoming"), m_incoming)});

    QCOMPARE(p.categoryCount(), 4);
    for (int i = 1; i < 4; ++i) {
        QVERIFY2(p.category(i).incomingPath.isEmpty(),
                 qPrintable(QStringLiteral("category %1 kept a reserved path").arg(i)));
        QCOMPARE(p.incomingDirForCategory(i), m_incoming);
    }
}

void tst_CategoryPrefs::listIsCapped()
{
    QList<DownloadCategory> many;
    many.append(makeCategory(QStringLiteral("All")));
    for (int i = 0; i < kMaxCategories + 20; ++i)
        many.append(makeCategory(QStringLiteral("Cat %1").arg(i)));

    Preferences p;
    setUpDirs(p);
    p.setCategories(many);

    QCOMPARE(p.categoryCount(), kMaxCategories);
}

void tst_CategoryPrefs::untitledCategoryKeepsItsSlot()
{
    Preferences p;
    setUpDirs(p);
    p.setCategories({makeCategory(QStringLiteral("All")),
                     makeCategory(QString()),
                     makeCategory(QStringLiteral("Third"))});

    // Dropping the untitled one would shift "Third" from index 2 to index 1 and
    // silently re-file every download that named it.
    QCOMPARE(p.categoryCount(), 3);
    QCOMPARE(p.category(1).displayName(), QStringLiteral("?"));
    QCOMPARE(p.category(2).title, QStringLiteral("Third"));
}

void tst_CategoryPrefs::incomingDirForCategoryResolves()
{
    const QString movies = m_dir.filePath(QStringLiteral("Movies"));
    QVERIFY(QDir().mkpath(movies));

    Preferences p;
    setUpDirs(p);
    p.setCategories({makeCategory(QStringLiteral("All")),
                     makeCategory(QStringLiteral("Movies"), movies),
                     makeCategory(QStringLiteral("NoFolder"))});

    QCOMPARE(p.incomingDirForCategory(0), m_incoming);
    QCOMPARE(p.incomingDirForCategory(1), QDir::cleanPath(movies));
    QCOMPARE(p.incomingDirForCategory(2), m_incoming);
    // A download naming a category that no longer exists still has to land
    // somewhere; MFC clamps the same case to 0 (srchybrid/PartFile.cpp:4434).
    QCOMPARE(p.incomingDirForCategory(99), m_incoming);
}

void tst_CategoryPrefs::incomingDirForCategoryFollowsAVanishedFolder()
{
    const QString movies = m_dir.filePath(QStringLiteral("Movies"));
    QVERIFY(QDir().mkpath(movies));

    Preferences p;
    setUpDirs(p);
    p.setCategories({makeCategory(QStringLiteral("All")),
                     makeCategory(QStringLiteral("Movies"), movies)});
    QCOMPARE(p.incomingDirForCategory(1), QDir::cleanPath(movies));

    // The user deletes or unmounts it while eMule runs. The stored path is
    // kept — the volume may come back — but a download completing right now
    // must not be moved into a directory that is not there.
    QVERIFY(QDir(movies).removeRecursively());
    QCOMPARE(p.incomingDirForCategory(1), m_incoming);
    QCOMPARE(p.category(1).incomingPath, QDir::cleanPath(movies));
}

void tst_CategoryPrefs::allIncomingDirsDeduplicates()
{
    const QString movies = m_dir.filePath(QStringLiteral("Movies"));
    QVERIFY(QDir().mkpath(movies));

    Preferences p;
    setUpDirs(p);
    p.setCategories({makeCategory(QStringLiteral("All")),
                     makeCategory(QStringLiteral("Movies"), movies),
                     // The same folder by a different spelling. Scanning it
                     // twice would hash every file in it twice.
                     makeCategory(QStringLiteral("Films"), movies + QStringLiteral("/")),
                     makeCategory(QStringLiteral("NoFolder"))});

    const QStringList dirs = p.allIncomingDirs();
    QCOMPARE(dirs.size(), 2);
    QCOMPARE(dirs.at(0), m_incoming);
    QCOMPARE(dirs.at(1), QDir::cleanPath(movies));
}

QTEST_MAIN(tst_CategoryPrefs)
#include "tst_CategoryPrefs.moc"
