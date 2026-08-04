/// @file tst_PortMapWire.cpp
/// @brief Tests for the PCP (RFC 6887) and NAT-PMP (RFC 6886) codecs.
///
/// Everything here is pure — no sockets, no event loop, no clock — so the suite
/// behaves identically on every platform. Timing rules take `now` and the random
/// fraction as parameters precisely so they can be asserted without qWait().
///
/// Several vectors are real bytes captured from the dev FRITZ!Box by
/// scripts/probe-portmap.py; they are marked as such. They pin down firmware
/// behaviour that the RFCs do not describe.

#include "TestHelpers.h"
#include "portmap/NatPmpMessages.h"
#include "portmap/PcpMessages.h"
#include "portmap/PortMapTypes.h"
#include "portmap/PortMapWire.h"

#include <QTest>

using namespace eMule;
using namespace eMule::portmap;

namespace {

QByteArray hex(const char* text)
{
    return QByteArray::fromHex(text);
}

std::span<const uint8> bytesOf(const QByteArray& data)
{
    return asBytes(data);
}

pcp::Nonce testNonce()
{
    return {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};
}

pcp::MapRequest makeMapRequest(const char* client = "192.168.1.42", bool ipv6 = false)
{
    pcp::MapRequest r;
    r.nonce = testNonce();
    r.protocol = pcp::IpProtocol::Tcp;
    r.internalPort = 4662;
    r.suggestedExternalPort = 4662;
    r.clientAddress = Address::fromString(QString::fromLatin1(client));
    r.lifetimeSecs = 7200;
    r.externalFamilyIsIPv4 = !ipv6;
    return r;
}

// -- Golden vectors ---------------------------------------------------------

// NAT-PMP MAP request: TCP, internal 4662, suggest 4662, lifetime 7200.
constexpr const char* kNatPmpMapRequest = "000200001236123600001c20";
// NAT-PMP MAP response: SUCCESS, epoch 4242, internal 4662, external 62000, lifetime 3600.
constexpr const char* kNatPmpMapResponse = "00820000000010921236f23000000e10";
// NAT-PMP external-address response: 203.0.113.7. Natural byte order.
constexpr const char* kNatPmpExtAddrResponse = "0080000000001092cb007107";
// NAT-PMP delete: UDP internal 4672, suggested port 0, lifetime 0.
constexpr const char* kNatPmpDelete = "000100001240000000000000";
// NAT-PMP UNSUPP_VERSION, erratum-3618 form (OP = 128).
constexpr const char* kNatPmpVersionError = "0080000100001092";
// Same, pre-errata literal form: R bit clear.
constexpr const char* kNatPmpVersionErrorNoRBit = "0000000100001092";

// PCP MAP request, IPv4 client 192.168.1.42.
constexpr const char* kPcpMapRequestV4 =
    "0201000000001c2000000000000000000000ffffc0a8012a"
    "00112233445566778899aabb060000001236123600000000000000000000ffff00000000";
// PCP MAP request, IPv6 client; note the all-zeros IPv6 suggested external.
constexpr const char* kPcpMapRequestV6 =
    "0201000000001c202a0d334415db320004e6a98b052a8c8d"
    "00112233445566778899aabb060000001236123600000000000000000000000000000000";
// PCP MAP response: SUCCESS, lifetime 3600, epoch 4242, external 203.0.113.7:62000.
constexpr const char* kPcpMapResponse =
    "0281000000000e100000109200000000000000000000000000112233445566778899aabb"
    "060000001236f23000000000000000000000ffffcb007107";
// PCP ADDRESS_MISMATCH: result 12, and the echoed port/address are our request's.
constexpr const char* kPcpAddressMismatch =
    "0281000c000007080000109200000000000000000000000000112233445566778899aabb"
    "060000001236123600000000000000000000ffff00000000";

// -- Captured from the dev FRITZ!Box ---------------------------------------

// A PCP box answering our 12-byte NAT-PMP MAP request: version 2, opcode 130,
// result 1. Bytes 4..7 are our own echoed port bytes, NOT an epoch.
constexpr const char* kFritzPcpAnswersNatPmp = "028200010000070800001092";
// The same box answering our 2-byte NAT-PMP request with only TWO bytes, where
// RFC 6886 section 3.5 mandates eight.
constexpr const char* kFritzNatPmpTwoByteReject = "0280";
// A real successful IPv6 PCP MAP response: 120s lease, pinhole on port 42391,
// external address == internal address (no translation, firewall only).
constexpr const char* kFritzPcpMapV6 =
    "0281000000000078005501a7000000000000000000000000"
    "8a205cdcf2b28f80daa4706906000000a597a597"
    "2a0d334415db320004e6a98b052a8c8d";

} // namespace

class tst_PortMapWire : public QObject {
    Q_OBJECT

private slots:
    // -- 128-bit address codec ---------------------------------------------
    void addr128_encodesIPv4AsMapped();
    void addr128_encodesIPv6Verbatim();
    void addr128_nullPicksFamilySpecificZeros();
    void addr128_decodesMappedToIPv4();
    void addr128_decodesBothAllZeroFormsAsNull();
    void addr128_checksAllNinetySixLeadingBits();
    void addr128_roundTrips();

    // -- NAT-PMP encode ----------------------------------------------------
    void natpmp_encodeMapRequest_matchesGolden();
    void natpmp_encodeExtAddrRequest_isTwoZeroBytes();
    void natpmp_encodeDelete_zeroesPortAndLifetime();
    void natpmp_opcodeUdpIsOneTcpIsTwo();

    // -- NAT-PMP decode ----------------------------------------------------
    void natpmp_decodeMapResponse_matchesGolden();
    void natpmp_decodeExtAddr_usesNaturalByteOrder();
    void natpmp_decodeVersionError_erratumForm();
    void natpmp_decodeVersionError_acceptsClearRBit();
    void natpmp_decodeTwoByteReject_fromRealFritzBox();
    void natpmp_versionErrorNeverExposesAnEpoch();
    void natpmp_decodeTruncated_everyShortPrefixFails();
    void natpmp_decodeTrailingBytes_accepted();
    void natpmp_decodeRequestOpcode_rejected();
    void natpmp_unknownResultCode_survives();

    // -- PCP encode --------------------------------------------------------
    void pcp_encodeMapRequestV4_matchesGolden();
    void pcp_encodeMapRequestV6_matchesGolden();
    void pcp_encodeDelete_zeroesSuggestedPortAndAddress();
    void pcp_encodeAnnounce_isTwentyFourBytes();
    void pcp_encodePreferFailure_appendsOption();
    void pcp_encodeIsAlwaysMultipleOfFourAndUnder1100();

    // -- PCP decode --------------------------------------------------------
    void pcp_decodeMapResponse_matchesGolden();
    void pcp_resultCodeIsEightBitsAtOffsetThree();
    void pcp_decodeHeader_survivesEightByteReply();
    void pcp_decodeHeader_survivesTwoByteReply();
    void pcp_decodeMapResponse_rejectsShortAndUnaligned();
    void pcp_decodeMapResponse_rejectsOversized();
    void pcp_addressMismatch_doesNotReportEchoedPortAsAssigned();
    void pcp_unknownResultCode_parsesWithoutEnumCast();
    void pcp_decodeRealFritzBoxIPv6Mapping();
    void pcp_generateNonceIsRandomAndNonZero();

    // -- response matching -------------------------------------------------
    void match_acceptsWellFormedResponse();
    void match_rejectsNonceMismatch();
    void match_rejectsPortAndProtocolMismatch();
    void match_rejectsWrongDestination();

    // -- cross-protocol classification -------------------------------------
    void classify_byFirstByte();
    void classify_realFritzBoxRepliesDowngradeAndUpgrade();

    // -- pure timing arithmetic --------------------------------------------
    void lifetime_clampsAbsurdSuccessToTwentyFourHours();
    void lifetime_clampsErrorHoldoffToThirtyMinutes();
    void renewal_firstAttemptFallsBetweenHalfAndFiveEighths();
    void renewal_laterAttemptsConverge();
    void renewal_neverBelowFourSeconds();
    void renewal_natPmpUsesHalfLife();
    void retransmit_doublesAndSaturatesAtMrt();
    void retransmit_jitterStaysWithinTenPercent();

    // -- epoch / reboot detection ------------------------------------------
    void epoch_firstResponseIsAlwaysValid();
    void epoch_toleratesOneSecondOfReordering();
    void epoch_rejectsLargerBackwardsJump();
    void epoch_toleratesSixPercentDrift();
    void epoch_detectsRebootToZero();
    void epoch_natPmpSevenEighthsRule();

    // -- PortMapping semantics ---------------------------------------------
    void mapping_portMismatchIsNotUsable();
    void mapping_cgnatExternalAddressIsNotUsable();
    void mapping_publicMatchingPortIsUsable();
};

// ---------------------------------------------------------------------------
// 128-bit address codec
// ---------------------------------------------------------------------------

void tst_PortMapWire::addr128_encodesIPv4AsMapped()
{
    const auto out = encodeAddr128(Address::fromString(QStringLiteral("192.168.1.42")), true);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(out.data()), 16).toHex(),
             QByteArray("00000000000000000000ffffc0a8012a"));
}

void tst_PortMapWire::addr128_encodesIPv6Verbatim()
{
    const auto addr = Address::fromString(QStringLiteral("2a0d:3344:15db:3200:4e6:a98b:52a:8c8d"));
    const auto out = encodeAddr128(addr, false);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(out.data()), 16).toHex(),
             QByteArray("2a0d334415db320004e6a98b052a8c8d"));
}

void tst_PortMapWire::addr128_nullPicksFamilySpecificZeros()
{
    // The whole reason kAnyIPv4 is a hard constant: Address cannot express it.
    QCOMPARE(encodeAddr128(Address{}, /*wantIPv4Family=*/true), kAnyIPv4);
    QCOMPARE(encodeAddr128(Address{}, /*wantIPv4Family=*/false), kAnyIPv6);
    // They differ only at bytes 10-11, which is exactly what selects the family.
    QVERIFY(kAnyIPv4 != kAnyIPv6);
}

void tst_PortMapWire::addr128_decodesMappedToIPv4()
{
    const QByteArray raw = hex("00000000000000000000ffffcb007107");
    const Address addr = decodeAddr128(bytesOf(raw).subspan<0, 16>());
    QVERIFY(addr.isIPv4());
    QCOMPARE(addr.toString(), QStringLiteral("203.0.113.7"));
}

void tst_PortMapWire::addr128_decodesBothAllZeroFormsAsNull()
{
    const QByteArray anyV4 = hex("00000000000000000000ffff00000000");
    const QByteArray anyV6 = hex("00000000000000000000000000000000");
    QVERIFY(decodeAddr128(bytesOf(anyV4).subspan<0, 16>()).isNull());
    QVERIFY(decodeAddr128(bytesOf(anyV6).subspan<0, 16>()).isNull());
}

void tst_PortMapWire::addr128_checksAllNinetySixLeadingBits()
{
    // Section 5 insists the full 96-bit prefix is checked. Testing only bits
    // 81-96 would misread this as the IPv4 address 1.2.3.4.
    const QByteArray notMapped = hex("00010000000000000000ffff01020304");
    const Address addr = decodeAddr128(bytesOf(notMapped).subspan<0, 16>());
    QVERIFY(addr.isIPv6());
}

void tst_PortMapWire::addr128_roundTrips()
{
    for (const char* text : {"1.2.3.4", "203.0.113.7", "2001:db8::1",
                             "2a0d:3344:15db:3200:4e6:a98b:52a:8c8d", "fe80::1"}) {
        const Address original = Address::fromString(QString::fromLatin1(text));
        const auto encoded = encodeAddr128(original, original.isIPv4());
        QCOMPARE(decodeAddr128(std::span<const uint8, 16>(encoded)), original);
    }
}

// ---------------------------------------------------------------------------
// NAT-PMP encode
// ---------------------------------------------------------------------------

void tst_PortMapWire::natpmp_encodeMapRequest_matchesGolden()
{
    natpmp::MapRequest r;
    r.opcode = natpmp::Opcode::MapTcp;
    r.internalPort = 4662;
    r.suggestedExternalPort = 4662;
    r.lifetimeSecs = 7200;
    QCOMPARE(natpmp::encode(r).toHex(), QByteArray(kNatPmpMapRequest));
    QCOMPARE(natpmp::encode(r).size(), qsizetype(natpmp::kMapRequestSize));
}

void tst_PortMapWire::natpmp_encodeExtAddrRequest_isTwoZeroBytes()
{
    QCOMPARE(natpmp::encodeExternalAddressRequest().toHex(), QByteArray("0000"));
}

void tst_PortMapWire::natpmp_encodeDelete_zeroesPortAndLifetime()
{
    QCOMPARE(natpmp::encodeDelete(natpmp::Opcode::MapUdp, 4672).toHex(),
             QByteArray(kNatPmpDelete));
}

void tst_PortMapWire::natpmp_opcodeUdpIsOneTcpIsTwo()
{
    QCOMPARE(static_cast<uint8>(natpmp::Opcode::MapUdp), uint8(1));
    QCOMPARE(static_cast<uint8>(natpmp::Opcode::MapTcp), uint8(2));
}

// ---------------------------------------------------------------------------
// NAT-PMP decode
// ---------------------------------------------------------------------------

void tst_PortMapWire::natpmp_decodeMapResponse_matchesGolden()
{
    const QByteArray raw = hex(kNatPmpMapResponse);
    const auto r = natpmp::decode(bytesOf(raw));
    QVERIFY(r.has_value());
    QCOMPARE(r->kind, natpmp::ReplyKind::Map);
    QCOMPARE(r->version, uint8(0));
    QCOMPARE(r->resultCode, uint16(0));
    QCOMPARE(r->secondsSinceEpoch, uint32(4242));
    QCOMPARE(r->internalPort, uint16(4662));
    QCOMPARE(r->mappedExternalPort, uint16(62000));
    QCOMPARE(r->lifetimeSecs, uint32(3600));
}

void tst_PortMapWire::natpmp_decodeExtAddr_usesNaturalByteOrder()
{
    const QByteArray raw = hex(kNatPmpExtAddrResponse);
    const auto r = natpmp::decode(bytesOf(raw));
    QVERIFY(r.has_value());
    QCOMPARE(r->kind, natpmp::ReplyKind::ExternalAddress);
    // A host-order bug yields 7.113.0.203 — still a plausible-looking public
    // address that passes isPublicIP(), so only a golden vector catches it.
    QCOMPARE(r->externalAddress.toString(), QStringLiteral("203.0.113.7"));
}

void tst_PortMapWire::natpmp_decodeVersionError_erratumForm()
{
    const QByteArray raw = hex(kNatPmpVersionError);
    const auto r = natpmp::decode(bytesOf(raw));
    QVERIFY(r.has_value());
    QCOMPARE(r->kind, natpmp::ReplyKind::VersionError);
    QCOMPARE(r->version, uint8(0));
    QVERIFY(!r->suggestsPcp());   // version 0 means "I am a NAT-PMP server"
}

void tst_PortMapWire::natpmp_decodeVersionError_acceptsClearRBit()
{
    // Firmware that copied the pre-errata section 3.5 figure sends OP = 0.
    // Section 8.3's "drop when R is clear" rule must not eat it.
    const QByteArray raw = hex(kNatPmpVersionErrorNoRBit);
    const auto r = natpmp::decode(bytesOf(raw));
    QVERIFY(r.has_value());
    QCOMPARE(r->kind, natpmp::ReplyKind::VersionError);
}

void tst_PortMapWire::natpmp_decodeTwoByteReject_fromRealFritzBox()
{
    // Captured live. RFC 6886 section 3.5 mandates eight bytes; this box sends
    // two. A decoder requiring four never detects the router at all.
    const QByteArray raw = hex(kFritzNatPmpTwoByteReject);
    QCOMPARE(raw.size(), qsizetype(2));
    const auto r = natpmp::decode(bytesOf(raw));
    QVERIFY(r.has_value());
    QCOMPARE(r->kind, natpmp::ReplyKind::VersionError);
    QCOMPARE(r->version, uint8(2));
    QVERIFY(r->suggestsPcp());
}

void tst_PortMapWire::natpmp_versionErrorNeverExposesAnEpoch()
{
    // In a version-mismatch reply, offsets 4..7 are PCP's Lifetime holding our
    // own echoed request bytes. Feeding that to epoch tracking would look like a
    // server reboot on every probe. Confirmed live: c059c059 is our port.
    const QByteArray raw = hex(kFritzPcpAnswersNatPmp);
    const auto r = natpmp::decode(bytesOf(raw));
    QVERIFY(r.has_value());
    QCOMPARE(r->kind, natpmp::ReplyKind::VersionError);
    QCOMPARE(r->secondsSinceEpoch, uint32(0));
}

void tst_PortMapWire::natpmp_decodeTruncated_everyShortPrefixFails()
{
    const QByteArray full = hex(kNatPmpMapResponse);
    for (qsizetype n = 0; n < full.size(); ++n) {
        const QByteArray prefix = full.left(n);
        const auto r = natpmp::decode(bytesOf(prefix));
        // Version 0 with fewer than the full 16 bytes cannot yield a Map reply.
        if (r.has_value())
            QVERIFY(r->kind != natpmp::ReplyKind::Map);
    }
}

void tst_PortMapWire::natpmp_decodeTrailingBytes_accepted()
{
    // Some CPEs pad. Accept size >= expected, never size == expected.
    QByteArray padded = hex(kNatPmpMapResponse);
    padded.append(8, '\0');
    const auto r = natpmp::decode(bytesOf(padded));
    QVERIFY(r.has_value());
    QCOMPARE(r->kind, natpmp::ReplyKind::Map);
    QCOMPARE(r->mappedExternalPort, uint16(62000));
}

void tst_PortMapWire::natpmp_decodeRequestOpcode_rejected()
{
    // A request (R bit clear) with a success result is not a response.
    const QByteArray raw = hex("00020000000010921236f23000000e10");
    QVERIFY(!natpmp::decode(bytesOf(raw)).has_value());
}

void tst_PortMapWire::natpmp_unknownResultCode_survives()
{
    const QByteArray raw = hex("008200ff000010921236f23000000e10");
    const auto r = natpmp::decode(bytesOf(raw));
    QVERIFY(r.has_value());
    QCOMPARE(r->resultCode, uint16(255));
    QVERIFY(natpmp::resultName(255).contains(QStringLiteral("unknown")));
}

// ---------------------------------------------------------------------------
// PCP encode
// ---------------------------------------------------------------------------

void tst_PortMapWire::pcp_encodeMapRequestV4_matchesGolden()
{
    const QByteArray out = pcp::encodeMapRequest(makeMapRequest());
    QCOMPARE(out.size(), qsizetype(pcp::kMapPacketSize));
    QCOMPARE(out.toHex(), QByteArray(kPcpMapRequestV4));
}

void tst_PortMapWire::pcp_encodeMapRequestV6_matchesGolden()
{
    auto r = makeMapRequest("2a0d:3344:15db:3200:4e6:a98b:52a:8c8d", /*ipv6=*/true);
    const QByteArray out = pcp::encodeMapRequest(r);
    QCOMPARE(out.size(), qsizetype(pcp::kMapPacketSize));
    QCOMPARE(out.toHex(), QByteArray(kPcpMapRequestV6));
    // The v4 and v6 forms differ at bytes 54-55 of the suggested external
    // address: ::ffff:0:0 versus ::. That single distinction selects the family.
    QCOMPARE(out.mid(54, 2).toHex(), QByteArray("0000"));
    QCOMPARE(pcp::encodeMapRequest(makeMapRequest()).mid(54, 2).toHex(), QByteArray("ffff"));
}

void tst_PortMapWire::pcp_encodeDelete_zeroesSuggestedPortAndAddress()
{
    const QByteArray out = pcp::encodeDeleteRequest(makeMapRequest());
    QCOMPARE(out.size(), qsizetype(pcp::kMapPacketSize));
    QCOMPARE(readU32(bytesOf(out), 4), uint32(0));           // lifetime 0 = delete
    QCOMPARE(readU16(bytesOf(out), 40), uint16(4662));       // internal port kept
    QCOMPARE(readU16(bytesOf(out), 42), uint16(0));          // suggested port zeroed
    QCOMPARE(out.mid(44, 16).toHex(), QByteArray("00000000000000000000ffff00000000"));
}

void tst_PortMapWire::pcp_encodeAnnounce_isTwentyFourBytes()
{
    const QByteArray out =
        pcp::encodeAnnounceRequest(Address::fromString(QStringLiteral("192.168.1.42")));
    QCOMPARE(out.size(), qsizetype(pcp::kHeaderSize));
    QCOMPARE(out.toHex(), QByteArray("020000000000000000000000000000000000ffffc0a8012a"));
}

void tst_PortMapWire::pcp_encodePreferFailure_appendsOption()
{
    auto r = makeMapRequest();
    r.preferFailure = true;
    const QByteArray out = pcp::encodeMapRequest(r);
    QCOMPARE(out.size(), qsizetype(pcp::kMapPacketSize + 4));
    QCOMPARE(out.right(4).toHex(), QByteArray("02000000"));
}

void tst_PortMapWire::pcp_encodeIsAlwaysMultipleOfFourAndUnder1100()
{
    for (const bool preferFailure : {false, true}) {
        for (const bool ipv6 : {false, true}) {
            auto r = makeMapRequest(ipv6 ? "2001:db8::1" : "10.0.0.1", ipv6);
            r.preferFailure = preferFailure;
            const QByteArray out = pcp::encodeMapRequest(r);
            QCOMPARE(out.size() % 4, qsizetype(0));
            QVERIFY(out.size() <= qsizetype(pcp::kMaxMessageSize));
        }
    }
}

// ---------------------------------------------------------------------------
// PCP decode
// ---------------------------------------------------------------------------

void tst_PortMapWire::pcp_decodeMapResponse_matchesGolden()
{
    const QByteArray raw = hex(kPcpMapResponse);
    QCOMPARE(raw.size(), qsizetype(pcp::kMapPacketSize));
    const auto r = pcp::decodeMapResponse(bytesOf(raw));
    QVERIFY(r.has_value());
    QVERIFY(r->isSuccess());
    QCOMPARE(r->lifetimeSecs, uint32(3600));
    QCOMPARE(r->epochTime, uint32(4242));
    QCOMPARE(r->nonce, testNonce());
    QCOMPARE(r->protocol, pcp::IpProtocol::Tcp);
    QCOMPARE(r->internalPort, uint16(4662));
    QCOMPARE(r->assignedExternalPort, uint16(62000));
    QCOMPARE(r->assignedExternalAddress.toString(), QStringLiteral("203.0.113.7"));
}

void tst_PortMapWire::pcp_resultCodeIsEightBitsAtOffsetThree()
{
    // Offset 2 is eight reserved bits; the result is a single byte at offset 3.
    // Reading a 16-bit code at offset 2 (NAT-PMP's layout) would give 12 here.
    QByteArray raw = hex(kPcpMapResponse);
    raw[2] = char(0x00);
    raw[3] = char(0x0C);
    const auto r = pcp::decodeMapResponse(bytesOf(raw));
    QVERIFY(r.has_value());
    QCOMPARE(r->resultCode, uint8(12));
    QCOMPARE(readU16(bytesOf(raw), 2), uint16(12));   // the tempting misread
}

void tst_PortMapWire::pcp_decodeHeader_survivesEightByteReply()
{
    // Section 8.3 orders the UNSUPP_VERSION check before the 24-octet minimum.
    // If decodeHeader enforced >= 24 we would never detect a NAT-PMP router.
    const QByteArray raw = hex(kNatPmpVersionError);
    const auto h = pcp::decodeHeader(bytesOf(raw));
    QVERIFY(h.has_value());
    QCOMPARE(h->version, uint8(0));
    QVERIFY(h->hasResultCode);
    QVERIFY(!h->hasFullHeader);
    QCOMPARE(h->resultCode, uint8(1));
    QVERIFY(h->isVersionMismatch());
}

void tst_PortMapWire::pcp_decodeHeader_survivesTwoByteReply()
{
    const QByteArray raw = hex(kFritzNatPmpTwoByteReject);
    const auto h = pcp::decodeHeader(bytesOf(raw));
    QVERIFY(h.has_value());
    QCOMPARE(h->version, uint8(2));
    QVERIFY(!h->hasResultCode);
    QVERIFY(h->isResponse);
}

void tst_PortMapWire::pcp_decodeMapResponse_rejectsShortAndUnaligned()
{
    const QByteArray full = hex(kPcpMapResponse);
    for (qsizetype n = 0; n < full.size(); ++n)
        QVERIFY(!pcp::decodeMapResponse(bytesOf(full.left(n))).has_value());

    QByteArray unaligned = full;
    unaligned.append('\0');   // 61 bytes: not a multiple of 4
    QVERIFY(!pcp::decodeMapResponse(bytesOf(unaligned)).has_value());
}

void tst_PortMapWire::pcp_decodeMapResponse_rejectsOversized()
{
    QByteArray oversized = hex(kPcpMapResponse);
    oversized.append(2000, '\0');
    QVERIFY(!pcp::decodeMapResponse(bytesOf(oversized)).has_value());
}

void tst_PortMapWire::pcp_addressMismatch_doesNotReportEchoedPortAsAssigned()
{
    // On an error the server echoes the request's opcode data (section 8.2), so
    // the port there is what we asked for. Treating it as assigned would publish
    // port 4662 as forwarded when nothing was mapped at all.
    const QByteArray raw = hex(kPcpAddressMismatch);
    const auto r = pcp::decodeMapResponse(bytesOf(raw));
    QVERIFY(r.has_value());
    QVERIFY(!r->isSuccess());
    QCOMPARE(r->resultCode, uint8(pcp::Result::AddressMismatch));
    QCOMPARE(r->assignedExternalPort, uint16(0));
    QVERIFY(r->assignedExternalAddress.isNull());
    // The echoed request data is still on the wire — we simply refuse to read it.
    QCOMPARE(readU16(bytesOf(raw), 42), uint16(4662));
}

void tst_PortMapWire::pcp_unknownResultCode_parsesWithoutEnumCast()
{
    for (const uint8 code : {uint8(14), uint8(99), uint8(255)}) {
        QByteArray raw = hex(kPcpMapResponse);
        raw[3] = static_cast<char>(code);
        const auto r = pcp::decodeMapResponse(bytesOf(raw));
        QVERIFY(r.has_value());
        QCOMPARE(r->resultCode, code);
        QVERIFY(pcp::resultName(code).contains(QStringLiteral("unknown")));
    }
}

void tst_PortMapWire::pcp_decodeRealFritzBoxIPv6Mapping()
{
    // Captured live: a successful IPv6 pinhole. No translation happens, so the
    // external port equals the internal port and the external address equals the
    // client's GUA.
    const QByteArray raw = hex(kFritzPcpMapV6);
    QCOMPARE(raw.size(), qsizetype(pcp::kMapPacketSize));
    const auto r = pcp::decodeMapResponse(bytesOf(raw));
    QVERIFY(r.has_value());
    QVERIFY(r->isSuccess());
    QCOMPARE(r->lifetimeSecs, uint32(120));
    QCOMPARE(r->internalPort, uint16(42391));
    QCOMPARE(r->assignedExternalPort, uint16(42391));
    QCOMPARE(r->assignedExternalAddress.toString(),
             QStringLiteral("2a0d:3344:15db:3200:4e6:a98b:52a:8c8d"));
}

void tst_PortMapWire::pcp_generateNonceIsRandomAndNonZero()
{
    const pcp::Nonce a = pcp::generateNonce();
    const pcp::Nonce b = pcp::generateNonce();
    const pcp::Nonce zero{};
    QVERIFY(a != zero);
    QVERIFY(a != b);
}

// ---------------------------------------------------------------------------
// Response matching
// ---------------------------------------------------------------------------

void tst_PortMapWire::match_acceptsWellFormedResponse()
{
    const auto request = makeMapRequest();
    const QByteArray raw = hex(kPcpMapResponse);
    const auto response = pcp::decodeMapResponse(bytesOf(raw));
    QVERIFY(response.has_value());
    QVERIFY(pcp::matchesRequest(*response, request, request.clientAddress));
}

void tst_PortMapWire::match_rejectsNonceMismatch()
{
    auto request = makeMapRequest();
    request.nonce[0] = 0xFF;
    const QByteArray raw = hex(kPcpMapResponse);
    const auto response = pcp::decodeMapResponse(bytesOf(raw));
    QVERIFY(response.has_value());
    QVERIFY(!pcp::matchesRequest(*response, request, request.clientAddress));
}

void tst_PortMapWire::match_rejectsPortAndProtocolMismatch()
{
    const QByteArray raw = hex(kPcpMapResponse);
    const auto response = pcp::decodeMapResponse(bytesOf(raw));
    QVERIFY(response.has_value());

    auto wrongPort = makeMapRequest();
    wrongPort.internalPort = 4663;
    QVERIFY(!pcp::matchesRequest(*response, wrongPort, wrongPort.clientAddress));

    auto wrongProtocol = makeMapRequest();
    wrongProtocol.protocol = pcp::IpProtocol::Udp;
    QVERIFY(!pcp::matchesRequest(*response, wrongProtocol, wrongProtocol.clientAddress));
}

void tst_PortMapWire::match_rejectsWrongDestination()
{
    const auto request = makeMapRequest();
    const QByteArray raw = hex(kPcpMapResponse);
    const auto response = pcp::decodeMapResponse(bytesOf(raw));
    QVERIFY(response.has_value());
    QVERIFY(!pcp::matchesRequest(*response, request,
                                 Address::fromString(QStringLiteral("192.168.1.99"))));
}

// ---------------------------------------------------------------------------
// Cross-protocol classification
// ---------------------------------------------------------------------------

void tst_PortMapWire::classify_byFirstByte()
{
    QCOMPARE(classifyDatagram(bytesOf(hex(kNatPmpMapResponse))), ProbeVerdict::SpeaksNatPmp);
    QCOMPARE(classifyDatagram(bytesOf(hex(kPcpMapResponse))), ProbeVerdict::SpeaksPcp);
    QCOMPARE(classifyDatagram(bytesOf(hex("07ff"))), ProbeVerdict::UnknownVersion);
    QCOMPARE(classifyDatagram({}), ProbeVerdict::Ignore);
}

void tst_PortMapWire::classify_realFritzBoxRepliesDowngradeAndUpgrade()
{
    // Our NAT-PMP request answered by a PCP box => upgrade to PCP.
    QCOMPARE(classifyDatagram(bytesOf(hex(kFritzPcpAnswersNatPmp))), ProbeVerdict::SpeaksPcp);
    QCOMPARE(classifyDatagram(bytesOf(hex(kFritzNatPmpTwoByteReject))), ProbeVerdict::SpeaksPcp);
    // Our PCP request answered by a NAT-PMP box => downgrade to NAT-PMP.
    QCOMPARE(classifyDatagram(bytesOf(hex(kNatPmpVersionError))), ProbeVerdict::SpeaksNatPmp);
}

// ---------------------------------------------------------------------------
// Timing arithmetic
// ---------------------------------------------------------------------------

void tst_PortMapWire::lifetime_clampsAbsurdSuccessToTwentyFourHours()
{
    QCOMPARE(clampGrantedLifetime(0xFFFFFFFFu, /*isError=*/false), kMaxSaneLifetimeSecs);
    QCOMPARE(clampGrantedLifetime(3600, false), uint32(3600));
}

void tst_PortMapWire::lifetime_clampsErrorHoldoffToThirtyMinutes()
{
    // Without this, one spoofed error carrying 0xFFFFFFFF disables port mapping
    // for 136 years.
    QCOMPARE(clampGrantedLifetime(0xFFFFFFFFu, /*isError=*/true), kMaxErrorHoldoffSecs);
    QCOMPARE(clampGrantedLifetime(60, true), uint32(60));
}

void tst_PortMapWire::renewal_firstAttemptFallsBetweenHalfAndFiveEighths()
{
    constexpr uint32 lifetime = 7200;
    QCOMPARE(renewalDelaySecs(lifetime, 0, 0.0), uint32(lifetime / 2));
    QCOMPARE(renewalDelaySecs(lifetime, 0, 1.0), uint32(lifetime * 5 / 8));
    const uint32 mid = renewalDelaySecs(lifetime, 0, 0.5);
    QVERIFY(mid >= lifetime / 2 && mid <= lifetime * 5 / 8);
}

void tst_PortMapWire::renewal_laterAttemptsConverge()
{
    constexpr uint32 lifetime = 3600;
    QCOMPARE(renewalDelaySecs(lifetime, 1, 0.0), uint32(lifetime * 3 / 4));
    QCOMPARE(renewalDelaySecs(lifetime, 2, 0.0), uint32(lifetime * 7 / 8));
    // Monotonically later, and never past the lifetime itself.
    uint32 previous = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32 delay = renewalDelaySecs(lifetime, attempt, 0.0);
        QVERIFY(delay >= previous);
        QVERIFY(delay < lifetime);
        previous = delay;
    }
}

void tst_PortMapWire::renewal_neverBelowFourSeconds()
{
    QCOMPARE(renewalDelaySecs(1, 0, 0.0), kMinRenewGapSecs);
    QCOMPARE(renewalDelaySecs(0, 5, 0.0), kMinRenewGapSecs);
}

void tst_PortMapWire::renewal_natPmpUsesHalfLife()
{
    QCOMPARE(natPmpRenewalDelaySecs(7200), uint32(3600));
    QCOMPARE(natPmpRenewalDelaySecs(2), kMinRenewGapSecs);
}

void tst_PortMapWire::retransmit_doublesAndSaturatesAtMrt()
{
    QCOMPARE(nextRetransmitMs(0, 0.5), qint64(3000));          // IRT, no jitter at 0.5
    QCOMPARE(nextRetransmitMs(3000, 0.5), qint64(6000));
    QCOMPARE(nextRetransmitMs(6000, 0.5), qint64(12000));
    // Saturates at MRT rather than doubling forever.
    QCOMPARE(nextRetransmitMs(1'024'000, 0.5), qint64(1'024'000));
    QCOMPARE(nextRetransmitMs(2'000'000, 0.5), qint64(1'024'000));
}

void tst_PortMapWire::retransmit_jitterStaysWithinTenPercent()
{
    QCOMPARE(nextRetransmitMs(0, 0.0), qint64(2700));   // -10%
    QCOMPARE(nextRetransmitMs(0, 1.0), qint64(3300));   // +10%
    for (const double r : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const qint64 value = nextRetransmitMs(10'000, r);
        QVERIFY(value >= 18'000 && value <= 22'000);
    }
}

// ---------------------------------------------------------------------------
// Epoch / reboot detection
// ---------------------------------------------------------------------------

void tst_PortMapWire::epoch_firstResponseIsAlwaysValid()
{
    EpochTracker tracker;
    QVERIFY(!tracker.hasSample());
    QVERIFY(tracker.validate(999'999, 0));
    QVERIFY(tracker.hasSample());
}

void tst_PortMapWire::epoch_toleratesOneSecondOfReordering()
{
    EpochTracker tracker;
    QVERIFY(tracker.validate(1000, 0));
    QVERIFY(tracker.validate(999, 1));   // reordered by one second
}

void tst_PortMapWire::epoch_rejectsLargerBackwardsJump()
{
    EpochTracker tracker;
    QVERIFY(tracker.validate(1000, 0));
    QVERIFY(!tracker.validate(990, 1));
}

void tst_PortMapWire::epoch_toleratesSixPercentDrift()
{
    EpochTracker tracker;
    QVERIFY(tracker.validate(1000, 0));
    // Server advanced 106s while we observed 100s: inside the 1/16 allowance
    // that cheap CPE crystals need.
    QVERIFY(tracker.validate(1106, 100));

    EpochTracker slow;
    QVERIFY(slow.validate(1000, 0));
    QVERIFY(slow.validate(1094, 100));
}

void tst_PortMapWire::epoch_detectsRebootToZero()
{
    EpochTracker tracker;
    QVERIFY(tracker.validate(500'000, 0));
    QVERIFY(!tracker.validate(0, 60));   // router rebooted, all mappings gone
}

void tst_PortMapWire::epoch_natPmpSevenEighthsRule()
{
    // Steady state: SSSoE advanced by roughly the elapsed time.
    QVERIFY(!natPmpEpochIndicatesReboot(1000, 1100, 100));
    // Reboot: SSSoE restarted.
    QVERIFY(natPmpEpochIndicatesReboot(1000, 5, 100));
    // A slow server clock stays inside the 7/8 allowance.
    QVERIFY(!natPmpEpochIndicatesReboot(1000, 1088, 100));
}

// ---------------------------------------------------------------------------
// PortMapping semantics
// ---------------------------------------------------------------------------

void tst_PortMapWire::mapping_portMismatchIsNotUsable()
{
    PortMapping m;
    m.request.internalPort = 4662;
    m.externalPort = 62000;
    m.externalAddress = Address::fromString(QStringLiteral("203.0.113.7"));
    QVERIFY(!m.portMatches());
    // eD2K advertises thePrefs.port() and has no external-port tag, so this is
    // a silent LowID rather than a working mapping.
    QVERIFY(!m.isUsable());
}

void tst_PortMapWire::mapping_cgnatExternalAddressIsNotUsable()
{
    PortMapping m;
    m.request.internalPort = 4662;
    m.externalPort = 4662;
    // Exactly what the dev FRITZ!Box returns: 100.64.0.0/10 carrier-grade NAT.
    m.externalAddress = Address::fromString(QStringLiteral("100.83.250.167"));
    QVERIFY(m.portMatches());
    QVERIFY(!m.isUsable());
}

void tst_PortMapWire::mapping_publicMatchingPortIsUsable()
{
    PortMapping m;
    m.request.internalPort = 4662;
    m.externalPort = 4662;
    // Not 203.0.113.x — that is RFC 5737 documentation space, which isPublicIP()
    // correctly refuses. The wire vectors above use it precisely because they
    // only exercise decoding.
    m.externalAddress = Address::fromString(QStringLiteral("93.184.216.34"));
    QVERIFY(m.isUsable());
}

QTEST_MAIN(tst_PortMapWire)
#include "tst_PortMapWire.moc"
