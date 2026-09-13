/// @file tst_UsenetDetailsDialog.cpp
/// @brief The release details view behind double-click and the Details… entry.
///
/// Driven through applyDetails() with hand-built CBOR rather than a daemon: what
/// is worth pinning is how one GetUsenetItemDetails reply becomes a header and a
/// table — in particular the two ways a wrong answer here is *plausible*, an
/// unassessed health reading as 100% and a per-release fact quietly taken from
/// one file when the files disagree.

#include "dialogs/UsenetDetailsDialog.h"

#include <QCborArray>
#include <QCborMap>
#include <QLabel>
#include <QTest>
#include <QTreeWidget>

using namespace eMule;

namespace {

QCborMap file(const QString& name, qint64 size, int percent, int done, int total,
              int missing, bool isPar2, const QString& poster,
              const QStringList& groups, qint64 date = 1700000000)
{
    QCborArray g;
    for (const QString& s : groups)
        g.append(s);
    return QCborMap{
        {QStringLiteral("name"),           name},
        {QStringLiteral("size"),           size},
        {QStringLiteral("percent"),        percent},
        {QStringLiteral("doneSegments"),   done},
        {QStringLiteral("segmentCount"),   total},
        {QStringLiteral("missingSegments"), missing},
        {QStringLiteral("isPar2"),         isPar2},
        {QStringLiteral("poster"),         poster},
        {QStringLiteral("groups"),         g},
        {QStringLiteral("date"),           date},
        {QStringLiteral("finalPath"),      QString()},
        {QStringLiteral("relPath"),        QString()},
        {QStringLiteral("subject"),        name},
        {QStringLiteral("finalized"),      percent >= 100},
    };
}

QCborMap details(const QCborArray& files, int healthPercent, bool healthProbed = false)
{
    return QCborMap{
        {QStringLiteral("id"),           QStringLiteral("item")},
        {QStringLiteral("name"),         QStringLiteral("Some.Release.2026")},
        {QStringLiteral("status"),       1},
        {QStringLiteral("statusText"),   QStringLiteral("Downloading")},
        {QStringLiteral("totalBytes"),   qint64(4200000000LL)},
        {QStringLiteral("segmentCount"), 6102},
        {QStringLiteral("doneSegments"), 5984},
        {QStringLiteral("healthPercent"), healthPercent},
        {QStringLiteral("healthProbed"), healthProbed},
        {QStringLiteral("files"),        files},
    };
}

/// Header values are found by object name. Finding them by position among the
/// form's children would pass for the wrong reason: QFormLayout creates its own
/// label per row, so the child order interleaves labels and values.
QLabel* headerValue(QWidget* w, const char* name)
{
    return w->findChild<QLabel*>(QString::fromLatin1(name));
}

} // namespace

class tst_UsenetDetailsDialog : public QObject {
    Q_OBJECT

private slots:
    void everyNzbFileBecomesARow();
    void anUnassessedHealthNeverRendersAsAHundredPercent();
    void aReleaseWideFactSaysSoWhenTheFilesDisagree();
    void sizeAndArticleColumnsSortByMagnitudeNotByText();
    void theNameColumnStillSortsAsAName();
    void recoveryVolumesAreListedButGreyed();
};

void tst_UsenetDetailsDialog::everyNzbFileBecomesARow()
{
    UsenetDetailsDialog dlg(nullptr, QStringLiteral("item"), QString());

    dlg.applyDetails(details({
        file(QStringLiteral("rel.part01.rar"), 50000000, 100, 512, 512, 0, false,
             QStringLiteral("foo@bar.net"), {QStringLiteral("alt.binaries.x")}),
        file(QStringLiteral("rel.part02.rar"), 50000000, 64, 328, 512, 0, false,
             QStringLiteral("foo@bar.net"), {QStringLiteral("alt.binaries.x")}),
    }, 98, true));

    auto* tree = dlg.findChild<QTreeWidget*>();
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 2);

    // The articles cell is the one the queue list cannot show at all, and is the
    // reason this dialog exists.
    bool found = false;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(UsenetDetailsDialog::ColName)
            == QStringLiteral("rel.part02.rar")) {
            QCOMPARE(tree->topLevelItem(i)->text(UsenetDetailsDialog::ColArticles),
                     QStringLiteral("328/512"));
            found = true;
        }
    }
    QVERIFY(found);
}

void tst_UsenetDetailsDialog::anUnassessedHealthNeverRendersAsAHundredPercent()
{
    UsenetDetailsDialog dlg(nullptr, QStringLiteral("item"), QString());

    // -1 is the daemon's "nobody asked". Rendering it as a percentage — any
    // percentage — states a fact the daemon explicitly declined to state.
    dlg.applyDetails(details({
        file(QStringLiteral("a.bin"), 1000, 0, 0, 10, 0, false,
             QStringLiteral("p"), {QStringLiteral("g")}),
    }, -1));

    QLabel* health = headerValue(&dlg, "valueHealth");
    QVERIFY(health);
    QCOMPARE(health->text(), QStringLiteral("—"));

    dlg.applyDetails(details({
        file(QStringLiteral("a.bin"), 1000, 0, 0, 10, 0, false,
             QStringLiteral("p"), {QStringLiteral("g")}),
    }, 100, true));
    QCOMPARE(health->text(), QStringLiteral("100%"));
}

void tst_UsenetDetailsDialog::aReleaseWideFactSaysSoWhenTheFilesDisagree()
{
    UsenetDetailsDialog dlg(nullptr, QStringLiteral("item"), QString());

    // One poster across every file is the normal case and shows as itself.
    dlg.applyDetails(details({
        file(QStringLiteral("a.bin"), 1000, 0, 0, 10, 0, false,
             QStringLiteral("one@example.com"), {QStringLiteral("alt.binaries.x")}),
        file(QStringLiteral("b.bin"), 1000, 0, 0, 10, 0, false,
             QStringLiteral("one@example.com"), {QStringLiteral("alt.binaries.x")}),
    }, -1));

    QLabel* poster = headerValue(&dlg, "valuePoster");
    QVERIFY(poster);
    QCOMPARE(poster->text(), QStringLiteral("one@example.com"));

    // Two posters must not silently become one: naming the first alone is a
    // claim about the second file that nothing supports.
    dlg.applyDetails(details({
        file(QStringLiteral("a.bin"), 1000, 0, 0, 10, 0, false,
             QStringLiteral("one@example.com"), {QStringLiteral("alt.binaries.x")}),
        file(QStringLiteral("b.bin"), 1000, 0, 0, 10, 0, false,
             QStringLiteral("two@example.com"), {QStringLiteral("alt.binaries.y")}),
    }, -1));

    QVERIFY(poster->text().startsWith(QStringLiteral("one@example.com")));
    QVERIFY2(poster->text() != QStringLiteral("one@example.com"),
             "a disagreement between files was reported as a single poster");
}

void tst_UsenetDetailsDialog::sizeAndArticleColumnsSortByMagnitudeNotByText()
{
    UsenetDetailsDialog dlg(nullptr, QStringLiteral("item"), QString());

    // 9 MB sorts above 10 MB on the formatted text and below it on the bytes.
    dlg.applyDetails(details({
        file(QStringLiteral("big.bin"), 10LL * 1024 * 1024, 0, 90, 100, 0, false,
             QStringLiteral("p"), {QStringLiteral("g")}),
        file(QStringLiteral("small.bin"), 9LL * 1024 * 1024, 0, 9, 100, 0, false,
             QStringLiteral("p"), {QStringLiteral("g")}),
    }, -1));

    auto* tree = dlg.findChild<QTreeWidget*>();
    QVERIFY(tree);
    tree->sortByColumn(UsenetDetailsDialog::ColSize, Qt::AscendingOrder);

    QCOMPARE(tree->topLevelItem(0)->text(UsenetDetailsDialog::ColName),
             QStringLiteral("small.bin"));
    QCOMPARE(tree->topLevelItem(1)->text(UsenetDetailsDialog::ColName),
             QStringLiteral("big.bin"));
}

void tst_UsenetDetailsDialog::theNameColumnStillSortsAsAName()
{
    // The Name cell carries the published path under Qt::UserRole for the
    // double-click handler. A comparator reading Qt::UserRole would compare two
    // paths as numbers — 0 against 0 — so every pair looks equal and the column
    // stops sorting altogether. That is why the sort key lives under its own
    // role; this is the case that noticed.
    UsenetDetailsDialog dlg(nullptr, QStringLiteral("item"), QString());

    dlg.applyDetails(details({
        file(QStringLiteral("zulu.bin"), 1000, 0, 0, 10, 0, false,
             QStringLiteral("p"), {QStringLiteral("g")}),
        file(QStringLiteral("alpha.bin"), 1000, 0, 0, 10, 0, false,
             QStringLiteral("p"), {QStringLiteral("g")}),
    }, -1));

    auto* tree = dlg.findChild<QTreeWidget*>();
    QVERIFY(tree);

    tree->sortByColumn(UsenetDetailsDialog::ColName, Qt::AscendingOrder);
    QCOMPARE(tree->topLevelItem(0)->text(UsenetDetailsDialog::ColName),
             QStringLiteral("alpha.bin"));

    tree->sortByColumn(UsenetDetailsDialog::ColName, Qt::DescendingOrder);
    QCOMPARE(tree->topLevelItem(0)->text(UsenetDetailsDialog::ColName),
             QStringLiteral("zulu.bin"));
}

void tst_UsenetDetailsDialog::recoveryVolumesAreListedButGreyed()
{
    UsenetDetailsDialog dlg(nullptr, QStringLiteral("item"), QString());

    dlg.applyDetails(details({
        file(QStringLiteral("rel.part01.rar"), 50000000, 100, 512, 512, 0, false,
             QStringLiteral("p"), {QStringLiteral("g")}),
        file(QStringLiteral("rel.vol000+01.par2"), 1200000, 0, 0, 14, 0, true,
             QStringLiteral("p"), {QStringLiteral("g")}),
    }, -1));

    auto* tree = dlg.findChild<QTreeWidget*>();
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 2);

    const QBrush grey = dlg.palette().brush(QPalette::Disabled, QPalette::Text);
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree->topLevelItem(i);
        const bool isPar2 =
            item->text(UsenetDetailsDialog::ColName).endsWith(QStringLiteral(".par2"));
        if (isPar2) {
            QCOMPARE(item->foreground(UsenetDetailsDialog::ColName).color(), grey.color());
        } else {
            // A payload row carries no explicit brush at all, so it follows the
            // theme rather than being painted a colour that happens to match it.
            QCOMPARE(item->foreground(UsenetDetailsDialog::ColName), QBrush());
        }
    }
}

QTEST_MAIN(tst_UsenetDetailsDialog)
#include "tst_UsenetDetailsDialog.moc"
