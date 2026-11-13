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
    void addFriend_anEd3kLinkIsConfirmedByTheUser();

    // --- CollectionViewDialog's category rule --------------------------------
    void collection_findsOrAddsItsCategory();

    // --- NetworkInfoDialog ---------------------------------------------------
    void networkInfo_listsServerFeaturesAndLanMode();
    void networkInfo_listsServedBuddies();
    void networkInfo_showsEd3kNode();
    void networkInfo_showsTorrentIpsAndI2p();
    void networkInfo_showsSharedCounts();

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

// friend.md §2, §3 (batch 15): a link opened from outside fills the dialog, Add sends it; an
// open link (s) is hidden and carries no request text; one with r may ask
void tst_CategoryDialog::addFriend_anEd3kLinkIsConfirmedByTheUser()
{
    for (const bool open : {true, false}) {
        AddFriendDialog dlg;
        QString sentLink, sentText;
        std::function<void(int)> answer;
        dlg.setEd3kSubmitter([&](const QString& link, const QString& text, std::function<void(int)> done) {
            sentLink = link;
            sentText = text;
            answer = std::move(done);
        });
        dlg.setEd3kAvailable(true);
        const QString link = open ? QStringLiteral("ed3k:friend?key=abc&s=secretsecret&dn=Bob")
                                  : QStringLiteral("ed3k:friend?key=abc&r=mfrggzdf&dn=Bob");
        dlg.setEd3kLink(link);
        QCOMPARE(AddFriendDialog::isOpenFriendLink(link), open);
        QCOMPARE(AddFriendDialog::hasFriendRequest(link), !open);
        QLineEdit* linkEdit = nullptr;
        QLineEdit* textEdit = nullptr;
        for (QLineEdit* e : dlg.findChildren<QLineEdit*>()) {
            if (e->text() == link)
                linkEdit = e;
            else if (e->maxLength() == 96)
                textEdit = e;
        }
        QVERIFY(linkEdit && textEdit);
        QVERIFY(linkEdit->isReadOnly());
        QCOMPARE(linkEdit->echoMode(), open ? QLineEdit::Password : QLineEdit::Normal);   // §2: a credential
        QCOMPARE(textEdit->isEnabled(), !open);
        textEdit->setText(QStringLiteral("hi, it's me"));
        QVERIFY(sentLink.isEmpty());   // nothing before the user's Add
        QPushButton* add = nullptr;
        for (QPushButton* b : dlg.findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Add"))
                add = b;
        QVERIFY(add);
        add->click();
        QCOMPARE(sentLink, link);
        QCOMPARE(sentText, open ? QString() : QStringLiteral("hi, it's me"));
        QVERIFY(answer);
        answer(AddFriendDialog::kEd3kAdded);
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
    }
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
        {QStringLiteral("obfuscationUdp"), true}, {QStringLiteral("obfuscationTcp"), false},
        {QStringLiteral("ipv6Tcp"), true}, {QStringLiteral("lowIdHolePunch"), true},
        {QStringLiteral("natWake"), false}};
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
    // not MFC: IPv6 and NAT callback extensions; a missing key reads No
    QVERIFY(extended.contains(QRegularExpression(QStringLiteral("IPv6 \\(TCP\\):\\s+Yes"))));
    QVERIFY(extended.contains(QRegularExpression(QStringLiteral("IPv6 \\(UDP\\):\\s+No"))));
    QVERIFY(extended.contains(QRegularExpression(QStringLiteral("LowID hole punching:\\s+Yes"))));
    QVERIFY(extended.contains(QRegularExpression(QStringLiteral("NAT wake-up:\\s+No"))));
    QVERIFY(extended.contains(QRegularExpression(QStringLiteral("Callback for clients on other servers:\\s+No"))));
    QVERIFY(extended.contains(QStringLiteral("Open (LAN Mode)")));
    QVERIFY(extended.contains(QRegularExpression(QStringLiteral("Ping:\\s+42 ms"))));
    QVERIFY(extended.contains(QStringLiteral("eD2K Server Features")));

    // the feature list is an advanced-controls matter; the limits line is not
    const QString plain = textOf(NetworkInfoDialog::infoHtml(info, /*extended*/ false));
    QVERIFY(!plain.contains(QStringLiteral("TCP compression")));
    QVERIFY(plain.contains(QStringLiteral("Soft/Hard File Limits:")));
}

// Not MFC: an open node lists the firewalled nodes it relays for.
void tst_CategoryDialog::networkInfo_listsServedBuddies()
{
    const auto textOf = [](const QString& html) {
        QTextDocument doc;
        doc.setHtml(html);
        return doc.toPlainText();
    };
    const auto infoWith = [](bool udpFirewalled, const QCborArray& served) {
        return QCborMap{
            {QStringLiteral("kad"),
             QCborMap{{QStringLiteral("running"), true}, {QStringLiteral("connected"), true},
                      {QStringLiteral("udpFirewalled"), udpFirewalled},
                      {QStringLiteral("buddiesServed"), served.size()},
                      {QStringLiteral("buddiesServedMax"), 8},
                      {QStringLiteral("servedBuddies"), served}}}};
    };
    const QCborArray two{
        QCborMap{{QStringLiteral("address"), QStringLiteral("192.0.2.7")}, {QStringLiteral("port"), 4662},
                 {QStringLiteral("name"), QStringLiteral("al<i>ce")},
                 {QStringLiteral("software"), QStringLiteral("eMule v0.50a")},
                 {QStringLiteral("connectedSecs"), 7260}},
        QCborMap{{QStringLiteral("address"), QStringLiteral("2001:db8::9")}, {QStringLiteral("port"), 5000},
                 {QStringLiteral("connectedSecs"), 30}}};

    const QString open = textOf(NetworkInfoDialog::infoHtml(infoWith(false, two), false));
    QVERIFY2(open.contains(QRegularExpression(QStringLiteral("Buddies served:\\s+2 / 8"))), qPrintable(open));
    // the name is shown literally, not parsed as markup
    QVERIFY2(open.contains(QStringLiteral("192.0.2.7:4662, al<i>ce (eMule v0.50a), ")), qPrintable(open));
    QVERIFY(open.contains(QStringLiteral("[2001:db8::9]:5000, ")));
    QVERIFY(open.indexOf(QStringLiteral("192.0.2.7")) < open.indexOf(QStringLiteral("2001:db8::9")));

    const QString none = textOf(NetworkInfoDialog::infoHtml(infoWith(false, {}), false));
    QVERIFY(none.contains(QRegularExpression(QStringLiteral("Buddies served:\\s+0 / 8"))));

    // firewalled: our own buddy row, no served list
    const QString firewalled = textOf(NetworkInfoDialog::infoHtml(infoWith(true, two), false));
    QVERIFY(firewalled.contains(QStringLiteral("Buddy:")));
    QVERIFY(!firewalled.contains(QStringLiteral("Buddies served")));
    QVERIFY(!firewalled.contains(QStringLiteral("192.0.2.7")));
}

// Not MFC: the ed3k node, with its public endpoint and NAT class per family (#165)
void tst_CategoryDialog::networkInfo_showsEd3kNode()
{
    const auto textOf = [](const QString& html) {
        QTextDocument doc;
        doc.setHtml(html);
        return doc.toPlainText();
    };
    const QCborMap running{{QStringLiteral("available"), true}, {QStringLiteral("running"), true},
                           {QStringLiteral("udpPort"), 6600}, {QStringLiteral("contacts"), 12},
                           {QStringLiteral("class4"), 2}, {QStringLiteral("class6"), 1},
                           {QStringLiteral("natChecking"), 0}, {QStringLiteral("kadOffline"), 0},
                           {QStringLiteral("router"), true}, {QStringLiteral("nodeId"), QStringLiteral("ab12")},
                           {QStringLiteral("endpoint4"), QStringLiteral("192.0.2.5:6600")},
                           {QStringLiteral("endpoint6"), QString()}};
    const QString on = textOf(NetworkInfoDialog::infoHtml({{QStringLiteral("ed3k"), running}}, false));
    QVERIFY2(on.contains(QStringLiteral("ed3k Network")), qPrintable(on));
    QVERIFY(on.contains(QRegularExpression(QStringLiteral("IPv4:\\s+192\\.0\\.2\\.5:6600 \\(Cone NAT\\)"))));
    QVERIFY(on.contains(QRegularExpression(QStringLiteral("IPv6:\\s+Open"))));
    QVERIFY(on.contains(QRegularExpression(QStringLiteral("Router:\\s+Yes"))));
    QVERIFY(on.contains(QRegularExpression(QStringLiteral("UDP Port:\\s+6600"))));
    // an open router without relays hides the row (MFC Buddy); no stats, no Indexed
    QVERIFY(!on.contains(QStringLiteral("Relays:")));
    QVERIFY(!on.contains(QStringLiteral("Indexed:")));

    // not a router: relays (the Kad buddy), connections and records stored for others
    QCborMap leaf = running;
    leaf.insert(QStringLiteral("router"), false);
    leaf.insert(QStringLiteral("relays4"),
                QCborArray{QCborMap{{QStringLiteral("address"), QStringLiteral("198.51.100.7:6600")},
                                    {QStringLiteral("node"), QStringLiteral("0123456789abcdef")}}});
    leaf.insert(QStringLiteral("relays6"),
                QCborArray{QCborMap{{QStringLiteral("address"), QStringLiteral("[2001:db8::1]:6600")},
                                    {QStringLiteral("node"), QStringLiteral("fedcba9876543210")}}});
    leaf.insert(QStringLiteral("stats"), QCborMap{{QStringLiteral("connections"), 3},
                                                  {QStringLiteral("records"), 42},
                                                  {QStringLiteral("recordBytes"), 2048}});
    const QString relayed = textOf(NetworkInfoDialog::infoHtml({{QStringLiteral("ed3k"), leaf}}, false));
    QVERIFY2(relayed.contains(QRegularExpression(QStringLiteral("Relays:\\s+2"))), qPrintable(relayed));
    QVERIFY(relayed.contains(QStringLiteral("198.51.100.7:6600 (01234567)")));
    QVERIFY(relayed.contains(QStringLiteral("[2001:db8::1]:6600 (fedcba98)")));
    QVERIFY(relayed.contains(QRegularExpression(QStringLiteral("Connections:\\s+3 \\(Obfuscated\\)"))));
    QVERIFY(relayed.contains(QRegularExpression(QStringLiteral("Indexed:\\s+42 \\("))));

    leaf.remove(QStringLiteral("relays4"));
    leaf.remove(QStringLiteral("relays6"));
    const QString noRelay = textOf(NetworkInfoDialog::infoHtml({{QStringLiteral("ed3k"), leaf}}, false));
    QVERIFY(noRelay.contains(QRegularExpression(QStringLiteral("Relays:\\s+None"))));
    // a leaf serves nobody: no Buddies served row, no index node section without a list
    QVERIFY(!noRelay.contains(QStringLiteral("Buddies served:")));
    QVERIFY(!noRelay.contains(QStringLiteral("ed3k Index Node")));

    // a router: leaves it relays for, eMule's Kad estimates, the index node held
    QCborMap router = running;
    router.insert(QStringLiteral("leavesServed"),
                  QCborArray{QCborMap{{QStringLiteral("address"), QStringLiteral("203.0.113.9:6611")},
                                      {QStringLiteral("node"), QStringLiteral("aabbccdd11223344")},
                                      {QStringLiteral("hop"), false}}});
    router.insert(QStringLiteral("users"), 1200);
    router.insert(QStringLiteral("usersExperimental"), 900);
    router.insert(QStringLiteral("files"), 129600);
    const auto link = [](int state, bool current, const QString& addr) {
        return QCborMap{{QStringLiteral("address"), addr}, {QStringLiteral("node"), QStringLiteral("99887766")},
                        {QStringLiteral("state"), state}, {QStringLiteral("current"), current},
                        {QStringLiteral("rttMs"), 42}, {QStringLiteral("agent"), QStringLiteral("eNode 1.0")},
                        {QStringLiteral("nick"), QStringLiteral("Test index")}};
    };
    router.insert(QStringLiteral("indexNodes"), QCborArray{link(0, false, QStringLiteral("192.0.2.20:4662")),
                                                           link(2, true, QStringLiteral("192.0.2.21:4662"))});
    const QString served = textOf(NetworkInfoDialog::infoHtml({{QStringLiteral("ed3k"), router}}, false));
    QVERIFY2(served.contains(QRegularExpression(QStringLiteral("Buddies served:\\s+1"))), qPrintable(served));
    QVERIFY(served.contains(QStringLiteral("203.0.113.9:6611 (aabbccdd)")));
    QVERIFY(served.contains(QRegularExpression(QStringLiteral("Users:\\s+1[.,]?200 \\(Experimental: 900\\)"))));
    QVERIFY(served.contains(QRegularExpression(QStringLiteral("Files:\\s+129[.,]?600"))));
    QVERIFY(served.contains(QStringLiteral("ed3k Index Node")));
    QVERIFY(served.contains(QRegularExpression(QStringLiteral("Name:\\s+Test index"))));
    QVERIFY(served.contains(QRegularExpression(QStringLiteral("IP:Port:\\s+192\\.0\\.2\\.21:4662"))));
    QVERIFY(served.contains(QRegularExpression(QStringLiteral("Status:\\s+Connected"))));
    QVERIFY(served.contains(QRegularExpression(QStringLiteral("Version:\\s+eNode 1\\.0"))));
    QVERIFY(served.contains(QRegularExpression(QStringLiteral("Ping:\\s+42 ms"))));
    QVERIFY(served.contains(QRegularExpression(QStringLiteral("Index nodes:\\s+2"))));
    // dialling: no version or ping yet
    router.insert(QStringLiteral("indexNodes"), QCborArray{link(1, true, QStringLiteral("192.0.2.21:4662"))});
    const QString dialling = textOf(NetworkInfoDialog::infoHtml({{QStringLiteral("ed3k"), router}}, false));
    QVERIFY(dialling.contains(QRegularExpression(QStringLiteral("Status:\\s+Connecting"))));
    QVERIFY(!dialling.contains(QStringLiteral("Ping:")));
    QVERIFY(!dialling.contains(QStringLiteral("Index nodes:")));

    const QCborMap off{{QStringLiteral("available"), true}, {QStringLiteral("running"), false},
                       {QStringLiteral("enabled"), false}};
    const QString disabled = textOf(NetworkInfoDialog::infoHtml({{QStringLiteral("ed3k"), off}}, false));
    QVERIFY(disabled.contains(QRegularExpression(QStringLiteral("ed3k Network\\s+Status:\\s+Disabled"))));

    // a core without ed3k, or from before the section: nothing
    const QString none = textOf(NetworkInfoDialog::infoHtml({}, false));
    QVERIFY(!none.contains(QStringLiteral("ed3k")));
}

// Not MFC: own shared files, per network with advanced controls, else combined
void tst_CategoryDialog::networkInfo_showsSharedCounts()
{
    const auto textOf = [](const QString& html) {
        QTextDocument doc;
        doc.setHtml(html);
        return doc.toPlainText();
    };
    const QCborMap shared{{QStringLiteral("ed2kTotal"), 120}, {QStringLiteral("ed2kHashing"), 3},
                          {QStringLiteral("ed2kPublished"), 100}, {QStringLiteral("ed3kTotal"), 117},
                          {QStringLiteral("ed3kHashing"), 5}, {QStringLiteral("ed3kPublished"), 110}};
    const QCborMap info{{QStringLiteral("shared"), shared}};

    const QString ext = textOf(NetworkInfoDialog::infoHtml(info, true));
    QVERIFY2(ext.contains(QRegularExpression(QStringLiteral("Total:\\s+120\\s+117"))), qPrintable(ext));
    QVERIFY(ext.contains(QRegularExpression(QStringLiteral("Hashing:\\s+3\\s+5"))));
    QVERIFY(ext.contains(QRegularExpression(QStringLiteral("Published:\\s+100\\s+110"))));

    const QString plain = textOf(NetworkInfoDialog::infoHtml(info, false));
    QVERIFY2(plain.contains(QRegularExpression(QStringLiteral("Total:\\s+120\\n"))), qPrintable(plain));
    QVERIFY(plain.contains(QRegularExpression(QStringLiteral("Hashing:\\s+8\\n"))));
    QVERIFY(plain.contains(QRegularExpression(QStringLiteral("Published:\\s+110\\n"))));

    // ed3k off: eD2K's own, no columns even with advanced controls
    const QCborMap ed2kOnly{{QStringLiteral("shared"), QCborMap{{QStringLiteral("ed2kTotal"), 7},
                                                               {QStringLiteral("ed2kHashing"), 0},
                                                               {QStringLiteral("ed2kPublished"), 7}}}};
    const QString solo = textOf(NetworkInfoDialog::infoHtml(ed2kOnly, true));
    QVERIFY2(solo.contains(QRegularExpression(QStringLiteral("Total:\\s+7\\n"))), qPrintable(solo));
    QVERIFY(!solo.contains(QStringLiteral("ed3k")));
}

// Not MFC: BitTorrent's external IPs (external_ip_alert) and the I2P state (#165)
void tst_CategoryDialog::networkInfo_showsTorrentIpsAndI2p()
{
    const auto textOf = [](const QString& html) {
        QTextDocument doc;
        doc.setHtml(html);
        return doc.toPlainText();
    };
    QCborMap bt{{QStringLiteral("available"), true}, {QStringLiteral("running"), true},
                {QStringLiteral("enabled"), true}, {QStringLiteral("listening"), true},
                {QStringLiteral("listenPort"), 6588}, {QStringLiteral("dhtNodes"), 300},
                {QStringLiteral("torrents"), 4},
                {QStringLiteral("externalIPv4"), QStringLiteral("198.51.100.7")},
                {QStringLiteral("externalIPv6"), QStringLiteral("2001:db8::7")},
                {QStringLiteral("i2pMode"), 1}, {QStringLiteral("i2pEnabled"), true},
                {QStringLiteral("i2pSam"), true}, {QStringLiteral("i2pSamHost"), QStringLiteral("127.0.0.1")},
                {QStringLiteral("i2pSamPort"), 7656}, {QStringLiteral("i2pError"), QString()},
                {QStringLiteral("i2pRouter"), 2}, {QStringLiteral("i2pRouterPort"), 23456},
                {QStringLiteral("i2pShare"), 50}, {QStringLiteral("i2pTunnelLength"), 3},
                {QStringLiteral("i2pTunnelQuantity"), 2}, {QStringLiteral("i2pMixed"), false},
                {QStringLiteral("i2pTorrents"), 1}, {QStringLiteral("i2pPeers"), 5}};
    const QString on = textOf(NetworkInfoDialog::infoHtml({{QStringLiteral("bt"), bt}}, false));
    QVERIFY2(on.contains(QRegularExpression(QStringLiteral("Status:\\s+Listening on port 6588"))), qPrintable(on));
    QVERIFY(on.contains(QRegularExpression(QStringLiteral("External IPv4:\\s+198\\.51\\.100\\.7"))));
    QVERIFY(on.contains(QRegularExpression(QStringLiteral("External IPv6:\\s+2001:db8::7"))));
    QVERIFY(on.contains(QStringLiteral("BitTorrent over I2P")));
    QVERIFY(on.contains(QRegularExpression(QStringLiteral("Router Status:\\s+Ready"))));
    QVERIFY(on.contains(QRegularExpression(QStringLiteral("SAM Bridge:\\s+127\\.0\\.0\\.1:7656"))));
    QVERIFY(on.contains(QRegularExpression(QStringLiteral("Tunnels:\\s+3 hops, 2 each way"))));
    QVERIFY(on.contains(QRegularExpression(QStringLiteral("I2P Torrents:\\s+1 \\(5 peers\\)"))));

    // no vote yet: IPv4 unknown, no IPv6 row; I2P off: no I2P section
    bt.insert(QStringLiteral("externalIPv4"), QString());
    bt.insert(QStringLiteral("externalIPv6"), QString());
    bt.insert(QStringLiteral("i2pMode"), 0);
    const QString plain = textOf(NetworkInfoDialog::infoHtml({{QStringLiteral("bt"), bt}}, false));
    QVERIFY(plain.contains(QRegularExpression(QStringLiteral("External IPv4:\\s+Unknown"))));
    QVERIFY(!plain.contains(QStringLiteral("External IPv6")));
    QVERIFY(!plain.contains(QStringLiteral("I2P")));
}

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
