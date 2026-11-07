/// @file tst_HosterSolvers.cpp
/// @brief JDownloader's captcha solver plugins through the hoster resolver sidecar:
/// solverAccounts (each account checked, its balance read: free) and solveTest (JD's own test
/// image, answer "KoYy2j": costs one image captcha per row). Also: accountCheck of a solver
/// gives its balance; solvers in JdResolverConfig go out while running and after every start.
///
/// Live: EMULE_HOSTER_LIVE=1, a built resolver.zip (scripts/build-hoster-resolver.sh) and Java
/// 17+. Accounts: EMULE_CAPTCHA_* in the project .env (see .env.example); a solver without
/// them skips. One sidecar for all rows.

#include "TestHelpers.h"

#include "Ed3kJavaRuntime.h"
#include "Ed3kJdResolver.h"
#include "Ed3kResolver.h"

#include <QElapsedTimer>
#include <QHash>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <memory>
#include <optional>
#include <string_view>

using namespace eMule::ed3k;

namespace {

/// A solver service and the .env names of its login.
struct SolverEnv {
    const char* tag;
    const char* host;   ///< the JD plugin's domain
    const char* key;    ///< key-only services: the key (as password); else nullptr
    const char* user;
    const char* pass;
};

constexpr std::array kSolvers{
    SolverEnv{"anticaptcha", "anti-captcha.com", "EMULE_CAPTCHA_ANTICAPTCHA_KEY", nullptr, nullptr},
    SolverEnv{"capmonster", "capmonster.cloud", "EMULE_CAPTCHA_CAPMONSTER_KEY", nullptr, nullptr},
    SolverEnv{"2captcha", "2captcha.com", "EMULE_CAPTCHA_2CAPTCHA_KEY", nullptr, nullptr},
    SolverEnv{"9kw", "9kw.eu", "EMULE_CAPTCHA_9KW_KEY", nullptr, nullptr},
    SolverEnv{"deathbycaptcha", "deathbycaptcha.com", nullptr, "EMULE_CAPTCHA_DBC_USER", "EMULE_CAPTCHA_DBC_PASS"},
    SolverEnv{"deathbycaptcha2", "deathbycaptcha.com", nullptr, "EMULE_CAPTCHA_DBC2_USER", "EMULE_CAPTCHA_DBC2_PASS"},
    SolverEnv{"imagetyperz", "imagetyperz.com", nullptr, "EMULE_CAPTCHA_IMAGETYPERZ_USER", "EMULE_CAPTCHA_IMAGETYPERZ_PASS"},
    SolverEnv{"imagetyperz2", "imagetyperz.com", nullptr, "EMULE_CAPTCHA_IMAGETYPERZ2_USER", "EMULE_CAPTCHA_IMAGETYPERZ2_PASS"},
    SolverEnv{"endcaptcha", "endcaptcha.com", nullptr, "EMULE_CAPTCHA_ENDCAPTCHA_USER", "EMULE_CAPTCHA_ENDCAPTCHA_PASS"},
};

/// The account from the environment; nullopt: not configured.
std::optional<SolverAccount> accountFor(const SolverEnv& s)
{
    const auto env = [](const char* name) { return name ? qEnvironmentVariable(name).trimmed() : QString(); };
    SolverAccount a{QString::fromLatin1(s.host), env(s.user), s.key ? env(s.key) : env(s.pass)};
    if (a.password.isEmpty() || (s.user && a.user.isEmpty()))
        return std::nullopt;
    return a;
}

constexpr int kAccountTimeoutMs = 60 * 1000;
/// 9kw is solved by people; the others answer in seconds
constexpr int kSolveTimeoutMs = 5 * 60 * 1000;

} // namespace

class tst_HosterSolvers : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void live_solverAccounts_data();
    void live_solverAccounts();
    void live_solverSolvesImage_data();
    void live_solverSolvesImage();
    /// An account of a service JD has no plugin for, and a wrong key.
    void live_solverRefusesUnknown();
    /// accountCheck (the Options "Check" button) on a solver host: valid and its credit.
    void live_accountCheckGivesBalance_data();
    void live_accountCheckGivesBalance();
    /// JdResolverConfig::solvers: sent to the running sidecar, and again by a fresh one.
    void live_configSolversSent();

private:
    /// Starts the shared sidecar once; false: @p why to skip.
    bool resolver(QString* why);
    /// Registers @p accounts and returns the Solvers event; nullopt: none in time.
    std::optional<ResolverEvent> setAccounts(const QList<SolverAccount>& accounts);
    /// The final event of @p task, waited for up to @p ms.
    std::optional<ResolverEvent> finalOf(Ed3kResolver::TaskId task, int ms);
    void rows();
    /// The first configured key-only solver; nullopt: none in .env.
    std::optional<SolverAccount> anyKeyAccount() const;
    /// Waits until @p r reports solver states.
    static bool waitStates(const Ed3kJdResolver& r, int ms);

    QTemporaryDir m_tmp;
    std::unique_ptr<Ed3kJavaRuntime> m_java;
    std::unique_ptr<Ed3kJdResolver> m_resolver;
    QString m_skip;   ///< why the sidecar cannot run; set once tried
    bool m_tried = false;
    QHash<Ed3kResolver::TaskId, ResolverEvent> m_final;
    JdResolverConfig m_cfg;
};

void tst_HosterSolvers::initTestCase()
{
    eMule::testing::loadProjectEnv();
}

void tst_HosterSolvers::live_solverAccounts_data()
{
    rows();
}

/// Each account alone: valid, and a balance (free to ask).
void tst_HosterSolvers::live_solverAccounts()
{
    QFETCH(int, index);
    const SolverEnv& s = kSolvers[std::size_t(index)];
    const auto acc = accountFor(s);
    if (!acc)
        QSKIP(qPrintable(QStringLiteral("no %1 account in .env").arg(QLatin1StringView(s.tag))));
    QString why;
    if (!resolver(&why))
        QSKIP(qPrintable(why));
    const auto ev = setAccounts({*acc});
    QVERIFY2(ev, "no answer to solverAccounts");
    QCOMPARE(ev->kind, ResolverEvent::Kind::Solvers);
    QCOMPARE(ev->solvers.size(), 1);
    const SolverAccountState& st = ev->solvers.first();
    QCOMPARE(st.host, QString::fromLatin1(s.host));
    if (st.code == QLatin1StringView("io"))
        QSKIP(qPrintable(QStringLiteral("%1 unreachable: %2").arg(st.host, st.text)));
    QVERIFY2(st.valid, qPrintable(QStringLiteral("%1: %2 %3").arg(st.host, st.code, st.text)));
    QVERIFY(st.balance >= 0);
    qInfo("%s: balance %.4f %s", s.tag, st.balance, qPrintable(st.currency));
}

void tst_HosterSolvers::live_solverSolvesImage_data()
{
    rows();
}

/// JD's test image through one solver, as a hoster's puzzle would go (paid).
void tst_HosterSolvers::live_solverSolvesImage()
{
    QFETCH(int, index);
    const SolverEnv& s = kSolvers[std::size_t(index)];
    const auto acc = accountFor(s);
    if (!acc)
        QSKIP(qPrintable(QStringLiteral("no %1 account in .env").arg(QLatin1StringView(s.tag))));
    QString why;
    if (!resolver(&why))
        QSKIP(qPrintable(why));
    const auto added = setAccounts({*acc});
    QVERIFY2(added && added->solvers.size() == 1, "no answer to solverAccounts");
    const SolverAccountState& st = added->solvers.first();
    if (!st.valid)
        QSKIP(qPrintable(QStringLiteral("%1 account not valid: %2 %3").arg(st.host, st.code, st.text)));
    if (st.balance <= 0)
        QSKIP(qPrintable(QStringLiteral("%1 has no credit").arg(st.host)));

    const auto t = m_resolver->solveTest(QString::fromLatin1(s.host), QStringLiteral("IMAGE"));
    const auto ev = finalOf(t, kSolveTimeoutMs);
    QVERIFY2(ev, "no answer to solveTest");
    QVERIFY2(ev->kind == ResolverEvent::Kind::Solved, qPrintable(QStringLiteral("%1: %2").arg(ev->code, ev->text)));
    QCOMPARE(ev->host, QString::fromLatin1(s.host));
    QCOMPARE(ev->name, QStringLiteral("KoYy2j"));
    qInfo("%s: '%s'", s.tag, qPrintable(ev->text));
    // case is the hard part of this image; some services ignore it
    QCOMPARE(ev->text.toLower(), ev->name.toLower());
}

void tst_HosterSolvers::live_solverRefusesUnknown()
{
    QString why;
    if (!resolver(&why))
        QSKIP(qPrintable(why));
    const auto ev = setAccounts({{QStringLiteral("no-such-solver.example"), {}, QStringLiteral("x")},
                                 {QStringLiteral("2captcha.com"), {}, QStringLiteral("00000000000000000000000000000000")}});
    QVERIFY2(ev, "no answer to solverAccounts");
    QCOMPARE(ev->solvers.size(), 2);
    QCOMPARE(ev->solvers[0].code, QStringLiteral("noPlugin"));
    QVERIFY(!ev->solvers[0].valid);
    QVERIFY(!ev->solvers[1].valid);
    QCOMPARE(ev->solvers[1].host, QStringLiteral("2captcha.com"));
    // nothing valid left: no solver takes the test puzzle
    const auto t = m_resolver->solveTest(QStringLiteral("2captcha.com"), QStringLiteral("IMAGE"));
    const auto solved = finalOf(t, kAccountTimeoutMs);
    QVERIFY(solved);
    QCOMPARE(solved->kind, ResolverEvent::Kind::Failed);
    QCOMPARE(solved->code, QStringLiteral("noSolver"));
}

void tst_HosterSolvers::live_accountCheckGivesBalance_data()
{
    rows();
}

void tst_HosterSolvers::live_accountCheckGivesBalance()
{
    QFETCH(int, index);
    const SolverEnv& s = kSolvers[std::size_t(index)];
    const auto acc = accountFor(s);
    if (!acc)
        QSKIP(qPrintable(QStringLiteral("no %1 account in .env").arg(QLatin1StringView(s.tag))));
    QString why;
    if (!resolver(&why))
        QSKIP(qPrintable(why));
    // as Ed3kHosterJoin::checkAccount sends it: a key-only service gets a stand-in user
    const QString user = acc->user.isEmpty() ? QStringLiteral("apikey") : acc->user;
    const auto ev = finalOf(m_resolver->accountCheck(acc->host, ResolverAccount{user, acc->password}), kAccountTimeoutMs * 3);
    QVERIFY2(ev, "no answer to accountCheck");
    if (ev->kind == ResolverEvent::Kind::Failed && ev->code == QLatin1StringView("io"))
        QSKIP(qPrintable(QStringLiteral("%1 unreachable").arg(acc->host)));
    QVERIFY2(ev->kind == ResolverEvent::Kind::Account, qPrintable(QStringLiteral("%1: %2 %3").arg(acc->host, ev->code, ev->text)));
    QVERIFY(ev->valid);
    QVERIFY2(ev->balance >= 0, "no balance in the account event");
    QVERIFY(!ev->currency.isEmpty());
}

void tst_HosterSolvers::live_configSolversSent()
{
    const auto acc = anyKeyAccount();
    if (!acc)
        QSKIP("no key-only solver in .env");
    QString why;
    if (!resolver(&why))
        QSKIP(qPrintable(why));
    // running: a config change goes out at once
    JdResolverConfig cfg = m_cfg;
    cfg.solvers = {*acc};
    finalOf(m_resolver->sites(QStringLiteral("rapidgator.net")), kAccountTimeoutMs * 3);   // up and running
    m_resolver->setConfig(cfg);
    QVERIFY2(waitStates(*m_resolver, kAccountTimeoutMs), "running sidecar got no solvers");
    QCOMPARE(m_resolver->solverStates().size(), 1);
    QCOMPARE(m_resolver->solverStates().first().host, acc->host);

    // a fresh sidecar: sent with its start, before anything asks for them
    QTemporaryDir dir;
    Ed3kJdResolver fresh;
    cfg.dataDir = dir.filePath(QStringLiteral("resolver"));
    fresh.setConfig(cfg);
    QVERIFY(fresh.available());
    (void)fresh.sites(QStringLiteral("rapidgator.net"));
    QVERIFY2(waitStates(fresh, kAccountTimeoutMs * 3), "fresh sidecar got no solvers");
    const SolverAccountState st = fresh.solverStates().first();
    if (st.code == QLatin1StringView("io"))
        QSKIP(qPrintable(QStringLiteral("%1 unreachable").arg(st.host)));
    QVERIFY2(st.valid, qPrintable(QStringLiteral("%1: %2").arg(st.host, st.code)));

    // none: cleared again
    cfg = m_cfg;
    m_resolver->setConfig(cfg);
    QVERIFY(m_resolver->solverStates().isEmpty());
}

bool tst_HosterSolvers::resolver(QString* why)
{
    if (!m_tried) {
        m_tried = true;
        if (qEnvironmentVariable("EMULE_HOSTER_LIVE") != QLatin1StringView("1")) {
            m_skip = QStringLiteral("live: set EMULE_HOSTER_LIVE=1");
        } else if (const QString zip = Ed3kJdResolver::findBundledZip(); zip.isEmpty()) {
            m_skip = QStringLiteral("no resolver.zip: run scripts/build-hoster-resolver.sh");
        } else {
            m_java = std::make_unique<Ed3kJavaRuntime>(m_tmp.path());
            m_java->locate({});
            QElapsedTimer probe;
            probe.start();
            while (m_java->state() == Ed3kJavaRuntime::State::Probing && probe.elapsed() < 60000)
                QTest::qWait(50);
            if (!m_java->runtime()) {
                m_skip = QStringLiteral("no Java 17+ on this machine");
            } else {
                m_resolver = std::make_unique<Ed3kJdResolver>();
                JdResolverConfig cfg;
                cfg.java = m_java->runtime()->java;
                cfg.dataDir = m_tmp.filePath(QStringLiteral("resolver"));
                cfg.zip = zip;
                cfg.askPuzzles = false;
                m_cfg = cfg;
                m_resolver->setConfig(cfg);
                if (!m_resolver->available())
                    m_skip = m_resolver->unavailableReason();
                connect(m_resolver.get(), &Ed3kResolver::taskEvent, this,
                        [this](Ed3kResolver::TaskId t, const ResolverEvent& e) {
                            if (e.isFinal())
                                m_final.insert(t, e);
                        });
            }
        }
    }
    *why = m_skip;
    return m_skip.isEmpty();
}

std::optional<ResolverEvent> tst_HosterSolvers::setAccounts(const QList<SolverAccount>& accounts)
{
    // the first call also waits for JD to start
    return finalOf(m_resolver->setSolverAccounts(accounts), kAccountTimeoutMs * 3);
}

std::optional<ResolverEvent> tst_HosterSolvers::finalOf(Ed3kResolver::TaskId task, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!m_final.contains(task) && t.elapsed() < ms)
        QTest::qWait(50);
    if (!m_final.contains(task))
        return std::nullopt;
    return m_final.take(task);
}

void tst_HosterSolvers::rows()
{
    QTest::addColumn<int>("index");
    for (std::size_t i = 0; i < kSolvers.size(); ++i)
        QTest::newRow(kSolvers[i].tag) << int(i);
}

std::optional<SolverAccount> tst_HosterSolvers::anyKeyAccount() const
{
    for (const SolverEnv& s : kSolvers)
        if (s.key)
            if (const auto a = accountFor(s); a && s.host != std::string_view("9kw.eu"))   // 9kw: often down
                return a;
    return std::nullopt;
}

bool tst_HosterSolvers::waitStates(const Ed3kJdResolver& r, int ms)
{
    QElapsedTimer t;
    t.start();
    while (r.solverStates().isEmpty() && t.elapsed() < ms)
        QTest::qWait(50);
    return !r.solverStates().isEmpty();
}

QTEST_GUILESS_MAIN(tst_HosterSolvers)
#include "tst_HosterSolvers.moc"
