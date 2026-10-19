/// @file tst_FileDetailDialog.cpp
/// @brief File Details dialog on a small screen, and the ED2K Link page's HTML format.
///
/// Issue #36: on an 800x600 monitor the dialog could not be shrunk to fit, and with
/// "HTML Format" ticked the link box rendered the anchor -- so it showed, and copied,
/// only the file name. Runs on an 800x600 offscreen screen set up in main().

#include "dialogs/FileDetailDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QCborMap>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QLayout>
#include <QPushButton>
#include <QScreen>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>

using namespace eMule;

namespace {

constexpr int kScreenWidth  = 800;
constexpr int kScreenHeight = 600;

QCborMap fileDetails()
{
    const QString hash = QStringLiteral("0123456789ABCDEF0123456789ABCDEF");
    return QCborMap{
        {QStringLiteral("fileName"), QStringLiteral("Some.Linux.Distribution-x86_64.iso")},
        {QStringLiteral("hash"), hash},
        {QStringLiteral("fileSize"), 734003200},
        {QStringLiteral("completedSize"), 367001600},
        {QStringLiteral("percentCompleted"), 50.0},
        {QStringLiteral("status"), QStringLiteral("Downloading")},
        {QStringLiteral("fileType"), QStringLiteral("Iso")},   // the 7th tab too
        {QStringLiteral("ed2kLink"),
         QStringLiteral("ed2k://|file|Some.Linux.Distribution-x86_64.iso|734003200|%1|/")
             .arg(hash)}};
}

} // namespace

class TestFileDetailDialog : public QObject {
    Q_OBJECT

private slots:
    void fitsOnAn800x600Screen();
    void htmlLinkShowsAndCopiesTheMarkup();
};

/// The window manager may not let the user drag a window smaller than its minimum, so
/// the minimum plus the title bar has to fit in the available screen height.
void TestFileDetailDialog::fitsOnAn800x600Screen()
{
    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    QCOMPARE(avail.height(), kScreenHeight);

    for (int tab = FileDetailDialog::General; tab <= FileDetailDialog::Ed2kLink; ++tab) {
        FileDetailDialog dlg(fileDetails(), FileDetailDialog::Tab(tab));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        QTest::qWait(10);   // the deferred re-fit

        const QString where = QStringLiteral("tab %1: min %2x%3, size %4x%5")
                                  .arg(tab)
                                  .arg(dlg.minimumWidth()).arg(dlg.minimumHeight())
                                  .arg(dlg.width()).arg(dlg.height());
        QVERIFY2(dlg.minimumHeight() <= avail.height() - 60, qPrintable(where));
        QVERIFY2(dlg.minimumWidth() <= avail.width() - 40, qPrintable(where));
        QVERIFY2(dlg.height() <= avail.height() - 60, qPrintable(where));

        // Not the preferred height: Qt's for a tab widget is its tallest preferred page,
        // and the link box and the lists ask for far more than they need
        QVERIFY2(dlg.minimumHeight() < dlg.layout()->totalSizeHint().height(),
                 qPrintable(where + QStringLiteral(", hint %1")
                                        .arg(dlg.layout()->totalSizeHint().height())));
    }
}

/// MFC shows the HTML source in the edit box; that text is what the button copies.
void TestFileDetailDialog::htmlLinkShowsAndCopiesTheMarkup()
{
    FileDetailDialog dlg(fileDetails(), FileDetailDialog::Ed2kLink);
    auto* edit = dlg.findChild<QTextEdit*>();
    QVERIFY(edit);
    auto* html = [&dlg]() -> QCheckBox* {
        for (auto* box : dlg.findChildren<QCheckBox*>())
            if (box->text() == FileDetailDialog::tr("HTML Format"))
                return box;
        return nullptr;
    }();
    QVERIFY(html);

    const QString markup = QStringLiteral(
        "<a href=\"ed2k://|file|Some.Linux.Distribution-x86_64.iso|734003200|"
        "0123456789ABCDEF0123456789ABCDEF|/\">Some.Linux.Distribution-x86_64.iso</a>");
    html->setChecked(true);
    dlg.applyEd2kLink(markup, false);   // the daemon's answer for the HTML variant
    QCOMPARE(edit->toPlainText(), markup);

    QPushButton* copy = nullptr;
    for (auto* button : dlg.findChildren<QPushButton*>())
        if (button->text() == FileDetailDialog::tr("Copy to Clipboard"))
            copy = button;
    QVERIFY(copy);
    copy->click();
    QCOMPARE(QGuiApplication::clipboard()->text(), markup);
}

int main(int argc, char* argv[])
{
    // An 800x600 screen for the offscreen platform
    QTemporaryDir dir;
    const QString config = dir.filePath(QStringLiteral("screen.json"));
    QFile file(config);
    if (!file.open(QIODevice::WriteOnly))
        return 1;
    file.write(QStringLiteral(R"({"screens":[{"name":"small","x":0,"y":0,"width":%1,"height":%2,)"
                              R"("logicalDpi":96,"logicalBaseDpi":96,"dpr":1}]})")
                   .arg(kScreenWidth).arg(kScreenHeight).toUtf8());
    file.close();
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen:configfile=%1").arg(config).toUtf8());

    QApplication app(argc, argv);
    TestFileDetailDialog test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_FileDetailDialog.moc"
