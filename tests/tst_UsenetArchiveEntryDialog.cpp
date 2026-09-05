/// @file tst_UsenetArchiveEntryDialog.cpp
/// @brief The chooser behind Preview on a multi-file Usenet archive set.
///
/// Driven through applyListing() with hand-built CBOR rather than a daemon:
/// what is worth pinning here is how a listing becomes rows — which of them a
/// user can reach, and which ordinal a click yields — and none of that needs a
/// download behind it.

#include "dialogs/UsenetArchiveEntryDialog.h"

#include <QCborArray>
#include <QCborMap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>

using namespace eMule;

namespace {

QCborMap entry(int index, const QString& name, qint64 size, bool playable,
               const QString& note = {})
{
    return QCborMap{
        {QStringLiteral("entry"),    index},
        {QStringLiteral("name"),     name},
        {QStringLiteral("size"),     size},
        {QStringLiteral("playable"), playable},
        {QStringLiteral("note"),     note},
    };
}

QCborMap listing(int status, const QCborArray& entries)
{
    return QCborMap{
        {QStringLiteral("status"),  status},
        {QStringLiteral("note"),    QString()},
        {QStringLiteral("entries"), entries},
    };
}

constexpr int kComplete = 2;
constexpr int kNotSeekable = 3;

} // namespace

class tst_UsenetArchiveEntryDialog : public QObject {
    Q_OBJECT

private slots:
    void unplayableRowsAreListedButCannotBeReached();
    void theChosenEntryIsTheOrdinalNotTheRow();
    void onePlayableFileIsAnsweredWithoutEverShowingAWindow();
    void anUnstreamableSetShowsTheReasonAndOffersNothing();
};

void tst_UsenetArchiveEntryDialog::unplayableRowsAreListedButCannotBeReached()
{
    UsenetArchiveEntryDialog dlg(nullptr, QStringLiteral("item"), 0);
    dlg.applyListing(listing(kComplete, {
        entry(0, QStringLiteral("intro.nfo"), 500, false, QStringLiteral("Not playable")),
        entry(1, QStringLiteral("S01E01.mkv"), 700000000, true),
        entry(2, QStringLiteral("S01E02.mkv"), 700000000, true),
    }));

    auto* tree = dlg.findChild<QTreeWidget*>();
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 3);

    // By name, not by row: the list sorts, so row order is not insertion order.
    const auto rowNamed = [tree](const QString& name) -> const QTreeWidgetItem* {
        const auto found = tree->findItems(name, Qt::MatchExactly, 0);
        return found.isEmpty() ? nullptr : found.first();
    };

    // The .nfo is visible — that is the point of listing it — but it is not a
    // thing the user can pick, and it says why.
    const QTreeWidgetItem* nfo = rowNamed(QStringLiteral("intro.nfo"));
    QVERIFY(nfo);
    QVERIFY(!(nfo->flags() & Qt::ItemIsSelectable));
    QVERIFY(!(nfo->flags() & Qt::ItemIsEnabled));
    QVERIFY(!nfo->text(2).isEmpty());
    QCOMPARE(nfo->foreground(0),
             dlg.palette().brush(QPalette::Disabled, QPalette::Text));

    // A playable row carries no explicit brush at all, so it follows the
    // palette through a theme change rather than being painted "normal".
    const QTreeWidgetItem* episode = rowNamed(QStringLiteral("S01E01.mkv"));
    QVERIFY(episode);
    QVERIFY(episode->flags() & Qt::ItemIsSelectable);
    QVERIFY(!episode->data(0, Qt::ForegroundRole).isValid());
}

void tst_UsenetArchiveEntryDialog::theChosenEntryIsTheOrdinalNotTheRow()
{
    UsenetArchiveEntryDialog dlg(nullptr, QStringLiteral("item"), 0);
    dlg.applyListing(listing(kComplete, {
        entry(0, QStringLiteral("intro.nfo"), 500, false, QStringLiteral("Not playable")),
        entry(1, QStringLiteral("small.mkv"), 1000, true),
        entry(2, QStringLiteral("large.mkv"), 900000, true),
    }));

    auto* tree = dlg.findChild<QTreeWidget*>();
    QVERIFY(tree);

    // Sorting by size puts entry 2 at the top. Reading the row index instead of
    // the stored ordinal is the likeliest bug in the whole dialog, and it only
    // shows up once the list has been re-ordered.
    tree->sortByColumn(1, Qt::DescendingOrder);
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("large.mkv"));
    tree->setCurrentItem(tree->topLevelItem(0));

    QSignalSpy chosen(&dlg, &UsenetArchiveEntryDialog::entryChosen);
    QPushButton* preview = nullptr;
    for (QPushButton* b : dlg.findChildren<QPushButton*>()) {
        if (b->text() == QStringLiteral("Preview"))
            preview = b;
    }
    QVERIFY(preview);
    QVERIFY(preview->isEnabled());
    preview->click();

    QCOMPARE(chosen.size(), 1);
    QCOMPARE(chosen.at(0).at(0).toInt(), 2);
    QCOMPARE(dlg.chosenEntry(), 2);
}

void tst_UsenetArchiveEntryDialog::onePlayableFileIsAnsweredWithoutEverShowingAWindow()
{
    auto* dlg = new UsenetArchiveEntryDialog(nullptr, QStringLiteral("item"), 0);
    QSignalSpy chosen(dlg, &UsenetArchiveEntryDialog::entryChosen);

    // The ordinary release: a feature and its .nfo. There is nothing to choose,
    // so Preview must behave exactly as it did before the chooser existed.
    dlg->applyListing(listing(kComplete, {
        entry(0, QStringLiteral("intro.nfo"), 500, false, QStringLiteral("Not playable")),
        entry(1, QStringLiteral("Some.Release.mkv"), 900000, true),
    }));

    QCOMPARE(chosen.size(), 1);
    QCOMPARE(chosen.at(0).at(0).toInt(), 1);
    QVERIFY(!dlg->isVisible());
}

void tst_UsenetArchiveEntryDialog::anUnstreamableSetShowsTheReasonAndOffersNothing()
{
    UsenetArchiveEntryDialog dlg(nullptr, QStringLiteral("item"), 0);

    QCborMap refused = listing(kNotSeekable, {});
    refused[QStringLiteral("note")] = QStringLiteral("Solid archive — cannot seek");
    dlg.applyListing(refused);

    auto* tree = dlg.findChild<QTreeWidget*>();
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 0);

    for (QPushButton* b : dlg.findChildren<QPushButton*>()) {
        if (b->text() == QStringLiteral("Preview"))
            QVERIFY(!b->isEnabled());
    }
}

QTEST_MAIN(tst_UsenetArchiveEntryDialog)
#include "tst_UsenetArchiveEntryDialog.moc"
