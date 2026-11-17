/// @file tst_CoreConnectScript.cpp
/// @brief Core connect script: host check, quoting, and the bash script run for real
/// against a stub "eMule Qt" that records its args and EMULEQT_CORE_TOKEN.
/// bash runs as /bin/bash: 3.2 on macOS, the oldest one the script must survive.

#include "TestHelpers.h"

#include "dialogs/CoreScriptDialog.h"
#include "utils/CoreConnectScript.h"

#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using eMule::CoreConnectScript;
using eMule::CoreScriptDialog;

class tst_CoreConnectScript : public QObject {
    Q_OBJECT

private slots:
    void hostValidation_data();
    void hostValidation();
    void localOnlyHosts();
    void listensFor();
    void targetDropdownPreselectsThisOs();
    void listenRowOnlyForLoopbackLocalCore();
    void powerShellQuotingBomAndCrlf();
    void powerShellParses();
    void bashSyntaxOnEveryShell();
    void bashRunsAppWithTokenInEnv_data();
    void bashRunsAppWithTokenInEnv();
    void bashEmptyTokenWithoutTtyFails();

private:
    /// Stub app writing "args\ntoken" to @p out
    static QString writeStubApp(const QTemporaryDir& dir, const QString& out);
};

void tst_CoreConnectScript::hostValidation_data()
{
    QTest::addColumn<QString>("host");
    QTest::addColumn<bool>("valid");
    QTest::newRow("ipv4") << "203.0.113.5" << true;
    QTest::newRow("domain") << "core.example.org" << true;
    QTest::newRow("ipv6") << "2001:db8::1" << true;
    QTest::newRow("ipv6 brackets") << "[2001:db8::1]" << true;
    QTest::newRow("scope") << "fe80::1%en0" << true;
    QTest::newRow("empty") << "" << false;
    QTest::newRow("space") << "a b" << false;
    QTest::newRow("quote") << "a'b" << false;
    QTest::newRow("dollar") << "$(id)" << false;
    QTest::newRow("leading dash") << "-x" << false;
}

void tst_CoreConnectScript::hostValidation()
{
    QFETCH(QString, host);
    QFETCH(bool, valid);
    QCOMPARE(CoreConnectScript::isValidHost(host), valid);
}

void tst_CoreConnectScript::localOnlyHosts()
{
    for (const auto* h : {"", "localhost", "127.0.0.1", "::1", "0.0.0.0", "::", "*", "[::1]"})
        QVERIFY2(CoreScriptDialog::isLocalOnlyHost(QString::fromLatin1(h)), h);
    for (const auto* h : {"203.0.113.5", "core.example.org", "2001:db8::1", "192.168.1.10"})
        QVERIFY2(!CoreScriptDialog::isLocalOnlyHost(QString::fromLatin1(h)), h);
}

void tst_CoreConnectScript::listensFor()
{
    const QHostAddress any(QHostAddress::Any), v4(QHostAddress::AnyIPv4), lo(QHostAddress::LocalHost);
    const QHostAddress lan(QStringLiteral("192.168.1.10"));
    const auto pub = QStringLiteral("203.0.113.5");
    QVERIFY(CoreScriptDialog::listensFor({any}, QStringLiteral("*"), pub));
    QVERIFY(CoreScriptDialog::listensFor({any}, QStringLiteral("*"), QStringLiteral("2001:db8::1")));
    QVERIFY(CoreScriptDialog::listensFor({any}, QStringLiteral("*"), QStringLiteral("core.example.org")));
    QVERIFY(CoreScriptDialog::listensFor({v4}, QStringLiteral("0.0.0.0"), pub));
    QVERIFY(!CoreScriptDialog::listensFor({v4}, QStringLiteral("0.0.0.0"), QStringLiteral("2001:db8::1")));
    QVERIFY(!CoreScriptDialog::listensFor({lo}, QStringLiteral("127.0.0.1"), pub));
    QVERIFY(CoreScriptDialog::listensFor({lan}, QStringLiteral("192.168.1.10"), QStringLiteral("192.168.1.10")));
    QVERIFY(!CoreScriptDialog::listensFor({lan}, QStringLiteral("192.168.1.10"), pub));
    // a name only by the one it listens by
    QVERIFY(CoreScriptDialog::listensFor({lo, lan}, QStringLiteral("Box.lan"), QStringLiteral("box.lan")));
    QVERIFY(!CoreScriptDialog::listensFor({lo, lan}, QStringLiteral("box.lan"), QStringLiteral("other.lan")));
    QVERIFY(!CoreScriptDialog::listensFor({lo}, QStringLiteral("box.lan"), QStringLiteral("box.lan")));
}

void tst_CoreConnectScript::targetDropdownPreselectsThisOs()
{
    CoreScriptDialog dlg(false, QStringLiteral("core.example.org"), 4712, QStringLiteral("t"),
                         QString(), nullptr);
    auto* combo = dlg.findChild<QComboBox*>();
    QVERIFY(combo);
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->currentData().toInt(), static_cast<int>(CoreConnectScript::nativeTarget()));
    QCOMPARE(CoreConnectScript::suggestedFileName(QStringLiteral("::1"), CoreConnectScript::Target::Windows),
             QStringLiteral("eMule Qt core --1.ps1"));
    QVERIFY(CoreConnectScript::fileFilter(CoreConnectScript::Target::Linux).contains(QStringLiteral("*.sh")));
    QCOMPARE(CoreConnectScript::kindOf(CoreConnectScript::Target::MacOS), CoreConnectScript::Kind::Bash);
}

void tst_CoreConnectScript::listenRowOnlyForLoopbackLocalCore()
{
    const auto listenButton = [](const CoreScriptDialog& d) -> QPushButton* {
        for (auto* b : d.findChildren<QPushButton*>())
            if (b->text().startsWith(QStringLiteral("Listen")))
                return b;
        return nullptr;
    };
    {
        CoreScriptDialog dlg(true, QString(), 4712, QStringLiteral("t"), QStringLiteral("127.0.0.1"), nullptr);
        dlg.show();
        QPushButton* btn = listenButton(dlg);
        QVERIFY(btn && btn->isVisible());
        QVERIFY(!btn->isEnabled());   // no address yet (and no core here)
    }
    for (const auto* listen : {"*", "0.0.0.0", "192.168.1.10"}) {
        CoreScriptDialog dlg(true, QString(), 4712, QStringLiteral("t"), QString::fromLatin1(listen), nullptr);
        dlg.show();
        QPushButton* btn = listenButton(dlg);
        QVERIFY2(!btn || !btn->isVisible(), listen);
    }
    CoreScriptDialog remote(false, QStringLiteral("core.example.org"), 4712, QString(),
                            QStringLiteral("127.0.0.1"), nullptr);
    remote.show();
    QPushButton* btn = listenButton(remote);
    QVERIFY(!btn || !btn->isVisible());
}

void tst_CoreConnectScript::powerShellQuotingBomAndCrlf()
{
    const QByteArray s = CoreConnectScript::generate(
        CoreConnectScript::Kind::PowerShell,
        {.host = QStringLiteral("[2001:db8::1]"), .port = 4712,
         .token = QStringLiteral("a'b\u2019c@APP@"), .appPath = QStringLiteral("C:\\Pro'g\\emuleqt.exe")});
    QVERIFY(s.startsWith("\xEF\xBB\xBF"));
    QVERIFY(!s.contains("\r\r"));
    QCOMPARE(s.count('\n'), s.count("\r\n"));
    const QString text = QString::fromUtf8(s);
    QVERIFY(text.contains(QStringLiteral("$CoreHost  = '2001:db8::1'")));
    QVERIFY(text.contains(QStringLiteral("$CorePort  = 4712")));
    // doubled quotes, and the @APP@ inside the token left alone
    QVERIFY(text.contains(QStringLiteral("$CoreToken = 'a''b\u2019\u2019c@APP@'")));
    QVERIFY(text.contains(QStringLiteral("$App       = 'C:\\Pro''g\\emuleqt.exe'")));
    QVERIFY(text.contains(QStringLiteral("[2001:db8::1]:4712")));
}

void tst_CoreConnectScript::powerShellParses()
{
    const QString pwsh = QStandardPaths::findExecutable(QStringLiteral("pwsh"));
    if (pwsh.isEmpty())
        QSKIP("pwsh not installed");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("connect.ps1"));
    for (const QString& token : {QStringLiteral("t'o\"k$en"), QString{}}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(CoreConnectScript::generate(CoreConnectScript::Kind::PowerShell,
                                            {.host = QStringLiteral("core.example.org"), .port = 4712,
                                             .token = token, .appPath = QStringLiteral("C:\\x.exe")}));
        f.close();
        QProcess p;
        p.start(pwsh, {QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
                       QStringLiteral("$e=$null; [void][System.Management.Automation.Language.Parser]"
                                      "::ParseFile('%1',[ref]$null,[ref]$e); if ($e) { $e; exit 1 }")
                           .arg(path)});
        QVERIFY(p.waitForFinished(60'000));
        QVERIFY2(p.exitCode() == 0, p.readAllStandardOutput().constData());
    }
}

void tst_CoreConnectScript::bashSyntaxOnEveryShell()
{
#ifdef Q_OS_WIN
    QSKIP("no bash on Windows");
#endif
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("connect.sh"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(CoreConnectScript::generate(CoreConnectScript::Kind::Bash,
                                        {.host = QStringLiteral("core.example.org"), .port = 4712,
                                         .token = QStringLiteral("it's \"$(x)\" `y`"),
                                         .appPath = QStringLiteral("/Apps/eMule Qt.app/x")}));
    f.close();
    int shells = 0;
    for (const auto* sh : {"/bin/bash", "/bin/sh", "/bin/dash", "/bin/zsh"}) {
        if (!QFile::exists(QString::fromLatin1(sh)))
            continue;
        ++shells;
        QProcess p;
        p.start(QString::fromLatin1(sh), {QStringLiteral("-n"), path});
        QVERIFY(p.waitForFinished(10'000));
        QVERIFY2(p.exitCode() == 0, qPrintable(QString::fromLatin1(sh) + u' '
                                               + QString::fromUtf8(p.readAllStandardError())));
    }
    QVERIFY(shells > 0);
    QFile written(path);
    QVERIFY(written.open(QIODevice::ReadOnly));
    QVERIFY(!written.readAll().contains('\r'));
}

void tst_CoreConnectScript::bashRunsAppWithTokenInEnv_data()
{
    QTest::addColumn<QString>("host");
    QTest::addColumn<QString>("token");
    QTest::newRow("plain") << "core.example.org" << "0123456789abcdef";
    QTest::newRow("hostile token") << "203.0.113.5" << "it's \"$(touch pwned)\" `id` \\ $HOME";
    QTest::newRow("ipv6") << "[2001:db8::1]" << "x";
}

void tst_CoreConnectScript::bashRunsAppWithTokenInEnv()
{
#ifdef Q_OS_WIN
    QSKIP("no bash on Windows");
#endif
    QFETCH(QString, host);
    QFETCH(QString, token);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.filePath(QStringLiteral("out.txt"));
    const QString app = writeStubApp(dir, out);
    const QString script = dir.filePath(QStringLiteral("connect.command"));
    QFile f(script);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(CoreConnectScript::generate(CoreConnectScript::Kind::Bash,
                                        {.host = host, .port = 4799, .token = token, .appPath = app}));
    f.close();

    QProcess p;
    p.setWorkingDirectory(dir.path());
    p.setStandardInputFile(QProcess::nullDevice());
    p.start(QStringLiteral("/bin/bash"), {script});
    QVERIFY(p.waitForFinished(10'000));
    QCOMPARE(p.exitCode(), 0);
    // App runs detached: wait for its file
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(out) && QFileInfo(out).size() > 0, 5'000);
    QTest::qWait(100);
    QFile r(out);
    QVERIFY(r.open(QIODevice::ReadOnly));
    const QStringList lines = QString::fromUtf8(r.readAll()).split(u'\n');
    QVERIFY(lines.size() >= 2);
    QCOMPARE(lines.at(0), QStringLiteral("--core-host|%1|--core-port|4799|")
                              .arg(CoreConnectScript::normalizedHost(host)));
    QCOMPARE(lines.at(1), token);
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("pwned"))));
}

void tst_CoreConnectScript::bashEmptyTokenWithoutTtyFails()
{
#ifndef Q_OS_LINUX
    QSKIP("macOS would pop an osascript dialog");
#endif
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.filePath(QStringLiteral("out.txt"));
    const QString script = dir.filePath(QStringLiteral("connect.sh"));
    QFile f(script);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(CoreConnectScript::generate(CoreConnectScript::Kind::Bash,
                                        {.host = QStringLiteral("core.example.org"), .port = 4712,
                                         .token = {}, .appPath = writeStubApp(dir, out)}));
    f.close();
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));   // no zenity/kdialog
    env.remove(QStringLiteral("DISPLAY"));
    env.remove(QStringLiteral("WAYLAND_DISPLAY"));
    p.setProcessEnvironment(env);
    p.setStandardInputFile(QProcess::nullDevice());
    p.start(QStringLiteral("/bin/bash"), {script});
    QVERIFY(p.waitForFinished(10'000));
    QCOMPARE(p.exitCode(), 1);
    QVERIFY(p.readAllStandardError().contains("No token given."));
    QVERIFY(!QFile::exists(out));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

QString tst_CoreConnectScript::writeStubApp(const QTemporaryDir& dir, const QString& out)
{
    const QString path = dir.filePath(QStringLiteral("eMule Qt stub"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("#!/bin/sh\n"
            "for a in \"$@\"; do printf '%s|' \"$a\"; done > \"$0.tmp\"\n"
            "printf '\\n%s' \"$EMULEQT_CORE_TOKEN\" >> \"$0.tmp\"\n"
            "mv \"$0.tmp\" '" + out.toUtf8() + "'\n");
    f.close();
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

QTEST_MAIN(tst_CoreConnectScript)
#include "tst_CoreConnectScript.moc"
