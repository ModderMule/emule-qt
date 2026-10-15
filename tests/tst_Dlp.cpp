/// @file tst_Dlp.cpp
/// @brief specialdlp anti-leecher: DlpLoader against the real library, and the
///        UpDownClient hooks (eMule Xtreme DLP port) fed with hello / chat packets.

#include "app/AppContext.h"
#include "client/DlpLoader.h"
#include "client/UpDownClient.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QTest>

using namespace eMule;

namespace {

/// Hello from an eMule v0.50a with the given name and mod string, plus extra tags.
QByteArray buildHello(const QString& name, const QString& mod, const std::vector<Tag>& extra = {})
{
    std::vector<Tag> tags;
    tags.emplace_back(CT_NAME, name);
    tags.emplace_back(CT_VERSION, static_cast<uint32>(EDONKEYVERSION));
    // compat 0, v0.50a
    tags.emplace_back(CT_EMULE_VERSION, (50u << 10));
    if (!mod.isEmpty())
        tags.emplace_back(CT_MOD_VERSION, mod);
    for (const auto& t : extra)
        tags.push_back(t);

    SafeMemFile data;
    data.writeUInt8(16);
    uint8 hash[16];
    for (int i = 0; i < 16; ++i)
        hash[i] = static_cast<uint8>(0x31 + i * 7);
    data.writeHash16(hash);
    data.writeUInt32(0);
    data.writeUInt16(4662);
    data.writeUInt32(static_cast<uint32>(tags.size()));
    for (const auto& t : tags)
        t.writeTagToFile(data);
    data.writeUInt32(0);
    data.writeUInt16(0);
    return data.buffer();
}

void feedHello(UpDownClient& client, const QByteArray& buf)
{
    client.processHelloPacket(reinterpret_cast<const uint8*>(buf.constData()), static_cast<uint32>(buf.size()));
}

void feedChat(UpDownClient& client, const QString& message)
{
    const QByteArray utf8 = message.toUtf8();
    SafeMemFile data(reinterpret_cast<const uint8*>(utf8.constData()), static_cast<uint32>(utf8.size()));
    client.processChatMessage(data, static_cast<uint32>(utf8.size()));
}

} // namespace

class tst_Dlp : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void loader_loadsTheLibrary();
    void loader_missingLibraryChecksNothing();
    void loader_variantFollowsPreference();
    void hello_hardModStringBans();
    void hello_softModStringReducesScore();
    void hello_softModStringBansWhenAsked();
    void hello_suspectTagBans();
    void hello_md4StringTagIsSoft();
    void hello_cleanClientStaysClean();
    void hello_ownModStringIsClean();
    void hello_switchedOffChecksNothing();
    void hello_goodModLiftsSoftVerdict();
    void chat_firstSpamMessageBans();

private:
    DlpLoader m_dlp;
    bool m_prevOn = true;
    int m_prevLevel = 0;
};

void tst_Dlp::initTestCase()
{
    // CMake passes an absolute path, the VS project one relative to the test exe
    QString path = QStringLiteral(SPECIALDLP_LIB_PATH);
    if (QDir::isRelativePath(path))
        path = QDir(QCoreApplication::applicationDirPath()).filePath(path);
    QVERIFY2(m_dlp.load(path), qPrintable(path));
    theApp.dlp = &m_dlp;
    m_prevOn = thePrefs.antiLeecher();
    m_prevLevel = thePrefs.antiLeecherLevel();
}

void tst_Dlp::cleanupTestCase()
{
    theApp.dlp = nullptr;
    thePrefs.setAntiLeecher(m_prevOn);
    thePrefs.setAntiLeecherLevel(m_prevLevel);
}

void tst_Dlp::init()
{
    thePrefs.setAntiLeecher(true);
    thePrefs.setAntiLeecherLevel(0);
}

void tst_Dlp::loader_loadsTheLibrary()
{
    QVERIFY(m_dlp.isLoaded());
    QVERIFY(m_dlp.rulesVersion() >= 44006u);
    QVERIFY(!m_dlp.libVersion().isEmpty());
    QCOMPARE(m_dlp.checkHelloTag(0x15), std::optional<QString>(QStringLiteral("[DodgeBoards & DarkMule eVorteX]")));
    QCOMPARE(m_dlp.checkModstringHard(QStringLiteral("TM0910 x"), QStringLiteral("eMule v0.50a")),
             std::optional<QString>(QStringLiteral("Bad MODSTRING")));
}

void tst_Dlp::loader_missingLibraryChecksNothing()
{
    DlpLoader none;
    QVERIFY(!none.load(QStringLiteral("/nonexistent/libspecialdlp-missing")));
    QVERIFY(!none.isLoaded());
    QCOMPARE(none.rulesVersion(), 0u);
    QVERIFY(!none.checkHelloTag(0x15));
    QVERIFY(!none.checkUsernameHard(QStringLiteral("Muse")));
}

void tst_Dlp::loader_variantFollowsPreference()
{
    const QString name = QStringLiteral("nick [VeryCD]");
    QVERIFY(!m_dlp.checkUsernameSoft(name));
    thePrefs.setAntiLeecherLevel(1);     // strict keeps the lite rules
    QVERIFY(!m_dlp.checkUsernameSoft(name));
    thePrefs.setAntiLeecherLevel(2);     // strict + Chinese = verycd-tag
    QCOMPARE(m_dlp.checkUsernameSoft(name), std::optional<QString>(QStringLiteral("<SDC> verycd-tag")));
    thePrefs.setAntiLeecherLevel(0);
    QVERIFY(!m_dlp.checkUsernameSoft(name));
}

void tst_Dlp::hello_hardModStringBans()
{
    UpDownClient client;
    feedHello(client, buildHello(QStringLiteral("nick"), QStringLiteral("TM0910 x")));
    QCOMPARE(client.clientSoftwareStr(), QStringLiteral("eMule v0.50a"));
    QCOMPARE(client.leecherCategory(), LeecherCategory::Hard);
    QCOMPARE(client.leecherReason(), QStringLiteral("Bad MODSTRING"));
    QVERIFY(client.isBanned());
}

void tst_Dlp::hello_softModStringReducesScore()
{
    UpDownClient client;
    feedHello(client, buildHello(QStringLiteral("nick"), QStringLiteral("Community 1.0")));
    QCOMPARE(client.leecherCategory(), LeecherCategory::ModSoft);
    QVERIFY(!client.isBanned());
}

void tst_Dlp::hello_softModStringBansWhenAsked()
{
    thePrefs.setAntiLeecherLevel(1);     // strict bans soft hits
    UpDownClient client;
    feedHello(client, buildHello(QStringLiteral("nick"), QStringLiteral("Community 1.0")));
    QCOMPARE(client.leecherCategory(), LeecherCategory::ModSoft);
    QVERIFY(client.isBanned());
}

void tst_Dlp::hello_suspectTagBans()
{
    UpDownClient client;
    feedHello(client, buildHello(QStringLiteral("nick"), QStringLiteral("Xtreme 8.1"),
                                 {Tag(static_cast<uint8>(0x15), static_cast<uint32>(1))}));
    QCOMPARE(client.leecherCategory(), LeecherCategory::Snafu);
    QCOMPARE(client.leecherReason(), QStringLiteral("Suspect Hello-Tag: [DodgeBoards & DarkMule eVorteX]"));
    QVERIFY(client.isBanned());
}

void tst_Dlp::hello_md4StringTagIsSoft()
{
    UpDownClient client;
    feedHello(client, buildHello(QStringLiteral("nick"), QStringLiteral("Xtreme 8.1"),
                                 {Tag(static_cast<uint8>(0x42), QString(32, QLatin1Char('a')))}));
    QCOMPARE(client.leecherCategory(), LeecherCategory::Md4Tag);
    QVERIFY(!client.isBanned());
}

void tst_Dlp::hello_cleanClientStaysClean()
{
    // also covers the thief rule: mod "Xtreme x.y" needs «Xtreme x.y» in the nick
    UpDownClient client;
    feedHello(client, buildHello(QStringLiteral("nick «Xtreme 8.1»"), QStringLiteral("Xtreme 8.1")));
    QCOMPARE(client.leecherCategory(), LeecherCategory::None);
    QVERIFY(!client.isBanned());
}

void tst_Dlp::hello_ownModStringIsClean()
{
    // What our own hello says (UpDownClient::sendHelloTypePacket), with a v0.70b peer version
    UpDownClient client;
    QByteArray hello = buildHello(QStringLiteral("http://emule-project.net"),
                                  QStringLiteral("eMule Qt " EMULE_VERSION_STRING));
    feedHello(client, hello);
    QCOMPARE(client.leecherCategory(), LeecherCategory::None);
}

void tst_Dlp::hello_switchedOffChecksNothing()
{
    thePrefs.setAntiLeecher(false);
    UpDownClient client;
    feedHello(client, buildHello(QStringLiteral("Muse"), QStringLiteral("TM0910 x"),
                                 {Tag(static_cast<uint8>(0x15), static_cast<uint32>(1))}));
    QCOMPARE(client.leecherCategory(), LeecherCategory::None);
    QVERIFY(!client.isBanned());
}

void tst_Dlp::hello_goodModLiftsSoftVerdict()
{
    UpDownClient client;
    feedHello(client, buildHello(QStringLiteral("nick «Xtreme 8.1»"), QStringLiteral("Community 1.0")));
    QCOMPARE(client.leecherCategory(), LeecherCategory::ModSoft);
    // a real Xtreme carries «Xtreme x.y» in its nick, else it is a mod string thief
    feedHello(client, buildHello(QStringLiteral("nick «Xtreme 8.1»"), QStringLiteral("Xtreme 8.1")));
    QCOMPARE(client.leecherCategory(), LeecherCategory::None);
}

void tst_Dlp::chat_firstSpamMessageBans()
{
    UpDownClient client;
    feedHello(client, buildHello(QStringLiteral("nick «Xtreme 8.1»"), QStringLiteral("Xtreme 8.1")));
    QCOMPARE(client.leecherCategory(), LeecherCategory::None);

    feedChat(client, QStringLiteral("Join the L33cher now"));
    QCOMPARE(client.leecherCategory(), LeecherCategory::Spammer);
    QCOMPARE(client.leecherReason(), QStringLiteral("Spam-Message"));
    QVERIFY(client.isBanned());
    QCOMPARE(client.messagesReceived(), uint8{0});   // never shown
}

QTEST_GUILESS_MAIN(tst_Dlp)
#include "tst_Dlp.moc"
