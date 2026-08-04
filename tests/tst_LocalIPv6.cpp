/// @file tst_LocalIPv6.cpp
/// @brief Tests for net/LocalIPv6 — stable-vs-temporary ranking, /proc parsing,
///        Windows lifetime tie-breaking and publicIPv6Override resolution.
///
/// Everything here is pure: the platform gatherers are excluded on purpose, so the
/// suite behaves identically on macOS, Windows and Linux.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "net/LocalIPv6.h"
#include "prefs/Preferences.h"

#include <QTest>

using namespace eMule;

namespace {

/// Build a candidate entry. Defaults match a healthy stable SLAAC address.
LocalIPv6Address makeEntry(const char* addr,
                           IPv6AddressKind kind = IPv6AddressKind::Stable,
                           bool deprecated = false,
                           bool tentative = false,
                           const char* ifName = "en0",
                           uint32 validLifetime = 0)
{
    LocalIPv6Address e;
    e.address           = Address::fromString(QString::fromLatin1(addr));
    e.interfaceName     = QString::fromLatin1(ifName);
    e.kind              = kind;
    e.deprecated        = deprecated;
    e.tentative         = tentative;
    e.validLifetimeSecs = validLifetime;
    return e;
}

IPv6PrivacyReport makeReport(std::vector<LocalIPv6Address> addrs,
                             IPv6PrivacyPolicy policy = IPv6PrivacyPolicy::EnabledPreferred)
{
    IPv6PrivacyReport r;
    r.policy    = policy;
    r.addresses = std::move(addrs);
    return r;
}

// Real addresses observed on the dev machine, so the fixtures mirror production.
constexpr const char* kStable    = "2a0d:3344:15db:3200:4e6:a98b:52a:8c8d";
constexpr const char* kTemporary = "2a0d:3344:15db:3200:fc3f:93ac:9e7f:4bf8";
constexpr const char* kUla       = "fda0:440f:4c87:0:185b:333e:5b9f:532";
constexpr const char* kLinkLocal = "fe80::428:12d3:898c:608c";

} // namespace

class tst_LocalIPv6 : public QObject {
    Q_OBJECT

private slots:
    // selectPreferredIPv6 — tier ordering
    void select_prefersStableOverTemporary();
    void select_prefersStableWhenTemporaryListedFirst();
    void select_unknownBeatsTemporary();
    void select_fallsBackToTemporary();
    void select_skipsDeprecatedStable();
    void select_neverReturnsTentative();
    void select_allDeprecated_stillReturnsOne();
    void select_emptyReport_returnsNull();
    void select_rejectsUlaAndLinkLocal();
    void select_preservesOsOrderAmongStable();

    // resolveIPv6Override
    void override_empty_isNull();
    void override_invalidLiteral_reports();
    void override_ipv4Literal_reports();
    void override_notGlobalUnicast_reports();
    void override_notLocallyAssigned_reports();
    void override_matches_isUsed();
    void override_matchesTemporary_warnsButUses();

    // parseProcNetIfInet6
    void proc_parsesTemporaryAndPermanent();
    void proc_secondaryPermanent_isStable();
    void proc_skipsNonGlobalScope();
    void proc_flagsDeprecatedAndTentative();
    void proc_ignoresMalformedLines();

    // classifyByPrefixLifetime
    void lifetime_shortestInPrefixIsTemporary();
    void lifetime_singleMember_staysUnknown();
    void lifetime_equalLifetimes_stayUnknown();
    void lifetime_differentPrefixes_notGrouped();
    void lifetime_differentInterfaces_notGrouped();

    // updatePublicIPv6 — tier plumbing
    void update_populatesLocalSetAndTierSlots();
    void update_doesNotDisturbServerObservedTier();

    // misc
    void disableCommand_isNonEmpty();
    void disableCommand_coversSourceSelection();
    void removalCommands_nameOnlyTemporaryAddresses();
    void removalCommands_emptyWithoutTemporary();
    void report_hasHelpers();
};

// ---------------------------------------------------------------------------
// selectPreferredIPv6
// ---------------------------------------------------------------------------

void tst_LocalIPv6::select_prefersStableOverTemporary()
{
    const auto r = makeReport({makeEntry(kStable, IPv6AddressKind::Stable),
                               makeEntry(kTemporary, IPv6AddressKind::Temporary)});
    QCOMPARE(selectPreferredIPv6(r).toString(), QString::fromLatin1(kStable));
}

void tst_LocalIPv6::select_prefersStableWhenTemporaryListedFirst()
{
    // Order must not decide the outcome — this is the actual bug being fixed.
    const auto r = makeReport({makeEntry(kTemporary, IPv6AddressKind::Temporary),
                               makeEntry(kStable, IPv6AddressKind::Stable)});
    QCOMPARE(selectPreferredIPv6(r).toString(), QString::fromLatin1(kStable));
}

void tst_LocalIPv6::select_unknownBeatsTemporary()
{
    // A detection failure must never be worse than plain first-match.
    const auto r = makeReport({makeEntry(kTemporary, IPv6AddressKind::Temporary),
                               makeEntry(kStable, IPv6AddressKind::Unknown)});
    QCOMPARE(selectPreferredIPv6(r).toString(), QString::fromLatin1(kStable));
}

void tst_LocalIPv6::select_fallsBackToTemporary()
{
    const auto r = makeReport({makeEntry(kTemporary, IPv6AddressKind::Temporary)});
    QCOMPARE(selectPreferredIPv6(r).toString(), QString::fromLatin1(kTemporary));
}

void tst_LocalIPv6::select_skipsDeprecatedStable()
{
    // A preferred temporary address beats a deprecated stable one (tier 1 requires
    // !deprecated), because a deprecated address is no longer used for new flows.
    const auto r = makeReport({makeEntry(kStable, IPv6AddressKind::Stable, /*deprecated*/ true),
                               makeEntry(kTemporary, IPv6AddressKind::Temporary)});
    QCOMPARE(selectPreferredIPv6(r).toString(), QString::fromLatin1(kTemporary));
}

void tst_LocalIPv6::select_neverReturnsTentative()
{
    const auto r = makeReport({makeEntry(kStable, IPv6AddressKind::Stable,
                                         /*deprecated*/ false, /*tentative*/ true)});
    QVERIFY(selectPreferredIPv6(r).isNull());
}

void tst_LocalIPv6::select_allDeprecated_stillReturnsOne()
{
    const auto r = makeReport({makeEntry(kStable, IPv6AddressKind::Stable, /*deprecated*/ true)});
    QCOMPARE(selectPreferredIPv6(r).toString(), QString::fromLatin1(kStable));
}

void tst_LocalIPv6::select_emptyReport_returnsNull()
{
    QVERIFY(selectPreferredIPv6(makeReport({})).isNull());
}

void tst_LocalIPv6::select_rejectsUlaAndLinkLocal()
{
    // isPublicIP() must filter these even if a backend let them through.
    const auto r = makeReport({makeEntry(kUla, IPv6AddressKind::Stable),
                               makeEntry(kLinkLocal, IPv6AddressKind::Stable)});
    QVERIFY(selectPreferredIPv6(r).isNull());
}

void tst_LocalIPv6::select_preservesOsOrderAmongStable()
{
    const auto r = makeReport({makeEntry("2a0d:3344:15db:3200::1", IPv6AddressKind::Stable),
                               makeEntry(kStable, IPv6AddressKind::Stable)});
    QCOMPARE(selectPreferredIPv6(r).toString(), QStringLiteral("2a0d:3344:15db:3200::1"));
}

// ---------------------------------------------------------------------------
// resolveIPv6Override
// ---------------------------------------------------------------------------

void tst_LocalIPv6::override_empty_isNull()
{
    const auto r = makeReport({makeEntry(kStable)});
    QString reason;
    QVERIFY(resolveIPv6Override(QString(), r, &reason).isNull());
    QVERIFY2(reason.isEmpty(), "an unset preference must not produce a log line");
}

void tst_LocalIPv6::override_invalidLiteral_reports()
{
    const auto r = makeReport({makeEntry(kStable)});
    QString reason;
    QVERIFY(resolveIPv6Override(QStringLiteral("not-an-address"), r, &reason).isNull());
    QVERIFY(reason.contains(QStringLiteral("not a valid IPv6 literal")));
}

void tst_LocalIPv6::override_ipv4Literal_reports()
{
    const auto r = makeReport({makeEntry(kStable)});
    QString reason;
    QVERIFY(resolveIPv6Override(QStringLiteral("192.0.2.1"), r, &reason).isNull());
    QVERIFY(!reason.isEmpty());
}

void tst_LocalIPv6::override_notGlobalUnicast_reports()
{
    const auto r = makeReport({makeEntry(kStable)});
    QString reason;
    QVERIFY(resolveIPv6Override(QString::fromLatin1(kUla), r, &reason).isNull());
    QVERIFY(reason.contains(QStringLiteral("not a global-unicast")));
}

void tst_LocalIPv6::override_notLocallyAssigned_reports()
{
    const auto r = makeReport({makeEntry(kStable)});
    QString reason;
    const Address got = resolveIPv6Override(QStringLiteral("2001:4860:4860::8888"), r, &reason);
    QVERIFY(got.isNull());
    QVERIFY(reason.contains(QStringLiteral("not assigned to any local interface")));
}

void tst_LocalIPv6::override_matches_isUsed()
{
    const auto r = makeReport({makeEntry(kTemporary, IPv6AddressKind::Temporary),
                               makeEntry(kStable, IPv6AddressKind::Stable)});
    QString reason;
    const Address got = resolveIPv6Override(QString::fromLatin1(kStable), r, &reason);
    QCOMPARE(got.toString(), QString::fromLatin1(kStable));
    QVERIFY(reason.contains(QStringLiteral("en0")));
}

void tst_LocalIPv6::override_matchesTemporary_warnsButUses()
{
    const auto r = makeReport({makeEntry(kTemporary, IPv6AddressKind::Temporary)});
    QString reason;
    const Address got = resolveIPv6Override(QString::fromLatin1(kTemporary), r, &reason);
    QCOMPARE(got.toString(), QString::fromLatin1(kTemporary));
    QVERIFY2(reason.contains(QStringLiteral("will rotate")),
             "an explicitly pinned temporary address must still be flagged");
}

// ---------------------------------------------------------------------------
// parseProcNetIfInet6
// ---------------------------------------------------------------------------

void tst_LocalIPv6::proc_parsesTemporaryAndPermanent()
{
    // 2a0d:3344:15db:3200:4e6:a98b:52a:8c8d flags 80 (permanent) -> Stable
    // 2a0d:3344:15db:3200:fc3f:93ac:9e7f:4bf8 flags 01 (temporary) -> Temporary
    const QString text = QStringLiteral(
        "2a0d334415db320004e6a98b052a8c8d 02 40 00 80 eth0\n"
        "2a0d334415db3200fc3f93ac9e7f04bf 02 40 00 01 eth0\n");

    const auto out = parseProcNetIfInet6(text);
    QCOMPARE(out.size(), std::size_t{2});
    QCOMPARE(out[0].kind, IPv6AddressKind::Stable);
    QCOMPARE(out[1].kind, IPv6AddressKind::Temporary);
    QCOMPARE(out[0].interfaceName, QStringLiteral("eth0"));
}

void tst_LocalIPv6::proc_secondaryPermanent_isStable()
{
    // IFA_F_SECONDARY shares bit 0 with IFA_F_TEMPORARY. Flags 0x81 is a manually
    // added permanent secondary address and must NOT be classified as temporary.
    const QString text = QStringLiteral(
        "2a0d334415db320004e6a98b052a8c8d 02 40 00 81 eth0\n");

    const auto out = parseProcNetIfInet6(text);
    QCOMPARE(out.size(), std::size_t{1});
    QCOMPARE(out[0].kind, IPv6AddressKind::Stable);
}

void tst_LocalIPv6::proc_skipsNonGlobalScope()
{
    // scope 20 = link-local, scope 10 = host. Only scope 00 is global.
    const QString text = QStringLiteral(
        "fe800000000000000428012d3898c608c 02 40 20 80 eth0\n"
        "00000000000000000000000000000001 01 80 10 80 lo\n"
        "2a0d334415db320004e6a98b052a8c8d 02 40 00 80 eth0\n");

    const auto out = parseProcNetIfInet6(text);
    QCOMPARE(out.size(), std::size_t{1});
    QCOMPARE(out[0].address.toString(), QString::fromLatin1(kStable));
}

void tst_LocalIPv6::proc_flagsDeprecatedAndTentative()
{
    const QString text = QStringLiteral(
        "2a0d334415db320004e6a98b052a8c8d 02 40 00 20 eth0\n"   // deprecated
        "2a0d334415db3200fc3f93ac9e7f04bf 02 40 00 40 eth0\n"); // tentative

    const auto out = parseProcNetIfInet6(text);
    QCOMPARE(out.size(), std::size_t{2});
    QVERIFY(out[0].deprecated);
    QVERIFY(!out[0].tentative);
    QVERIFY(out[1].tentative);
}

void tst_LocalIPv6::proc_ignoresMalformedLines()
{
    const QString text = QStringLiteral(
        "\n"
        "short line\n"
        "notlongenough 02 40 00 80 eth0\n"                       // addr not 32 chars
        "zzzz334415db320004e6a98b052a8c8d 02 40 00 80 eth0\n"    // non-hex addr
        "2a0d334415db320004e6a98b052a8c8d 02 40 00 80 eth0\n");  // the only good one

    const auto out = parseProcNetIfInet6(text);
    QCOMPARE(out.size(), std::size_t{1});
    QCOMPARE(out[0].address.toString(), QString::fromLatin1(kStable));
}

// ---------------------------------------------------------------------------
// classifyByPrefixLifetime
// ---------------------------------------------------------------------------

void tst_LocalIPv6::lifetime_shortestInPrefixIsTemporary()
{
    std::vector<LocalIPv6Address> addrs{
        makeEntry(kStable, IPv6AddressKind::Unknown, false, false, "en0", 2592000),  // 30d
        makeEntry(kTemporary, IPv6AddressKind::Unknown, false, false, "en0", 604800), // 7d
    };
    classifyByPrefixLifetime(addrs);
    QCOMPARE(addrs[0].kind, IPv6AddressKind::Stable);
    QCOMPARE(addrs[1].kind, IPv6AddressKind::Temporary);
}

void tst_LocalIPv6::lifetime_singleMember_staysUnknown()
{
    std::vector<LocalIPv6Address> addrs{
        makeEntry(kStable, IPv6AddressKind::Unknown, false, false, "en0", 2592000),
    };
    classifyByPrefixLifetime(addrs);
    QCOMPARE(addrs[0].kind, IPv6AddressKind::Unknown);
}

void tst_LocalIPv6::lifetime_equalLifetimes_stayUnknown()
{
    std::vector<LocalIPv6Address> addrs{
        makeEntry(kStable, IPv6AddressKind::Unknown, false, false, "en0", 604800),
        makeEntry(kTemporary, IPv6AddressKind::Unknown, false, false, "en0", 604800),
    };
    classifyByPrefixLifetime(addrs);
    QCOMPARE(addrs[0].kind, IPv6AddressKind::Unknown);
    QCOMPARE(addrs[1].kind, IPv6AddressKind::Unknown);
}

void tst_LocalIPv6::lifetime_differentPrefixes_notGrouped()
{
    std::vector<LocalIPv6Address> addrs{
        makeEntry("2a0d:3344:15db:3200::1", IPv6AddressKind::Unknown, false, false, "en0", 2592000),
        makeEntry("2a0d:3344:15db:9900::1", IPv6AddressKind::Unknown, false, false, "en0", 604800),
    };
    classifyByPrefixLifetime(addrs);
    QCOMPARE(addrs[0].kind, IPv6AddressKind::Unknown);
    QCOMPARE(addrs[1].kind, IPv6AddressKind::Unknown);
}

void tst_LocalIPv6::lifetime_differentInterfaces_notGrouped()
{
    std::vector<LocalIPv6Address> addrs{
        makeEntry(kStable, IPv6AddressKind::Unknown, false, false, "en0", 2592000),
        makeEntry(kTemporary, IPv6AddressKind::Unknown, false, false, "en1", 604800),
    };
    classifyByPrefixLifetime(addrs);
    QCOMPARE(addrs[0].kind, IPv6AddressKind::Unknown);
    QCOMPARE(addrs[1].kind, IPv6AddressKind::Unknown);
}

// ---------------------------------------------------------------------------
// misc
// ---------------------------------------------------------------------------

void tst_LocalIPv6::disableCommand_isNonEmpty()
{
    // Every supported platform must offer an actionable command for the advisory.
    QVERIFY(!ipv6PrivacyDisableCommand().isEmpty());
}

void tst_LocalIPv6::disableCommand_coversSourceSelection()
{
    // Regression: the macOS command used to set use_tempaddr only, which stops new temporary
    // addresses but leaves existing ones preferred as the outgoing source — so the
    // advertised-vs-source mismatch survived the "fix". Measured 2026-07-30.
    const QString cmd = ipv6PrivacyDisableCommand();
#ifdef Q_OS_MACOS
    QVERIFY(cmd.contains(QStringLiteral("use_tempaddr=0")));
    QVERIFY(cmd.contains(QStringLiteral("prefer_tempaddr=0")));
#else
    QVERIFY(!cmd.isEmpty());
#endif
}

void tst_LocalIPv6::removalCommands_nameOnlyTemporaryAddresses()
{
    const auto report = makeReport({
        makeEntry(kStable, IPv6AddressKind::Stable, false, false, "en0"),
        makeEntry(kTemporary, IPv6AddressKind::Temporary, false, false, "en0"),
    });
    const QStringList cmds = ipv6TemporaryAddressRemovalCommands(report);

#if defined(Q_OS_WIN)
    // No supported per-address delete — stay silent rather than print something unactionable.
    QVERIFY(cmds.isEmpty());
#else
    QCOMPARE(cmds.size(), 1);
    QVERIFY(cmds.first().contains(QStringLiteral("en0")));
    // The stable address must never be named: removing it would kill inbound IPv6 outright.
    QVERIFY(!cmds.first().contains(QString::fromLatin1(kStable)));
#ifdef Q_OS_MACOS
    QVERIFY(cmds.first().contains(QString::fromLatin1(kTemporary)));
#endif
#endif
}

void tst_LocalIPv6::removalCommands_emptyWithoutTemporary()
{
    const auto report = makeReport({makeEntry(kStable, IPv6AddressKind::Stable)},
                                   IPv6PrivacyPolicy::Disabled);
    QVERIFY(ipv6TemporaryAddressRemovalCommands(report).isEmpty());
}

void tst_LocalIPv6::report_hasHelpers()
{
    const auto mixed = makeReport({makeEntry(kStable, IPv6AddressKind::Stable),
                                   makeEntry(kTemporary, IPv6AddressKind::Temporary)});
    QVERIFY(mixed.hasStableAddress());
    QVERIFY(mixed.hasTemporaryAddress());

    const auto stableOnly = makeReport({makeEntry(kStable, IPv6AddressKind::Stable)});
    QVERIFY(stableOnly.hasStableAddress());
    QVERIFY(!stableOnly.hasTemporaryAddress());

    // A deprecated stable address does not count as a usable stable address.
    const auto deprecated = makeReport(
        {makeEntry(kStable, IPv6AddressKind::Stable, /*deprecated*/ true)});
    QVERIFY(!deprecated.hasStableAddress());
}

// ---------------------------------------------------------------------------
// updatePublicIPv6 — feeds AppContext's tier slots and local-address set
// ---------------------------------------------------------------------------

void tst_LocalIPv6::update_populatesLocalSetAndTierSlots()
{
    thePrefs.setPublicIPv6Override(QString());
    theApp.clearPublicIPv6Observed();
    theApp.setPublicIPv6Override(Address{});
    theApp.setPublicIPv6Local(Address{});

    const auto report = makeReport({makeEntry(kStable, IPv6AddressKind::Stable),
                                    makeEntry(kTemporary, IPv6AddressKind::Temporary)});
    const Address effective = updatePublicIPv6(report);

    // The stable address wins the auto-selection, and both scanned addresses become
    // available for the local-ownership check that gates every reflected address.
    QCOMPARE(effective.toString(), QString::fromLatin1(kStable));
    QCOMPARE(theApp.publicIPv6Local().toString(), QString::fromLatin1(kStable));
    QVERIFY(theApp.isLocalIPv6(Address::fromString(QString::fromLatin1(kStable))));
    QVERIFY(theApp.isLocalIPv6(Address::fromString(QString::fromLatin1(kTemporary))));
    QVERIFY(!theApp.isLocalIPv6(Address::fromString(QStringLiteral("2606:4700::1"))));

    // The operator pin lands in its own slot and outranks the auto pick.
    thePrefs.setPublicIPv6Override(QString::fromLatin1(kTemporary));
    QCOMPARE(updatePublicIPv6(report).toString(), QString::fromLatin1(kTemporary));
    QCOMPARE(theApp.publicIPv6Override().toString(), QString::fromLatin1(kTemporary));
    QCOMPARE(theApp.publicIPv6Local().toString(), QString::fromLatin1(kStable));

    thePrefs.setPublicIPv6Override(QString());
}

void tst_LocalIPv6::update_doesNotDisturbServerObservedTier()
{
    thePrefs.setPublicIPv6Override(QString());
    theApp.clearPublicIPv6Observed();
    theApp.setPublicIPv6Override(Address{});
    theApp.setPublicIPv6Local(Address{});

    const auto report = makeReport({makeEntry(kStable, IPv6AddressKind::Stable),
                                    makeEntry(kTemporary, IPv6AddressKind::Temporary)});
    updatePublicIPv6(report);

    // The server observed us on the temporary address — that is our real egress.
    const Address observed = Address::fromString(QString::fromLatin1(kTemporary));
    theApp.setPublicIPv6Observed(observed, QStringLiteral("server.example"));
    QCOMPARE(theApp.publicIPv6().toString(), observed.toString());

    // Re-running the scan (as every server reconnect does) must not knock the
    // server-observed value out of the way with the interface pick.
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(updatePublicIPv6(report).toString(), observed.toString());
        QCOMPARE(theApp.publicIPv6().toString(), observed.toString());
        QCOMPARE(theApp.publicIPv6Local().toString(), QString::fromLatin1(kStable));
    }

    theApp.clearPublicIPv6Observed();
    QCOMPARE(theApp.publicIPv6().toString(), QString::fromLatin1(kStable));
    theApp.setLocalIPv6Addresses({});
    theApp.setPublicIPv6Local(Address{});
}

#include "tst_LocalIPv6.moc"

QTEST_GUILESS_MAIN(tst_LocalIPv6)
