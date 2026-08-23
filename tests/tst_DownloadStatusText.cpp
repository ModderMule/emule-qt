/// @file tst_DownloadStatusText.cpp
/// @brief The Transfer panel's Status column — MFC CPartFile::getPartfileStatus
///        (srchybrid/PartFile.cpp:3412-3453) and getPartfileStatusRank (:3456-3476).
///
/// The daemon sends the status as the raw enum token, which is the right wire format
/// and the wrong thing to put in front of a user: "ready" and "empty" are internal
/// names for one user-visible distinction, "is anything actually arriving".

#include "controls/DownloadListModel.h"

#include <QTest>

using namespace eMule;

class tst_DownloadStatusText : public QObject {
    Q_OBJECT

private slots:
    void readyAndEmptySplitOnActiveSources();
    void pausedReadsStoppedWhenStopped();
    void errorReadsInsufficientOnACompletionError();
    void hashingCoversWaitingForHash();
    void completingNamesTheOperation();
    void rankGroupsTheWayMfcDoes();

private:
    /// The model exposes the mapping only through data(); build a one-row model and
    /// read the column back, which is also what the view does.
    [[nodiscard]] static QString textOf(const DownloadRow& row);
    [[nodiscard]] static int rankOf(const DownloadRow& row);
    [[nodiscard]] static DownloadRow rowWith(const QString& status);
};

DownloadRow tst_DownloadStatusText::rowWith(const QString& status)
{
    DownloadRow row;
    row.hash = QStringLiteral("00112233445566778899AABBCCDDEEFF");
    row.fileName = QStringLiteral("file.bin");
    row.status = status;
    return row;
}

QString tst_DownloadStatusText::textOf(const DownloadRow& row)
{
    DownloadListModel model;
    model.setDownloads({row});
    return model.data(model.index(0, DownloadListModel::ColStatus, {}), Qt::DisplayRole).toString();
}

int tst_DownloadStatusText::rankOf(const DownloadRow& row)
{
    DownloadListModel model;
    model.setDownloads({row});
    return model.data(model.index(0, DownloadListModel::ColStatus, {}), Qt::UserRole).toInt();
}

void tst_DownloadStatusText::readyAndEmptySplitOnActiveSources()
{
    // Both statuses mean "downloading, not paused". What the user is shown depends on
    // whether a source is actually sending, not on which of the two the core latched.
    for (const QString& status : {QStringLiteral("ready"), QStringLiteral("empty")}) {
        DownloadRow idle = rowWith(status);
        idle.transferringSrcCount = 0;
        QCOMPARE(textOf(idle), QStringLiteral("Waiting"));
        QCOMPARE(rankOf(idle), 3);

        DownloadRow active = rowWith(status);
        active.transferringSrcCount = 2;
        QCOMPARE(textOf(active), QStringLiteral("Downloading"));
        QCOMPARE(rankOf(active), 2);
    }
}

void tst_DownloadStatusText::pausedReadsStoppedWhenStopped()
{
    DownloadRow paused = rowWith(QStringLiteral("paused"));
    QCOMPARE(textOf(paused), QStringLiteral("Paused"));
    QCOMPARE(rankOf(paused), 5);

    // Stopped is a pause the user cannot resume by accident, and MFC names it.
    DownloadRow stopped = rowWith(QStringLiteral("paused"));
    stopped.isStopped = true;
    QCOMPARE(textOf(stopped), QStringLiteral("Stopped"));
    QCOMPARE(rankOf(stopped), 6);
}

void tst_DownloadStatusText::errorReadsInsufficientOnACompletionError()
{
    DownloadRow err = rowWith(QStringLiteral("error"));
    QCOMPARE(textOf(err), QStringLiteral("Error"));

    // A completion that ran out of room is not a generic error, and saying so is the
    // difference between "something broke" and "free some disk space".
    DownloadRow noSpace = rowWith(QStringLiteral("error"));
    noSpace.completionError = true;
    QCOMPARE(textOf(noSpace), QStringLiteral("Insufficient disk space"));
    QCOMPARE(rankOf(noSpace), 8);

    QCOMPARE(textOf(rowWith(QStringLiteral("insufficient"))),
             QStringLiteral("Insufficient disk space"));
    QCOMPARE(rankOf(rowWith(QStringLiteral("insufficient"))), 4);
}

void tst_DownloadStatusText::hashingCoversWaitingForHash()
{
    // MFC shows one word for both, because the gap between them is a thread start.
    QCOMPARE(textOf(rowWith(QStringLiteral("hashing"))), QStringLiteral("Hashing"));
    QCOMPARE(textOf(rowWith(QStringLiteral("waitingforhash"))), QStringLiteral("Hashing"));
    QCOMPARE(rankOf(rowWith(QStringLiteral("waitingforhash"))), 7);
}

void tst_DownloadStatusText::completingNamesTheOperation()
{
    QCOMPARE(textOf(rowWith(QStringLiteral("completing"))), QStringLiteral("Completing"));

    DownloadRow copying = rowWith(QStringLiteral("completing"));
    copying.fileOp = 2;   // PartFileOp::Copying
    QCOMPARE(textOf(copying), QStringLiteral("Completing (Copying)"));

    // An import overrides the status entirely, as in MFC.
    DownloadRow importing = rowWith(QStringLiteral("ready"));
    importing.fileOp = 4;  // PartFileOp::ImportParts
    QCOMPARE(textOf(importing), QStringLiteral("Importing part"));

    QCOMPARE(textOf(rowWith(QStringLiteral("complete"))), QStringLiteral("Complete"));
}

void tst_DownloadStatusText::rankGroupsTheWayMfcDoes()
{
    // Sorting the column must group by meaning, not alphabetically by wire token —
    // which is what returning the raw string did.
    DownloadRow active = rowWith(QStringLiteral("ready"));
    active.transferringSrcCount = 1;
    DownloadRow stopped = rowWith(QStringLiteral("paused"));
    stopped.isStopped = true;

    QCOMPARE(rankOf(rowWith(QStringLiteral("complete"))), 0);
    QCOMPARE(rankOf(rowWith(QStringLiteral("completing"))), 1);
    QCOMPARE(rankOf(active), 2);
    QCOMPARE(rankOf(rowWith(QStringLiteral("ready"))), 3);
    QCOMPARE(rankOf(rowWith(QStringLiteral("insufficient"))), 4);
    QCOMPARE(rankOf(rowWith(QStringLiteral("paused"))), 5);
    QCOMPARE(rankOf(stopped), 6);
    QCOMPARE(rankOf(rowWith(QStringLiteral("hashing"))), 7);
    QCOMPARE(rankOf(rowWith(QStringLiteral("error"))), 8);
}

QTEST_MAIN(tst_DownloadStatusText)
#include "tst_DownloadStatusText.moc"
