/// @file tst_CategoryDialog.cpp
/// @brief The add/edit category dialog — MFC's CCatDialog.
///
/// Two things here are easy to get wrong and impossible to notice by looking:
///
///   - **Cancel must change nothing.** MFC edits the live `Category_Struct`
///     through a pointer into `catArr` and only afterwards asks whether the user
///     pressed OK (`srchybrid/CatDialog.cpp:45-52`), so a Cancel that follows a
///     failed path validation leaves the category half-edited. This dialog holds
///     a copy; the test pins that.
///   - **An empty incoming path is a valid answer**, meaning "the global
///     incoming directory". It is also the only way back after one has been set,
///     so it must not be treated as a validation failure.

#include "dialogs/AddFriendDialog.h"
#include "dialogs/CategoryDialog.h"
#include "dialogs/CollectionCategory.h"
#include "dialogs/NetworkInfoDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QAction>
#include <QCborArray>
#include <QCborMap>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;

namespace {

/// The OK button, which is what the validation hangs off.
QPushButton* okButton(CategoryDialog& dlg)
{
    auto* box = dlg.findChild<QDialogButtonBox*>();
    return box ? box->button(QDialogButtonBox::Ok) : nullptr;
}

/// Line edits in construction order: title, comment, incoming, autocat, regexp.
QList<QLineEdit*> edits(CategoryDialog& dlg)
{
    return dlg.findChildren<QLineEdit*>();
}

} // namespace

class tst_CategoryDialog : public QObject {
    Q_OBJECT

private slots:
    void populatesFromTheCategory();
    void acceptWritesTheEditedCopy();
    void rejectLeavesTheCategoryAlone();
    void emptyIncomingPathIsAccepted();
    void missingFolderIsCreatedOnAccept();
    void invalidRegexpBlocksAccept();
    void untitledCategoryBlocksAccept();
    void regexpIsTheViewFilter();
    void colourCanGoBackToDefault();

    // --- AddFriendDialog -----------------------------------------------------
    void addFriend_takesThePortFromTheAddressField();
    void addFriend_staysOpenUntilTheFriendIsAdded();
    void addFriend_detailsSheetIsWordedAsMfc();

    // --- CollectionViewDialog's category rule --------------------------------
    void collection_findsOrAddsItsCategory();

    // --- NetworkInfoDialog ---------------------------------------------------
    void networkInfo_listsServerFeaturesAndLanMode();

private:
    QTemporaryDir m_dir;
};

void tst_CategoryDialog::populatesFromTheCategory()
{
    DownloadCategory cat;
    cat.title = QStringLiteral("Movies");
    cat.comment = QStringLiteral("films");
    cat.incomingPath = m_dir.path();
    cat.autocat = QStringLiteral("mkv|avi");
    cat.autocatIsRegexp = true;
    cat.prio = 2;

    CategoryDialog dlg(cat, m_dir.path());
    const auto fields = edits(dlg);
    QCOMPARE(fields.size(), 5);
    QCOMPARE(fields.at(0)->text(), QStringLiteral("Movies"));
    QCOMPARE(fields.at(1)->text(), QStringLiteral("films"));
    QCOMPARE(fields.at(2)->text(), m_dir.path());
    QCOMPARE(fields.at(3)->text(), QStringLiteral("mkv|avi"));

    auto* check = dlg.findChild<QCheckBox*>();
    QVERIFY(check);
    QVERIFY(check->isChecked());

    // The combo's index is the priority value, as in MFC — kPrLow/Normal/High.
    auto* combo = dlg.findChild<QComboBox*>();
    QVERIFY(combo);
    QCOMPARE(combo->currentData().toUInt(), 2U);
}

void tst_CategoryDialog::acceptWritesTheEditedCopy()
{
    const QString movies = m_dir.filePath(QStringLiteral("Movies"));
    QVERIFY(QDir().mkpath(movies));

    CategoryDialog dlg(DownloadCategory{}, m_dir.path());
    const auto fields = edits(dlg);
    fields.at(0)->setText(QStringLiteral("  Movies  ")); // trimmed on the way out
    fields.at(2)->setText(movies);
    fields.at(3)->setText(QStringLiteral("mkv|avi"));

    okButton(dlg)->click();
    QCOMPARE(dlg.result(), int(QDialog::Accepted));

    const auto cat = dlg.category();
    QCOMPARE(cat.title, QStringLiteral("Movies"));
    QCOMPARE(cat.incomingPath, QDir::cleanPath(movies));
    QCOMPARE(cat.autocat, QStringLiteral("mkv|avi"));
}

void tst_CategoryDialog::rejectLeavesTheCategoryAlone()
{
    DownloadCategory cat;
    cat.title = QStringLiteral("Movies");
    cat.incomingPath = m_dir.path();

    CategoryDialog dlg(cat, m_dir.path());
    edits(dlg).at(0)->setText(QStringLiteral("Something else"));
    edits(dlg).at(2)->setText(QStringLiteral("/nowhere/at/all"));
    dlg.reject();

    // MFC would have written both of those into catArr before finding out the
    // user meant to cancel.
    QCOMPARE(dlg.category().title, QStringLiteral("Movies"));
    QCOMPARE(dlg.category().incomingPath, m_dir.path());
}

void tst_CategoryDialog::emptyIncomingPathIsAccepted()
{
    DownloadCategory cat;
    cat.title = QStringLiteral("Movies");
    cat.incomingPath = m_dir.path();

    CategoryDialog dlg(cat, m_dir.path());
    edits(dlg).at(2)->clear();
    okButton(dlg)->click();

    QCOMPARE(dlg.result(), int(QDialog::Accepted));
    QVERIFY2(dlg.category().incomingPath.isEmpty(),
             "clearing the field is how a category goes back to the global folder");
}

void tst_CategoryDialog::missingFolderIsCreatedOnAccept()
{
    const QString fresh = m_dir.filePath(QStringLiteral("Fresh/Deeper"));
    QVERIFY(!QDir(fresh).exists());

    CategoryDialog dlg(DownloadCategory{}, m_dir.path());
    edits(dlg).at(0)->setText(QStringLiteral("Fresh"));
    edits(dlg).at(2)->setText(fresh);
    okButton(dlg)->click();

    QCOMPARE(dlg.result(), int(QDialog::Accepted));
    QVERIFY(QDir(fresh).exists());
}

void tst_CategoryDialog::invalidRegexpBlocksAccept()
{
    CategoryDialog dlg(DownloadCategory{}, m_dir.path());
    edits(dlg).at(0)->setText(QStringLiteral("Movies"));
    edits(dlg).at(3)->setText(QStringLiteral("[unterminated"));
    dlg.findChild<QCheckBox*>()->setChecked(true);

    okButton(dlg)->click();
    // Still open, as MFC's ErrorBalloon path leaves it. Accepting a pattern that
    // cannot compile would mean a category that silently never auto-assigns.
    QVERIFY(dlg.result() != int(QDialog::Accepted));
}

void tst_CategoryDialog::untitledCategoryBlocksAccept()
{
    CategoryDialog dlg(DownloadCategory{}, m_dir.path());
    edits(dlg).at(2)->setText(m_dir.path());
    okButton(dlg)->click();
    QVERIFY(dlg.result() != int(QDialog::Accepted));
}

// MFC CatDialog.cpp:95-96, 186-198: the field stands for view filter 18. It was a
// stored string that no filter looked at unless the tab's menu was used as well.
void tst_CategoryDialog::regexpIsTheViewFilter()
{
    DownloadCategory cat;
    cat.title = QStringLiteral("Movies");
    cat.regexp = QStringLiteral("old.*");
    cat.filter = 4;   // some other filter: the stored expression is not in use

    {
        CategoryDialog dlg(cat, m_dir.path());
        QCOMPARE(edits(dlg).at(4)->text(), QString());
        okButton(dlg)->click();
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QCOMPARE(dlg.category().filter, 4);            // an unrelated edit leaves it alone
    }
    {
        CategoryDialog dlg(cat, m_dir.path());
        edits(dlg).at(4)->setText(QStringLiteral(".*\\.mkv"));
        okButton(dlg)->click();
        QCOMPARE(dlg.category().filter, 18);
        QCOMPARE(dlg.category().regexp, QStringLiteral(".*\\.mkv"));

        // shown again while it is the filter in use, and clearing it turns it off
        CategoryDialog again(dlg.category(), m_dir.path());
        QCOMPARE(edits(again).at(4)->text(), QStringLiteral(".*\\.mkv"));
        edits(again).at(4)->clear();
        okButton(again)->click();
        QCOMPARE(again.category().filter, 0);
    }
}

// MFC CatDialog.cpp:205-209: "Default" on the colour button.
void tst_CategoryDialog::colourCanGoBackToDefault()
{
    DownloadCategory cat;
    cat.title = QStringLiteral("Movies");
    cat.color = 0x00FF0000;
    CategoryDialog dlg(cat, m_dir.path());

    auto* reset = dlg.findChild<QAction*>(QStringLiteral("colorDefault"));
    QVERIFY(reset);
    reset->trigger();
    okButton(dlg)->click();
    QCOMPARE(dlg.category().color, kCategoryColorAuto);
}

// MFC CAddFriend::OnAddBtn (AddFriend.cpp:112-139): "ip:port" in the IP field, else
// the port field. An invalid address used to be sent to the daemon as it was.
void tst_CategoryDialog::addFriend_takesThePortFromTheAddressField()
{
    QString address;
    int port = 0;
    QVERIFY(AddFriendDialog::parseEndpoint(QStringLiteral("203.0.113.9:4662"), QString(), address, port));
    QCOMPARE(address, QStringLiteral("203.0.113.9"));
    QCOMPARE(port, 4662);

    // the port in the address field wins
    QVERIFY(AddFriendDialog::parseEndpoint(QStringLiteral(" 203.0.113.9:5000 "), QStringLiteral("4662"), address, port));
    QCOMPARE(port, 5000);

    QVERIFY(AddFriendDialog::parseEndpoint(QStringLiteral("203.0.113.9"), QStringLiteral("4662"), address, port));
    QCOMPARE(port, 4662);

    QVERIFY(AddFriendDialog::parseEndpoint(QStringLiteral("[2001:db8::7]:4672"), QString(), address, port));
    QCOMPARE(address, QStringLiteral("2001:db8::7"));
    QCOMPARE(port, 4672);
    QVERIFY(AddFriendDialog::parseEndpoint(QStringLiteral("2001:db8::7"), QStringLiteral("4662"), address, port));
    QCOMPARE(port, 4662);

    QVERIFY(!AddFriendDialog::parseEndpoint(QStringLiteral("203.0.113"), QStringLiteral("4662"), address, port));
    QVERIFY(!AddFriendDialog::parseEndpoint(QStringLiteral("300.1.1.1"), QStringLiteral("4662"), address, port));
    QVERIFY(!AddFriendDialog::parseEndpoint(QStringLiteral("not an address"), QStringLiteral("4662"), address, port));
    QVERIFY(!AddFriendDialog::parseEndpoint(QStringLiteral("203.0.113.9"), QString(), address, port));
    QVERIFY(!AddFriendDialog::parseEndpoint(QStringLiteral("203.0.113.9:70000"), QString(), address, port));
    QVERIFY(!AddFriendDialog::parseEndpoint(QStringLiteral("203.0.113.9"), QStringLiteral("0"), address, port));
}

// The dialog closed on Add whatever happened next, and the daemon answered "true" to
// a duplicate. Now the one who adds reports back.
void tst_CategoryDialog::addFriend_staysOpenUntilTheFriendIsAdded()
{
    AddFriendDialog dlg;
    const QList<QLineEdit*> fields = dlg.findChildren<QLineEdit*>();
    fields.at(0)->setText(QStringLiteral("203.0.113.9:4662"));

    std::function<void(bool)> answer;
    QString sentAddress;
    int sentPort = 0;
    dlg.setSubmitter([&](const AddFriendDialog& d, std::function<void(bool)> done) {
        sentAddress = d.ipAddress();
        sentPort = d.port();
        answer = std::move(done);
    });

    QPushButton* add = nullptr;
    for (QPushButton* b : dlg.findChildren<QPushButton*>())
        if (b->text() == QStringLiteral("Add"))
            add = b;
    QVERIFY(add);
    add->click();
    QCOMPARE(sentAddress, QStringLiteral("203.0.113.9"));
    QCOMPARE(sentPort, 4662);
    QVERIFY(answer);
    QVERIFY(dlg.result() != int(QDialog::Accepted));   // waiting for the verdict
    QVERIFY(!add->isEnabled());

    answer(true);
    QCOMPARE(dlg.result(), int(QDialog::Accepted));
}

// MFC AddFriend.cpp:87, 96, 105
void tst_CategoryDialog::addFriend_detailsSheetIsWordedAsMfc()
{
    AddFriendDialog dlg;
    QCOMPARE(dlg.findChildren<QLineEdit*>().at(2)->maxLength(), 50);   // the name

    dlg.showFriend(QStringLiteral("alice"), QStringLiteral("00112233445566778899AABBCCDDEEFF"),
                   QStringLiteral("203.0.113.9"), 4662, QString(), 0);
    QCOMPARE(dlg.windowTitle(), QStringLiteral("Details"));
    QStringList labels;
    for (const QLabel* label : dlg.findChildren<QLabel*>())
        labels << label->text();
    QVERIFY2(labels.contains(QStringLiteral("User ID:")), qPrintable(labels.join(u'|')));
    QVERIFY(!labels.contains(QStringLiteral("IP Address:")));
}

// MFC NetworkInfoDlg.cpp:182-213, 228-229
void tst_CategoryDialog::networkInfo_listsServerFeaturesAndLanMode()
{
    const QCborMap features{
        {QStringLiteral("tcpCompression"), true}, {QStringLiteral("shortTags"), true},
        {QStringLiteral("unicode"), true}, {QStringLiteral("intTypeTags"), false},
        {QStringLiteral("udpSources"), true}, {QStringLiteral("udpSources2"), false},
        {QStringLiteral("udpFiles"), true}, {QStringLiteral("largeFiles"), true},
        {QStringLiteral("obfuscationUdp"), true}, {QStringLiteral("obfuscationTcp"), false}};
    const QCborMap info{
        {QStringLiteral("client"), QCborMap{{QStringLiteral("nick"), QStringLiteral("me")}}},
        {QStringLiteral("ed2k"),
         QCborMap{{QStringLiteral("connected"), true},
                  {QStringLiteral("server"),
                   QCborMap{{QStringLiteral("name"), QStringLiteral("srv")},
                            {QStringLiteral("softFiles"), 1000}, {QStringLiteral("hardFiles"), 5000},
                            {QStringLiteral("ping"), 42}, {QStringLiteral("features"), features}}}}},
        {QStringLiteral("kad"),
         QCborMap{{QStringLiteral("running"), true}, {QStringLiteral("connected"), true},
                  {QStringLiteral("lanMode"), true}}},
    };

    const auto textOf = [](const QString& html) {
        QTextDocument doc;
        doc.setHtml(html);
        return doc.toPlainText();
    };

    const QString extended = textOf(NetworkInfoDialog::infoHtml(info, /*extended*/ true));
    QVERIFY2(extended.contains(QStringLiteral("TCP compression:\tYes"))
                 || extended.contains(QRegularExpression(QStringLiteral("TCP compression:\\s+Yes"))),
             qPrintable(extended));
    QVERIFY(extended.contains(QRegularExpression(QStringLiteral("Integer type tags:\\s+No"))));
    QVERIFY(extended.contains(QRegularExpression(
        QStringLiteral("Extended UDP protocol for source requests #2:\\s+No"))));
    QVERIFY(extended.contains(QRegularExpression(QStringLiteral("Protocol Obfuscation \\(UDP\\):\\s+Yes"))));
    QVERIFY(extended.contains(QRegularExpression(QStringLiteral("Protocol Obfuscation \\(TCP\\):\\s+No"))));
    QVERIFY(extended.contains(QStringLiteral("Open (LAN Mode)")));
    QVERIFY(extended.contains(QRegularExpression(QStringLiteral("Ping:\\s+42 ms"))));
    QVERIFY(extended.contains(QStringLiteral("eD2K Server Features")));

    // the feature list is an advanced-controls matter; the limits line is not
    const QString plain = textOf(NetworkInfoDialog::infoHtml(info, /*extended*/ false));
    QVERIFY(!plain.contains(QStringLiteral("TCP compression")));
    QVERIFY(plain.contains(QStringLiteral("Soft/Hard File Limits:")));
}

// MFC CCollectionViewDialog::DownloadSelected (CollectionViewDialog.cpp:156-169). The
// checkbox was never read and the downloads went uncategorised.
void tst_CategoryDialog::collection_findsOrAddsItsCategory()
{
    const auto cat = [](const char* title, int filter = 0) {
        return QCborMap{{QStringLiteral("title"), QString::fromLatin1(title)},
                        {QStringLiteral("filter"), filter}};
    };
    const QCborArray categories{cat(""), cat("Films", 5), cat("My Collection"), cat("MY COLLECTION")};

    QCOMPARE(CollectionCategory::indexFor(categories, QStringLiteral("my collection")), 3);   // last match
    QCOMPARE(CollectionCategory::indexFor(categories, QStringLiteral("films")), 1);
    QCOMPARE(CollectionCategory::indexFor(categories, QStringLiteral("Music")), 0);
    QCOMPARE(CollectionCategory::indexFor(categories, QString()), 0);                          // not "All"

    const QCborArray extended = CollectionCategory::withNewCategory(categories, QStringLiteral("Music"));
    QCOMPARE(extended.size(), 5);
    // every old entry says where it was, and keeps what it had
    for (int i = 0; i < 4; ++i)
        QCOMPARE(extended.at(i).toMap().value(QStringLiteral("oldIndex")).toInteger(-1), i);
    QCOMPARE(extended.at(1).toMap().value(QStringLiteral("filter")).toInteger(), 5);
    // the new one is new: no oldIndex, so nothing is moved into it
    QCOMPARE(extended.at(4).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Music"));
    QVERIFY(!extended.at(4).toMap().contains(QStringLiteral("oldIndex")));
}

QTEST_MAIN(tst_CategoryDialog)
#include "tst_CategoryDialog.moc"
