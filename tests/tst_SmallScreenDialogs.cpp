/// @file tst_SmallScreenDialogs.cpp
/// @brief Every dialog on an 800x600 screen: it has to fit, and shrink to what it needs.
///
/// Issue #36: dialogs sized their minimum from the preferred height, so they opened
/// too tall for a small monitor, could not be shrunk, and showed lots of white space.
/// Runs on an 800x600 offscreen screen set up in main().

#include "dialogs/AddFriendDialog.h"
#include "dialogs/AddSourceDialog.h"
#include "dialogs/BugReportDialog.h"
#include "dialogs/CategoryDialog.h"
#include "dialogs/ClientDetailDialog.h"
#include "dialogs/CollectionCreateDialog.h"
#include "dialogs/CollectionViewDialog.h"
#include "dialogs/CoreConnectDialog.h"
#include "dialogs/FileDetailDialog.h"
#include "dialogs/FirstStartWizard.h"
#include "dialogs/ImportDownloadsDialog.h"
#include "dialogs/MetaAccountDialog.h"
#include "dialogs/NetworkInfoDialog.h"
#include "dialogs/NzbFileChooserDialog.h"
#include "dialogs/OptionsDialog.h"
#include "dialogs/PasteTextDialog.h"
#include "dialogs/SearchDetailDialog.h"
#include "dialogs/ToolbarCustomizeDialog.h"
#include "dialogs/UsenetArchiveEntryDialog.h"
#include "dialogs/UsenetDetailsDialog.h"
#include "dialogs/WebLinksDialog.h"
#include "app/ToolbarButtons.h"
#include "controls/LogWidget.h"
#include "files/Collection.h"
#include "prefs/DownloadCategory.h"
#include "utils/DialogSizing.h"

#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QFile>
#include <QLayout>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <memory>

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
        {QStringLiteral("fileType"), QStringLiteral("Iso")},
        {QStringLiteral("ed2kLink"),
         QStringLiteral("ed2k://|file|Some.Linux.Distribution-x86_64.iso|734003200|%1|/")
             .arg(hash)}};
}

QCborMap clientDetails()
{
    return QCborMap{{QStringLiteral("userName"), QStringLiteral("http://emule-project.net")},
                    {QStringLiteral("userHash"), QStringLiteral("0123456789ABCDEF0123456789ABCDEF")},
                    {QStringLiteral("software"), QStringLiteral("eMule v0.50a")},
                    {QStringLiteral("ip"), QStringLiteral("192.0.2.10")},
                    {QStringLiteral("port"), 4662}};
}

/// PasteTextDialog is abstract; the subclasses only differ in what Accept does.
class PasteText : public PasteTextDialog {
public:
    explicit PasteText(const Chrome& chrome) : PasteTextDialog(chrome) {}

protected:
    void onAccepted() override {}
};

struct Case {
    const char* name;
    std::function<std::unique_ptr<QWidget>()> make;
};

QList<Case> cases()
{
    QList<Case> list;
    for (int tab = FileDetailDialog::General; tab <= FileDetailDialog::Ed2kLink; ++tab)
        list.append({"FileDetailDialog", [tab] {
                         return std::make_unique<FileDetailDialog>(fileDetails(),
                                                                   FileDetailDialog::Tab(tab));
                     }});
    list.append({"ClientDetailDialog",
                 [] { return std::make_unique<ClientDetailDialog>(clientDetails()); }});
    list.append({"SearchDetailDialog",
                 [] { return std::make_unique<SearchDetailDialog>(fileDetails()); }});
    list.append({"AddFriendDialog", [] { return std::make_unique<AddFriendDialog>(); }});
    list.append({"AddSourceDialog",
                 [] { return std::make_unique<AddSourceDialog>(QStringLiteral("file.iso")); }});
    list.append({"CategoryDialog", [] {
                     return std::make_unique<CategoryDialog>(DownloadCategory{},
                                                             QStringLiteral("/tmp/incoming"));
                 }});
    list.append({"CoreConnectDialog", [] { return std::make_unique<CoreConnectDialog>(); }});
    list.append({"CollectionViewDialog",
                 [] { return std::make_unique<CollectionViewDialog>(Collection{}); }});
    list.append({"CollectionCreateDialog",
                 [] { return std::make_unique<CollectionCreateDialog>(nullptr); }});
    list.append({"ImportDownloadsDialog",
                 [] { return std::make_unique<ImportDownloadsDialog>(nullptr); }});
    list.append({"NetworkInfoDialog",
                 [] { return std::make_unique<NetworkInfoDialog>(nullptr); }});
    list.append({"NzbFileChooserDialog", [] {
                     return std::make_unique<NzbFileChooserDialog>(
                         nullptr, QStringList{QStringLiteral("/tmp/a.nzb")},
                         QHash<QString, QList<int>>{});
                 }});
    list.append({"PasteTextDialog", [] {
                     PasteTextDialog::Chrome chrome;
                     chrome.title = QStringLiteral("Paste");
                     chrome.label = QStringLiteral("Links:");
                     return std::make_unique<PasteText>(chrome);
                 }});
    list.append({"UsenetArchiveEntryDialog", [] {
                     return std::make_unique<UsenetArchiveEntryDialog>(
                         nullptr, QStringLiteral("item"), 0);
                 }});
    list.append({"UsenetDetailsDialog", [] {
                     return std::make_unique<UsenetDetailsDialog>(nullptr, QStringLiteral("item"),
                                                                  QString{});
                 }});
    list.append({"WebLinksDialog", [] {
                     return std::make_unique<WebLinksDialog>(QStringLiteral("row"), QCborMap{},
                                                             QByteArray{},
                                                             QStringLiteral("file.iso"));
                 }});
    list.append({"BugReportDialog", [] {
                     static LogWidget log;
                     return std::make_unique<BugReportDialog>(&log);
                 }});
    list.append({"ToolbarCustomizeDialog",
                 [] { return std::make_unique<ToolbarCustomizeDialog>(QList<ToolbarButtonId>{}); }});
    list.append({"MetaAccountDialog", [] {
                     return std::make_unique<MetaAccountDialog>(nullptr, QCborMap{},
                                                                MetaAccountDialog::Purpose::Manage);
                 }});
    list.append({"FirstStartWizard", [] { return std::make_unique<FirstStartWizard>(nullptr); }});
    list.append({"OptionsDialog", [] { return std::make_unique<OptionsDialog>(nullptr, nullptr); }});
    return list;
}

} // namespace

class TestSmallScreenDialogs : public QObject {
    Q_OBJECT

private slots:
    void everyDialogFits();
};

/// Per dialog, on an 800x600 screen:
/// - its layout minimum fits: nothing is cut off at the smallest size
/// - minimum and default size fit the screen
/// - a resizable one is not pinned at the full screen height
/// - no hand-picked floor far above what the content needs (the white space)
void TestSmallScreenDialogs::everyDialogFits()
{
    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    QCOMPARE(avail.height(), kScreenHeight);
    const QSize budget(avail.width() - 40, avail.height() - 60);   // DialogSizing's

    for (const Case& c : cases()) {
        auto dlg = c.make();
        dlg->show();
        QVERIFY(QTest::qWaitForWindowExposed(dlg.get()));
        QTest::qWait(10);   // the deferred re-fit

        QLayout*    layout    = dlg->layout();
        const QSize layoutMin = layout->totalMinimumSize();
        const int   need      = DialogSizing::neededHeight(layout, dlg->width());
        const bool  resizable = dlg->minimumSize() != dlg->maximumSize();
        const QString where =
            QStringLiteral("%1: min %2x%3, size %4x%5, layout min %6x%7, needs %8")
                .arg(QLatin1StringView(c.name))
                .arg(dlg->minimumWidth()).arg(dlg->minimumHeight())
                .arg(dlg->width()).arg(dlg->height())
                .arg(layoutMin.width()).arg(layoutMin.height())
                .arg(need);

        QVERIFY2(layoutMin.height() <= budget.height(), qPrintable(where));
        QVERIFY2(layoutMin.width() <= budget.width(), qPrintable(where));
        QVERIFY2(dlg->minimumHeight() <= budget.height(), qPrintable(where));
        QVERIFY2(dlg->minimumWidth() <= budget.width(), qPrintable(where));
        QVERIFY2(dlg->height() <= budget.height(), qPrintable(where));
        QVERIFY2(dlg->width() <= budget.width(), qPrintable(where));
        if (resizable)
            QVERIFY2(dlg->minimumHeight() < budget.height(), qPrintable(where));

        int cap = std::max(layoutMin.height(), budget.height() * 3 / 4);
        if (need <= budget.height())
            cap = std::max(cap, need);
        QVERIFY2(dlg->minimumHeight() <= cap, qPrintable(where));
    }
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
    TestSmallScreenDialogs test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_SmallScreenDialogs.moc"
