/// @file tst_SourceExchangeCompat.cpp
/// @brief Interop/conformance tests for eD2K Source Exchange across peer dialects.
///
/// tst_SourceExchange.cpp covers the unit level — layout, byte order, caps.
/// This file covers the thing that unit tests cannot: that two *different*
/// implementations still understand each other. Extended Source Exchange (ExtSX)
/// rides on the same opcode pair as classic SX2 (OP_REQUESTSOURCES2 / OP_ANSWERSOURCES2)
/// and is distinguished only by a capability bit the peer advertised, so the two
/// dialects have to be kept apart by construction, in both directions:
///
///   - a peer without MODMISC_EXTXS must keep receiving byte-identical classic bytes;
///   - a peer with it, including one whose reader is stricter than the format allows,
///     must be able to read every source we send it;
///   - our reader must survive whatever a non-conforming peer sends back.
///
/// The two foreign readers are modelled in this file rather than mocked, so a drift
/// in our emitter shows up as a foreign reader losing sources.
///
/// Reference implementations:
///   srchybrid/KnownFile.cpp:1007-1150   CKnownFile::CreateSrcInfoPacket
///   srchybrid/PartFile.cpp:3752-3900    CPartFile::AddClientSources
///   srchybrid/ListenSocket.cpp:852-1046 the shared multipacket handler
/// Normative spec: docs/protocol/ipv6-spec.md §3.3 (ExtSX), §0.3 (tag encodings).

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "net/Address.h"
#include "net/ClientReqSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "transfer/DownloadQueue.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QHostAddress>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

using namespace eMule;

namespace {

/// SX2 payload prefix: version byte (1) + file hash (16). SX1 omits the version byte.
constexpr int kSx2HeaderSize = 1 + 16;
constexpr int kSx1HeaderSize = 16;

/// Byte size of one classic per-source record at a given version — the value both
/// sides multiply by the declared count. srchybrid/PartFile.cpp:3774-3798.
constexpr int classicRecordSize(uint8 version)
{
    int n = 4 + 2 + 4 + 2;      // clientID, tcpPort, serverIP, serverPort
    if (version >= 2) n += 16;  // userHash
    if (version >= 4) n += 1;   // cryptOptions
    return n;
}

// ---------------------------------------------------------------------------
// Little-endian byte helpers
// ---------------------------------------------------------------------------

uint32 readU32(const char* p, int off)
{
    uint32 v;
    std::memcpy(&v, p + off, 4);
    return v;
}

uint16 readU16(const char* p, int off)
{
    uint16 v;
    std::memcpy(&v, p + off, 2);
    return v;
}

void appendU16(QByteArray& b, uint16 v)
{
    b.append(char(v & 0xFF));
    b.append(char((v >> 8) & 0xFF));
}

void appendU32(QByteArray& b, uint32 v)
{
    for (int i = 0; i < 4; ++i)
        b.append(char((v >> (8 * i)) & 0xFF));
}

/// The payload of a built answer, minus the version byte and file hash — i.e. exactly
/// what addClientSources() and the foreign readers below are handed.
QByteArray answerBody(const Packet& p, bool isSX2)
{
    const int header = isSX2 ? kSx2HeaderSize : kSx1HeaderSize;
    return QByteArray(p.pBuffer + header, static_cast<int>(p.size) - header);
}

// ---------------------------------------------------------------------------
// Foreign reader #1 — stock eMule's fixed-record parser
//
// A faithful re-implementation of srchybrid/PartFile.cpp:3752-3830: the declared
// count is multiplied by a version-derived record size and must equal the remaining
// data exactly, otherwise the *whole* answer is discarded. This is the check our
// classic output has to keep satisfying, byte for byte.
// ---------------------------------------------------------------------------

struct LegacySource {
    uint32 clientId = 0;        ///< as it appeared on the wire
    uint16 tcpPort = 0;
    uint32 serverIp = 0;        ///< network order, as on the wire
    uint16 serverPort = 0;
    std::array<uint8, 16> userHash{};
    bool hasUserHash = false;
    uint8 cryptOptions = 0;
    bool hasCryptOptions = false;
};

struct LegacyParse {
    bool accepted = false;
    uint8 layoutVersion = 0;    ///< the version the reader inferred / accepted
    std::vector<LegacySource> sources;
};

LegacyParse legacyClassicReader(const QByteArray& body, uint8 announcedVersion, bool isSX2)
{
    LegacyParse out;
    if (body.size() < 2)
        return out;

    const uint16 count = readU16(body.constData(), 0);
    const qint64 dataSize = body.size() - 2;

    uint8 layout = 0;
    if (!isSX2) {
        // SX1 has no version byte: infer the layout from the record size, then require
        // the peer to have announced at least that much (srchybrid/PartFile.cpp:3773-3820).
        if (count != 0 && dataSize == qint64(count) * classicRecordSize(1))
            layout = 1;
        else if (count != 0 && dataSize == qint64(count) * classicRecordSize(2))
            layout = (announcedVersion == 2) ? 2 : 3;
        else if (count != 0 && dataSize == qint64(count) * classicRecordSize(4))
            layout = 4;
        else
            return out;
        if (announcedVersion < layout)
            return out;
    } else {
        // srchybrid/PartFile.cpp:3821-3835: a known version and an exact length, or nothing.
        if (announcedVersion == 0 || announcedVersion > SOURCEEXCHANGE2_VERSION)
            return out;
        layout = announcedVersion;
        if (dataSize != qint64(count) * classicRecordSize(layout))
            return out;
    }

    int off = 2;
    for (uint16 i = 0; i < count; ++i) {
        LegacySource s;
        s.clientId = readU32(body.constData(), off);            off += 4;
        s.tcpPort = readU16(body.constData(), off);             off += 2;
        s.serverIp = readU32(body.constData(), off);            off += 4;
        s.serverPort = readU16(body.constData(), off);          off += 2;
        if (layout >= 2) {
            std::memcpy(s.userHash.data(), body.constData() + off, 16);
            s.hasUserHash = true;
            off += 16;
        }
        if (layout >= 4) {
            s.cryptOptions = uint8(body.at(off));
            s.hasCryptOptions = true;
            off += 1;
        }
        out.sources.push_back(s);
    }

    out.accepted = true;
    out.layoutVersion = layout;
    return out;
}

// ---------------------------------------------------------------------------
// Foreign reader #2 — the intolerant ExtSX reader
//
// docs/protocol/ipv6-spec.md:1115: at least one deployed IPv6-capable implementation
// appends every unrecognised tag to an error string and then returns out of the whole
// source-exchange parse, so one unknown tag in the first record costs it every source
// in the packet — including the ones it had already accepted. MODMISC_EXTXS_SKIPTAGS
// exists solely so we can avoid handing that reader a tag it does not know.
//
// It knows exactly the three tags that existed before the userhash/crypt extension.
// ---------------------------------------------------------------------------

struct ExtSxSource {
    uint32 clientId = 0;
    uint16 tcpPort = 0;
    uint32 serverIp = 0;
    uint16 serverPort = 0;
    Address ipv6;
};

std::vector<ExtSxSource> intolerantExtSxReader(const QByteArray& body)
{
    static const std::vector<uint8> known{ CT_EMULE_SERVERIP, CT_EMULE_SERVERTCP, CT_MOD_IP_V6 };

    std::vector<ExtSxSource> out;
    SafeMemFile io(reinterpret_cast<const uint8*>(body.constData()), body.size());
    try {
        const uint16 count = io.readUInt16();
        for (uint16 i = 0; i < count; ++i) {
            ExtSxSource s;
            s.clientId = io.readUInt32();
            s.tcpPort = io.readUInt16();
            const uint8 tagCount = io.readUInt8();
            for (uint8 t = 0; t < tagCount; ++t) {
                Tag tag(io, true);
                if (std::find(known.begin(), known.end(), tag.nameId()) == known.end())
                    return {};   // the whole answer is abandoned, not just this tag
                switch (tag.nameId()) {
                case CT_EMULE_SERVERIP:
                    s.serverIp = tag.intValue();
                    break;
                case CT_EMULE_SERVERTCP:
                    s.serverPort = static_cast<uint16>(tag.intValue());
                    break;
                case CT_MOD_IP_V6:
                    if (tag.isHash())
                        s.ipv6 = Address::fromIPv6Bytes(tag.hashValue());
                    break;
                default:
                    break;
                }
            }
            out.push_back(s);
        }
    } catch (...) {
        return {};
    }
    return out;
}

/// Every tag name id present in an ExtSX answer body, in emission order per record.
std::vector<std::vector<uint8>> extSxTagIds(const QByteArray& body)
{
    std::vector<std::vector<uint8>> out;
    SafeMemFile io(reinterpret_cast<const uint8*>(body.constData()), body.size());
    const uint16 count = io.readUInt16();
    for (uint16 i = 0; i < count; ++i) {
        io.readUInt32();
        io.readUInt16();
        const uint8 tagCount = io.readUInt8();
        std::vector<uint8> ids;
        for (uint8 t = 0; t < tagCount; ++t) {
            Tag tag(io, true);
            ids.push_back(tag.nameId());
        }
        out.push_back(std::move(ids));
    }
    return out;
}

// ---------------------------------------------------------------------------
// A socket that records instead of sending
//
// sendPacket is virtual, so a peer's outgoing packets can be captured with no TCP and
// no event loop. That matters for the negative assertions here: the real socket flushes
// from a zero-timer, so "no packet was sent" would otherwise be vacuously true.
//
// It derives from ClientReqSocket rather than EMSocket so that wireIncomingSocket() will
// take it, which is what gives the tests a way to deliver an inbound packet — the
// dispatch slot itself is private.
// ---------------------------------------------------------------------------

class RecordingSocket : public ClientReqSocket {
    Q_OBJECT

public:
    struct Sent {
        uint8 opcode = 0;
        uint8 prot = 0;
        QByteArray payload;
    };

    std::vector<Sent> sent;

    void sendPacket(std::unique_ptr<Packet> packet, bool /*controlPacket*/ = true,
                    uint32 /*actualPayloadSize*/ = 0,
                    bool /*forceImmediateSend*/ = false) override
    {
        sent.push_back(Sent{ packet->opcode, packet->prot,
                             QByteArray(packet->pBuffer, static_cast<int>(packet->size)) });
    }

    [[nodiscard]] const Sent* find(uint8 opcode) const
    {
        for (const auto& s : sent)
            if (s.opcode == opcode)
                return &s;
        return nullptr;
    }

    [[nodiscard]] int countOf(uint8 opcode) const
    {
        int n = 0;
        for (const auto& s : sent)
            if (s.opcode == opcode)
                ++n;
        return n;
    }

    /// Deliver an eMule-extended-protocol packet as if it had arrived on the wire.
    void deliverExt(const QByteArray& payload, uint8 opcode)
    {
        emit extPacketReceived(reinterpret_cast<const uint8*>(payload.constData()),
                               static_cast<uint32>(payload.size()), opcode);
    }

    /// Deliver a fully framed packet through the real dispatch, protocol byte and all.
    /// deliverExt() jumps straight to the signal and so skips packetReceived — which is
    /// exactly where an OP_PACKEDPROT payload gets inflated. Anything testing compression
    /// has to come through here. The Packet is assembled the way EMSocket::onReadyRead
    /// does it (header ctor + standalone `new char[size + 1]` pBuffer), because that is the
    /// buffer ownership unPackPacket then frees.
    bool deliverWire(uint8 protocol, uint8 opcode, const QByteArray& payload)
    {
        char header[kPacketHeaderSize];
        auto* h = reinterpret_cast<HeaderStruct*>(header);
        h->eDonkeyID = protocol;
        h->packetLength = static_cast<uint32>(payload.size()) + 1;
        h->command = opcode;

        Packet packet(header);
        packet.pBuffer = new char[packet.size + 1];
        std::memcpy(packet.pBuffer, payload.constData(), packet.size);
        return packetReceived(&packet);
    }
};

// ---------------------------------------------------------------------------
// A shared file the upload side can find
//
// processMultiPacket* and processRequestSources* resolve the requested file through
// theApp.sharedFileList, so the multipacket tests need one standing up.
// ---------------------------------------------------------------------------

struct SharedFileFixture {
    KnownFileList knownFiles;
    SharedFileList shared{ &knownFiles };
    std::unique_ptr<KnownFile> owned{ std::make_unique<KnownFile>() };
    KnownFile* file = nullptr;

    SharedFileFixture()
    {
        uint8 hash[16];
        std::memset(hash, 0x2C, sizeof(hash));
        owned->setFileHash(hash);
        owned->setFileName(QStringLiteral("sx-shared.bin"));
        owned->setFileSize(EMFileSize(PARTSIZE));
        file = owned.get();

        theApp.knownFileList = &knownFiles;
        theApp.sharedFileList = &shared;
        shared.safeAddKFile(file, /*onlyAdd*/ true);
    }

    ~SharedFileFixture()
    {
        theApp.sharedFileList = nullptr;
        theApp.knownFileList = nullptr;
    }
};

// ---------------------------------------------------------------------------
// Client fixtures
// ---------------------------------------------------------------------------

/// A source shaped like a client that completed a handshake: address and hybrid ID
/// consistent, and in an upload state that makes it eligible to be handed out.
UpDownClient* makeHighIdClient(const QString& ip, uint16 port, uint8 hashPattern = 0xAB)
{
    auto* c = new UpDownClient;
    const Address addr = Address::fromString(ip);
    c->setUserAddress(addr);
    c->setUserIDHybrid(addr.toUint32());
    c->setUserPort(port);
    c->setServerAddress(Address::fromString(QStringLiteral("1.2.3.4")));
    c->setServerPort(4661);
    c->setUploadState(UploadState::Uploading);

    uint8 hash[16];
    std::memset(hash, hashPattern, sizeof(hash));
    c->setUserHash(hash);
    return c;
}

/// The peer asking us for sources. `extSX` drives MODMISC_EXTXS, `skipTags` bit 5.
UpDownClient* makeRequester(bool extSX = false, bool skipTags = false, bool supportsSX2 = true)
{
    auto* c = makeHighIdClient(QStringLiteral("99.98.97.96"), 4665, 0x99);
    c->setSupportsSourceExchange2(supportsSX2);
    c->setSourceExchange1Ver(4);
    c->setSupportsExtendedXS(extSX);
    c->setSupportsExtSXSkipTags(skipTags);
    return c;
}

/// Build a hello-answer buffer (no leading hash-size byte) — the peer-side view of
/// what a client advertises. Mirrors tst_UpDownClient.cpp:624.
QByteArray buildHelloAnswer(uint8 hashPattern, uint32 userId, uint16 port,
                            const std::vector<Tag>& tags)
{
    uint8 hash[16];
    std::memset(hash, hashPattern, sizeof(hash));

    SafeMemFile data;
    data.writeHash16(hash);
    data.writeUInt32(userId);
    data.writeUInt16(port);
    data.writeUInt32(static_cast<uint32>(tags.size()));
    for (const auto& tag : tags)
        tag.writeTagToFile(data);
    data.writeUInt32(0);    // server IP
    data.writeUInt16(0);    // server port
    return data.buffer();
}

/// The MISCOPTIONS1/2 bits a modern eMule advertises, mirroring the values
/// UpDownClient::sendHelloTypePacket writes (UpDownClient.cpp:1181-1218).
constexpr uint32 kMiscOptions1 =
    (uint32(1) << 29) | (uint32(1) << 28) | (uint32(4) << 24) | (uint32(1) << 20)
    | (uint32(4) << 12)   // source exchange 1 version
    | (uint32(2) <<  8) | (uint32(1) << 4) | (uint32(1) << 1);
/// CT_EMULE_VERSION, packed as UpDownClient::sendHelloTypePacket does — its presence is
/// what makes processHelloAnswer() report the peer as an eMule rather than a plain client.
constexpr uint32 kEmuleVersionTag =
    (uint32(SEND_EMULE_VERSION_MJR) << 17) | (uint32(SEND_EMULE_VERSION_MIN) << 10)
    | (uint32(SEND_EMULE_VERSION_UPD) << 7);
constexpr uint32 kMiscOptions2 =
    (uint32(1) <<  4)     // large files
    | (uint32(1) <<  5)   // ext multipacket
    | (uint32(1) << 10)   // source exchange 2
    | (uint32(1) << 13);  // file identifiers

} // namespace

class tst_SourceExchangeCompat : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    // -- A. Peers WITHOUT MODMISC_EXTXS: the classic bytes must not have moved --
    void classicRecordIsByteExact_data();
    void classicRecordIsByteExact();
    void sx1AnswerIsByteExact();
    void legacyReaderAcceptsOurClassicAnswer_data();
    void legacyReaderAcceptsOurClassicAnswer();
    void extSxCapabilityDoesNotLeakIntoClassicAnswer();
    void legacyReaderRejectsExtSxPayload();
    void nonExtSxPeerSendingTagBlock_isRejected();

    // -- B. Peers WITH MODMISC_EXTXS --
    void extSxRecordMatchesSpecWorkedExample();
    void intolerantReaderSurvivesOurAnswerForNonSkipTagsPeer();
    void intolerantReaderWouldLoseEverything_ifTagsWereUngated();
    void serverTagsAlwaysPaired();
    void serverIpWithLeadingZeroBytes_survivesSizeOptimisation();
    void ipv6TagOnlyForGlobalUnicast_data();
    void ipv6TagOnlyForGlobalUnicast();
    void extSxAnswerZlibRoundTrip();
    void addressLessSourceKeptOnExtSxPath();
    void requestedVersionIgnoredForExtSxPeer_data();
    void requestedVersionIgnoredForExtSxPeer();

    // -- C. Hostile / malformed input --
    void truncatedTagBlock_isCaughtNotOverread();
    void tagCountLargerThanTagsPresent_isCaught();
    void declaredCountExceedsRecordsPresent_isCaught();
    void oversizedStringTag_isSkippedWithoutDesync();
    void nonHashIpv6Tag_isIgnored();
    void nonPublicIpv6Tag_isIgnored_data();
    void nonPublicIpv6Tag_isIgnored();
    void familiesJudgedIndependently_data();
    void familiesJudgedIndependently();
    void extSxCapablePeerSendingClassicV1_doesNotCorrupt();
    void zeroSourceCount_isNoOp();

    // -- D. Negotiation: which version byte we ask for --
    void standaloneRequest_versionByteMatchesPeerCapability_data();
    void standaloneRequest_versionByteMatchesPeerCapability();
    void multipacketRequest_versionByteMatchesPeerCapability_data();
    void multipacketRequest_versionByteMatchesPeerCapability();
    void helloAdvertisesExtSxBits_roundTrip();
    void helloWithoutModMiscOptions_getsClassicAnswer();

    // -- E. The source request carried inside a multipacket --
    void multipacketExt2_bundledSourceRequest_isAnswered_data();
    void multipacketExt2_bundledSourceRequest_isAnswered();
    void multipacketLegacy_bundledSourceRequest_isAnswered();
    void multipacket_sourceRequestDoesNotDesyncFollowingSubOpcodes();
    void multipacket_sourceAnswerIsSeparatePacket();
    void secondSourceRequestWithinIntervalIsRefused();

    // -- F. The compressed transport, end to end --
    void packedExtSxAnswerFromPeerIsParsed();
    void packedClassicAnswerFromPeerIsParsed();

private:
    /// Stand up a download queue holding one PartFile with `hash`, run `body` through the
    /// socket of `peer` as a fully framed packet, and hand the PartFile to `check`.
    static void deliverAnswerInto(const uint8* hash, UpDownClient* peer, uint8 protocol,
                                  uint8 opcode, const QByteArray& wire,
                                  const std::function<void(PartFile&)>& check);

    std::unique_ptr<QTemporaryDir> m_tmpDir;
    std::vector<UpDownClient*> m_clients;

    UpDownClient* track(UpDownClient* c)
    {
        m_clients.push_back(c);
        return c;
    }

    /// A KnownFile with a deterministic hash and the given uploading clients.
    static std::unique_ptr<KnownFile> makeFile(const std::vector<UpDownClient*>& srcs)
    {
        auto f = std::make_unique<KnownFile>();
        uint8 hash[16];
        std::memset(hash, 0x11, sizeof(hash));
        f->setFileHash(hash);
        for (auto* c : srcs)
            f->addUploadingClient(c);
        return f;
    }

    /// Run a body through PartFile::addClientSources with a live download queue, and
    /// hand the resulting PartFile to `check` before tearing the queue back down.
    static void parseInto(const QByteArray& body, uint8 clientSXVersion, bool isSX2,
                          const UpDownClient* sender,
                          const std::function<void(PartFile&)>& check);
};

void tst_SourceExchangeCompat::initTestCase()
{
    // sendHelloTypePacket and the request builders read prefs; give them a scratch config
    // rather than whatever is in $HOME.
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());
    thePrefs.setConfigDir(m_tmpDir->path());
}

void tst_SourceExchangeCompat::cleanup()
{
    qDeleteAll(m_clients);
    m_clients.clear();
}

void tst_SourceExchangeCompat::parseInto(const QByteArray& body, uint8 clientSXVersion,
                                         bool isSX2, const UpDownClient* sender,
                                         const std::function<void(PartFile&)>& check)
{
    DownloadQueue queue;
    theApp.downloadQueue = &queue;

    auto* pf = new PartFile;
    uint8 hash[16];
    std::memset(hash, 0x55, sizeof(hash));
    pf->setFileHash(hash);
    queue.addDownload(pf);

    SafeMemFile io(reinterpret_cast<const uint8*>(body.constData()), body.size());
    pf->addClientSources(io, clientSXVersion, isSX2, sender);

    check(*pf);

    theApp.downloadQueue = nullptr;
    queue.deleteAll();
}

void tst_SourceExchangeCompat::deliverAnswerInto(const uint8* hash, UpDownClient* peer,
                                                 uint8 protocol, uint8 opcode,
                                                 const QByteArray& wire,
                                                 const std::function<void(PartFile&)>& check)
{
    DownloadQueue queue;
    theApp.downloadQueue = &queue;

    auto* pf = new PartFile;
    pf->setFileHash(hash);
    queue.addDownload(pf);

    RecordingSocket sock;
    peer->wireIncomingSocket(&sock);
    const bool kept = sock.deliverWire(protocol, opcode, wire);
    peer->setSocket(nullptr);

    QVERIFY(kept);
    check(*pf);

    theApp.downloadQueue = nullptr;
    queue.deleteAll();
}

// ===========================================================================
// A. Peers WITHOUT MODMISC_EXTXS — the classic bytes must not have moved
// ===========================================================================

void tst_SourceExchangeCompat::classicRecordIsByteExact_data()
{
    QTest::addColumn<uint8>("version");
    QTest::newRow("v1") << uint8(1);
    QTest::newRow("v2") << uint8(2);
    QTest::newRow("v3") << uint8(3);
    QTest::newRow("v4") << uint8(4);
}

// The golden test for every peer that never heard of ExtSX. The expected buffer is
// assembled here from the format description, not from our own writer, so a change in
// KnownFile::buildSrcInfoPacket that happens to be self-consistent still fails.
void tst_SourceExchangeCompat::classicRecordIsByteExact()
{
    QFETCH(uint8, version);

    auto* a = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    auto* b = track(makeHighIdClient(QStringLiteral("61.71.81.91"), 4663, 0xB2));
    a->setConnectOptions(0x03, true, false);    // supports + requests crypt
    b->setConnectOptions(0x00, true, false);

    auto file = makeFile({ a, b });
    auto* peer = track(makeRequester(/*extSX*/ false));

    auto packet = file->createSrcInfoPacket(peer, version, 0);
    QVERIFY(packet != nullptr);
    QCOMPARE(packet->opcode, uint8(OP_ANSWERSOURCES2));
    QCOMPARE(packet->prot, uint8(OP_EMULEPROT));

    uint8 fileHash[16];
    std::memset(fileHash, 0x11, sizeof(fileHash));

    QByteArray expected;
    expected.append(char(version));
    expected.append(reinterpret_cast<const char*>(fileHash), 16);
    appendU16(expected, 2);

    const auto appendRecord = [&](const UpDownClient* c, uint8 crypt) {
        // v3+ carries the hybrid (host-order) ID; below that, the address in network order.
        appendU32(expected, version >= 3 ? c->userIDHybrid()
                                         : c->userAddress().toNetworkUint32());
        appendU16(expected, c->userPort());
        appendU32(expected, c->serverAddress().toNetworkUint32());   // always network order
        appendU16(expected, c->serverPort());
        if (version >= 2)
            expected.append(reinterpret_cast<const char*>(c->userHash()), 16);
        if (version >= 4)
            expected.append(char(crypt));
    };
    appendRecord(a, 0x03);
    appendRecord(b, 0x00);

    QCOMPARE(QByteArray(packet->pBuffer, static_cast<int>(packet->size)), expected);
    // And the invariant the receiving side actually checks.
    QCOMPARE(qint64(packet->size) - kSx2HeaderSize - 2, qint64(2) * classicRecordSize(version));
}

// SX1 is a different dialect, not a variant: no version byte, and its own opcode.
// Mixing the two is how a peer ends up slicing records at the wrong offset.
void tst_SourceExchangeCompat::sx1AnswerIsByteExact()
{
    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));

    auto file = makeFile({ src });
    auto* peer = track(makeRequester(/*extSX*/ false, /*skipTags*/ false, /*supportsSX2*/ false));
    peer->setSourceExchange1Ver(2);

    // version 0 == "the peer did not ask over SX2", so the SX1 branch is taken.
    auto packet = file->createSrcInfoPacket(peer, 0, 0);
    QVERIFY(packet != nullptr);
    QCOMPARE(packet->opcode, uint8(OP_ANSWERSOURCES));

    uint8 fileHash[16];
    std::memset(fileHash, 0x11, sizeof(fileHash));

    QByteArray expected;                                  // no version byte
    expected.append(reinterpret_cast<const char*>(fileHash), 16);
    appendU16(expected, 1);
    appendU32(expected, src->userAddress().toNetworkUint32());
    appendU16(expected, src->userPort());
    appendU32(expected, src->serverAddress().toNetworkUint32());
    appendU16(expected, src->serverPort());
    expected.append(reinterpret_cast<const char*>(src->userHash()), 16);   // v2 tail

    QCOMPARE(QByteArray(packet->pBuffer, static_cast<int>(packet->size)), expected);
}

void tst_SourceExchangeCompat::legacyReaderAcceptsOurClassicAnswer_data()
{
    QTest::addColumn<uint8>("version");
    QTest::newRow("v1") << uint8(1);
    QTest::newRow("v2") << uint8(2);
    QTest::newRow("v3") << uint8(3);
    QTest::newRow("v4") << uint8(4);
}

// The real compatibility question is not "does our writer agree with our reader" but
// "does stock eMule accept this", so the answer goes through a re-implementation of its
// parser. A single stray byte anywhere fails its count*recordSize check and costs it
// every source in the packet.
void tst_SourceExchangeCompat::legacyReaderAcceptsOurClassicAnswer()
{
    QFETCH(uint8, version);

    auto* a = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    auto* b = track(makeHighIdClient(QStringLiteral("61.71.81.91"), 4663, 0xB2));
    a->setConnectOptions(0x05, true, false);   // supports + requires crypt

    auto file = makeFile({ a, b });
    auto* peer = track(makeRequester(/*extSX*/ false));

    auto packet = file->createSrcInfoPacket(peer, version, 0);
    QVERIFY(packet != nullptr);

    const LegacyParse parsed =
        legacyClassicReader(answerBody(*packet, /*isSX2*/ true), version, /*isSX2*/ true);
    QVERIFY(parsed.accepted);
    QCOMPARE(parsed.layoutVersion, version);
    QCOMPARE(int(parsed.sources.size()), 2);

    const auto& first = parsed.sources.front();
    QCOMPARE(first.clientId, version >= 3 ? a->userIDHybrid()
                                          : a->userAddress().toNetworkUint32());
    QCOMPARE(first.tcpPort, uint16(4662));
    QCOMPARE(first.serverIp, Address::fromString(QStringLiteral("1.2.3.4")).toNetworkUint32());
    QCOMPARE(first.serverPort, uint16(4661));
    QCOMPARE(first.hasUserHash, version >= 2);
    if (version >= 2)
        QCOMPARE(std::memcmp(first.userHash.data(), a->userHash(), 16), 0);
    QCOMPARE(first.hasCryptOptions, version >= 4);
    if (version >= 4)
        QCOMPARE(first.cryptOptions, uint8(0x05));
}

// A source can be fully ExtSX-equipped — public IPv6, user hash, crypt options — without
// any of that reaching a peer that did not advertise MODMISC_EXTXS. The proof is that the
// answer is identical to the one built for the same source with the IPv6 removed.
void tst_SourceExchangeCompat::extSxCapabilityDoesNotLeakIntoClassicAnswer()
{
    const auto build = [&](bool withIPv6) {
        auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
        src->setConnectOptions(0x03, true, false);
        if (withIPv6) {
            src->setUserIPv6(Address::fromString(QStringLiteral("2a01:4f8::1")));
            src->setOpenIPv6(true);
        }
        auto file = makeFile({ src });
        auto* peer = track(makeRequester(/*extSX*/ false, /*skipTags*/ true));   // bit 5 alone
        auto packet = file->createSrcInfoPacket(peer, 4, 0);
        Q_ASSERT(packet != nullptr);
        return QByteArray(packet->pBuffer, static_cast<int>(packet->size));
    };

    QCOMPARE(build(true), build(false));
    // ...and the result is still a well-formed v4 answer.
    QCOMPARE(build(true).size(), kSx2HeaderSize + 2 + classicRecordSize(4));
}

// If ExtSX bytes ever reached a peer that cannot read them, the failure has to be a clean
// rejection, not a mis-slice: reading a variable-length tag block as fixed 12-byte records
// would manufacture sources out of tag headers.
void tst_SourceExchangeCompat::legacyReaderRejectsExtSxPayload()
{
    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    src->setUserIPv6(Address::fromString(QStringLiteral("2a01:4f8::1")));
    src->setOpenIPv6(true);

    auto file = makeFile({ src });
    auto* ext = track(makeRequester(/*extSX*/ true));

    auto packet = file->createSrcInfoPacket(ext, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);

    const QByteArray body = answerBody(*packet, /*isSX2*/ true);
    // ExtSX pins the version byte at 1, so a legacy reader would try the 12-byte v1 layout.
    QVERIFY(body.size() - 2 != classicRecordSize(1));   // no accidental length collision
    const LegacyParse parsed = legacyClassicReader(body, SOURCEEXCHANGEEXT_VERSION, true);
    QVERIFY(!parsed.accepted);
    QVERIFY(parsed.sources.empty());
}

// The mirror image on our side: a peer that never advertised MODMISC_EXTXS does not get
// to send us tag blocks, whatever it puts in the version byte.
void tst_SourceExchangeCompat::nonExtSxPeerSendingTagBlock_isRejected()
{
    auto* sender = track(makeRequester(/*extSX*/ false));

    SafeMemFile w;
    w.writeUInt16(1);
    w.writeUInt32(Address::fromString(QStringLiteral("50.60.70.80")).toNetworkUint32());
    w.writeUInt16(4662);
    {
        std::vector<Tag> tags;
        tags.emplace_back(CT_MOD_IP_V6,
                          Address::fromString(QStringLiteral("2a01:4f8::2")).ipv6Bytes().data());
        w.writeUInt8(uint8(tags.size()));
        for (const auto& t : tags)
            t.writeNewEd2kTag(w);
    }

    parseInto(w.buffer(), SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, sender,
              [](PartFile& pf) { QCOMPARE(pf.sourceCount(), 0); });
}

// ===========================================================================
// B. Peers WITH MODMISC_EXTXS
// ===========================================================================

// The worked example from docs/protocol/ipv6-spec.md:407-415, byte for byte. This pins
// the tag order, the `type|0x80` numeric-name form, and the size-optimised integer widths
// all at once — the three things a foreign reader is most likely to disagree about.
void tst_SourceExchangeCompat::extSxRecordMatchesSpecWorkedExample()
{
    // The spec's "hybrid ID 0x0100000A" is that value as it appears on the wire, i.e. the
    // network-order form of 10.0.0.1; the hybrid ID this client actually holds is the
    // host-order 0x0A000001, and htonl() of it is what gets written.
    auto* src = track(makeHighIdClient(QStringLiteral("10.0.0.1"), 4662, 0xA1));
    QCOMPARE(src->userIDHybrid(), 0x0A000001u);
    src->setServerAddress(Address::fromString(QStringLiteral("1.2.3.4")));
    src->setServerPort(4661);
    src->setUserIPv6(Address::fromString(QStringLiteral("2a01:4f8::1")));
    src->setOpenIPv6(true);

    auto file = makeFile({ src });
    auto* peer = track(makeRequester(/*extSX*/ true, /*skipTags*/ false));

    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);
    QCOMPARE(packet->opcode, uint8(OP_ANSWERSOURCES2));
    QCOMPARE(uint8(packet->pBuffer[0]), uint8(SOURCEEXCHANGEEXT_VERSION));

    QByteArray expected;
    appendU16(expected, 1);                              // one source
    expected.append("\x0a\x00\x00\x01", 4);              // clientID = htonl(hybrid)
    expected.append("\x36\x12", 2);                      // tcpPort  = 4662
    expected.append(char(3));                            // tagCount
    expected.append("\x83\xba\x01\x02\x03\x04", 6);      // CT_EMULE_SERVERIP,  UINT32
    expected.append("\x88\xbb\x35\x12", 4);              // CT_EMULE_SERVERTCP, UINT16
    expected.append("\x81\xae", 2);                      // CT_MOD_IP_V6,       HASH16
    expected.append(reinterpret_cast<const char*>(
                        Address::fromString(QStringLiteral("2a01:4f8::1")).ipv6Bytes().data()), 16);

    QCOMPARE(answerBody(*packet, /*isSX2*/ true), expected);
}

// The reason MODMISC_EXTXS_SKIPTAGS exists. A peer that has not promised to skip unknown
// tags must receive only tags that predate the extension, so a reader which aborts on the
// first thing it does not recognise still gets every source.
void tst_SourceExchangeCompat::intolerantReaderSurvivesOurAnswerForNonSkipTagsPeer()
{
    auto* a = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    a->setUserIPv6(Address::fromString(QStringLiteral("2a01:4f8::1")));
    a->setOpenIPv6(true);
    a->setConnectOptions(0x03, true, false);
    auto* b = track(makeHighIdClient(QStringLiteral("61.71.81.91"), 4663, 0xB2));
    b->setConnectOptions(0x01, true, false);

    auto file = makeFile({ a, b });
    auto* peer = track(makeRequester(/*extSX*/ true, /*skipTags*/ false));

    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);
    const QByteArray body = answerBody(*packet, /*isSX2*/ true);

    const auto recovered = intolerantExtSxReader(body);
    QCOMPARE(int(recovered.size()), 2);
    QCOMPARE(recovered[0].tcpPort, uint16(4662));
    QCOMPARE(recovered[0].ipv6.toString(), QStringLiteral("2a01:4f8::1"));
    QCOMPARE(recovered[1].tcpPort, uint16(4663));

    // Stated as a set membership too, so the failure message names the offending tag.
    for (const auto& ids : extSxTagIds(body))
        for (uint8 id : ids)
            QVERIFY2(id == CT_EMULE_SERVERIP || id == CT_EMULE_SERVERTCP || id == CT_MOD_IP_V6,
                     qPrintable(QStringLiteral("tag 0x%1 sent to a peer without "
                                               "MODMISC_EXTXS_SKIPTAGS").arg(id, 2, 16,
                                                                             QLatin1Char('0'))));
}

// The negative control for the test above: without the gate, the same reader loses the
// whole answer. If this ever passes, the previous test has stopped proving anything.
void tst_SourceExchangeCompat::intolerantReaderWouldLoseEverything_ifTagsWereUngated()
{
    auto* a = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    a->setUserIPv6(Address::fromString(QStringLiteral("2a01:4f8::1")));
    a->setOpenIPv6(true);
    a->setConnectOptions(0x03, true, false);
    auto* b = track(makeHighIdClient(QStringLiteral("61.71.81.91"), 4663, 0xB2));

    auto file = makeFile({ a, b });
    auto* tolerant = track(makeRequester(/*extSX*/ true, /*skipTags*/ true));

    auto packet = file->createSrcInfoPacket(tolerant, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);
    const QByteArray body = answerBody(*packet, /*isSX2*/ true);

    // CT_EMULE_USERHASH is there, so the intolerant reader bails on record 1 and returns
    // nothing at all — losing source 2 as well, which it had already parsed.
    QVERIFY(intolerantExtSxReader(body).empty());

    // Our own reader, which conforms, keeps both and recovers the extra fields.
    parseInto(body, SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, tolerant, [&](PartFile& pf) {
        QCOMPARE(pf.sourceCount(), 2);
        for (auto* s : pf.srcList())
            QVERIFY(s->hasValidHash());
    });
}

// The server tags are a pair: a serverIP with no serverPort is a source nobody can use
// for a callback, so either both are emitted or neither is.
void tst_SourceExchangeCompat::serverTagsAlwaysPaired()
{
    auto* withServer = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    auto* noServer = track(makeHighIdClient(QStringLiteral("61.71.81.91"), 4663, 0xB2));
    noServer->setServerAddress(Address{});
    noServer->setServerPort(0);

    auto file = makeFile({ withServer, noServer });
    auto* peer = track(makeRequester(/*extSX*/ true));

    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);

    const auto tags = extSxTagIds(answerBody(*packet, /*isSX2*/ true));
    QCOMPARE(int(tags.size()), 2);
    QCOMPARE(tags[0], (std::vector<uint8>{ CT_EMULE_SERVERIP, CT_EMULE_SERVERTCP }));
    QVERIFY(tags[1].empty());
}

// New-format integer tags are size-optimised, so a network-order server IP whose high
// bytes are zero is written in fewer than four bytes. The value has to survive that.
void tst_SourceExchangeCompat::serverIpWithLeadingZeroBytes_survivesSizeOptimisation()
{
    // 1.0.0.0 in network order is 0x00000001 — one significant byte.
    const Address server = Address::fromString(QStringLiteral("1.0.0.0"));
    QCOMPARE(server.toNetworkUint32(), 1u);

    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    src->setServerAddress(server);
    src->setServerPort(4661);

    auto file = makeFile({ src });
    auto* peer = track(makeRequester(/*extSX*/ true));

    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);
    const QByteArray body = answerBody(*packet, /*isSX2*/ true);

    const auto recovered = intolerantExtSxReader(body);
    QCOMPARE(int(recovered.size()), 1);
    QCOMPARE(recovered[0].serverIp, server.toNetworkUint32());
    QCOMPARE(recovered[0].serverPort, uint16(4661));

    parseInto(body, SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, peer, [&](PartFile& pf) {
        QCOMPARE(pf.sourceCount(), 1);
        QCOMPARE(pf.srcList().front()->serverAddress().toString(), server.toString());
    });
}

void tst_SourceExchangeCompat::ipv6TagOnlyForGlobalUnicast_data()
{
    QTest::addColumn<QString>("address");
    QTest::addColumn<bool>("emitted");

    QTest::newRow("global unicast")   << QStringLiteral("2a01:4f8::1")   << true;
    QTest::newRow("link local")       << QStringLiteral("fe80::1")       << false;
    QTest::newRow("unique local")     << QStringLiteral("fd00::1")       << false;
    QTest::newRow("documentation")    << QStringLiteral("2001:db8::1")   << false;
    QTest::newRow("loopback")         << QStringLiteral("::1")           << false;
    QTest::newRow("multicast")        << QStringLiteral("ff02::1")       << false;
    QTest::newRow("mapped v4")        << QStringLiteral("::ffff:1.2.3.4") << false;
    QTest::newRow("6to4")             << QStringLiteral("2002:102:304::1") << false;
    QTest::newRow("teredo")           << QStringLiteral("2001:0::1")      << false;
}

// A source's IPv6 is validated as globally routable before it is published, so we never
// hand a peer an address it cannot dial — and never leak a private one out of the LAN.
void tst_SourceExchangeCompat::ipv6TagOnlyForGlobalUnicast()
{
    QFETCH(QString, address);
    QFETCH(bool, emitted);

    // Built from raw bytes, as the wire does: Address::fromString folds ::ffff:a.b.c.d
    // down to a plain IPv4, which is not the value a CT_MOD_IP_V6 tag would carry.
    const QHostAddress parsed(address);
    const Q_IPV6ADDR raw = parsed.toIPv6Address();

    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    src->setUserIPv6(Address::fromIPv6Bytes(raw.c));
    src->setOpenIPv6(true);

    auto file = makeFile({ src });
    auto* peer = track(makeRequester(/*extSX*/ true));

    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);

    const auto tags = extSxTagIds(answerBody(*packet, /*isSX2*/ true));
    QCOMPARE(int(tags.size()), 1);
    const bool hasV6 = std::find(tags[0].begin(), tags[0].end(), uint8(CT_MOD_IP_V6))
                       != tags[0].end();
    QCOMPARE(hasV6, emitted);
}

// Past 354 bytes the answer is zlib-packed, which is where a length assumption baked into
// the tag-block reader would surface.
void tst_SourceExchangeCompat::extSxAnswerZlibRoundTrip()
{
    constexpr int kSources = 40;   // well past the 354-byte pack threshold

    std::vector<UpDownClient*> srcs;
    srcs.reserve(kSources);
    for (int i = 0; i < kSources; ++i) {
        auto* c = track(makeHighIdClient(QStringLiteral("60.%1.%2.90").arg(i / 250).arg(i % 250 + 1),
                                         static_cast<uint16>(4662 + i), uint8(0x40 + i)));
        c->setUserIPv6(Address::fromString(QStringLiteral("2a01:4f8::%1").arg(i + 1, 0, 16)));
        c->setOpenIPv6(true);
        srcs.push_back(c);
    }

    auto file = makeFile(srcs);
    auto* peer = track(makeRequester(/*extSX*/ true, /*skipTags*/ true));

    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);
    QCOMPARE(packet->prot, uint8(OP_PACKEDPROT));
    QVERIFY(packet->unPackPacket());
    QCOMPARE(uint8(packet->pBuffer[0]), uint8(SOURCEEXCHANGEEXT_VERSION));

    parseInto(answerBody(*packet, /*isSX2*/ true), SOURCEEXCHANGEEXT_VERSION, true, peer,
              [&](PartFile& pf) {
        QCOMPARE(pf.sourceCount(), kSources);
        for (auto* s : pf.srcList()) {
            QVERIFY(s->openIPv6());
            QVERIFY(s->userIPv6().isPublicIP());
        }
    });
}

// A source with no usable IPv4 at all has nothing to put in the classic record, so it was
// skipped outright. On the ExtSX path the tag block stands on its own.
void tst_SourceExchangeCompat::addressLessSourceKeptOnExtSxPath()
{
    auto* v6only = new UpDownClient;
    track(v6only);
    v6only->setUserPort(4662);
    v6only->setUserIDHybrid(0xFFFFFFFFu);   // the reference's IPv6-only sentinel
    v6only->setUserIPv6(Address::fromString(QStringLiteral("2a01:4f8::abcd")));
    v6only->setOpenIPv6(true);
    v6only->setUploadState(UploadState::Uploading);
    QVERIFY(v6only->userAddress().isNull());

    auto file = makeFile({ v6only });

    // A classic peer cannot be told about it at all.
    auto* classic = track(makeRequester(/*extSX*/ false));
    QVERIFY(file->createSrcInfoPacket(classic, 4, 0) == nullptr);

    // An ExtSX peer gets it, and reads it back as a LowID client so nothing dials
    // 255.255.255.255.
    auto* ext = track(makeRequester(/*extSX*/ true));
    auto packet = file->createSrcInfoPacket(ext, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);

    parseInto(answerBody(*packet, /*isSX2*/ true), SOURCEEXCHANGEEXT_VERSION, true, ext,
              [&](PartFile& pf) {
        QCOMPARE(pf.sourceCount(), 1);
        auto* learned = pf.srcList().front();
        QVERIFY(learned->hasLowID());
        QCOMPARE(learned->userIPv6().toString(), QStringLiteral("2a01:4f8::abcd"));
    });
}

void tst_SourceExchangeCompat::requestedVersionIgnoredForExtSxPeer_data()
{
    QTest::addColumn<uint8>("requested");
    QTest::newRow("asked for 1") << uint8(1);
    QTest::newRow("asked for 2") << uint8(2);
    QTest::newRow("asked for 3") << uint8(3);
    QTest::newRow("asked for 4") << uint8(4);
}

// The capability bit decides the dialect, not the requested version — deliberately, since
// both ends gate on the same bit. A peer that advertised MODMISC_EXTXS and then asked for
// v4 still gets a version-1 tag block, and its own reader will expect exactly that.
void tst_SourceExchangeCompat::requestedVersionIgnoredForExtSxPeer()
{
    QFETCH(uint8, requested);

    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    auto file = makeFile({ src });
    auto* peer = track(makeRequester(/*extSX*/ true));

    auto packet = file->createSrcInfoPacket(peer, requested, 0);
    QVERIFY(packet != nullptr);
    QCOMPARE(uint8(packet->pBuffer[0]), uint8(SOURCEEXCHANGEEXT_VERSION));
    // Tag block, not a fixed record: two server tags follow the 7-byte record head.
    QCOMPARE(uint8(packet->pBuffer[kSx2HeaderSize + 2 + 4 + 2]), uint8(2));

    // version 0 means "not asked over SX2 at all", which is still the SX1 fallback.
    auto sx1 = file->createSrcInfoPacket(peer, 0, 0);
    QVERIFY(sx1 != nullptr);
    QCOMPARE(sx1->opcode, uint8(OP_ANSWERSOURCES));
}

// ===========================================================================
// C. Hostile / malformed input
// ===========================================================================

// ExtSX records have no fixed size, so the classic count*recordSize pre-check cannot run
// and every read has to be bounds-checked. A packet cut mid-tag must be abandoned, not
// over-read.
void tst_SourceExchangeCompat::truncatedTagBlock_isCaughtNotOverread()
{
    auto* sender = track(makeRequester(/*extSX*/ true));

    SafeMemFile w;
    w.writeUInt16(2);
    w.writeUInt32(Address::fromString(QStringLiteral("50.60.70.80")).toNetworkUint32());
    w.writeUInt16(4662);
    {
        std::vector<Tag> tags;
        tags.emplace_back(CT_MOD_IP_V6,
                          Address::fromString(QStringLiteral("2a01:4f8::2")).ipv6Bytes().data());
        w.writeUInt8(uint8(tags.size()));
        for (const auto& t : tags)
            t.writeNewEd2kTag(w);
    }
    w.writeUInt32(Address::fromString(QStringLiteral("51.61.71.81")).toNetworkUint32());
    w.writeUInt16(4663);
    w.writeUInt8(1);
    w.writeUInt8(0x81);   // a HASH16 tag header, and then the packet simply ends
    w.writeUInt8(CT_MOD_IP_V6);

    parseInto(w.buffer(), SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, sender, [](PartFile& pf) {
        // The complete first record is kept; the truncated second is dropped.
        QCOMPARE(pf.sourceCount(), 1);
        QCOMPARE(pf.srcList().front()->userIPv6().toString(), QStringLiteral("2a01:4f8::2"));
    });
}

void tst_SourceExchangeCompat::tagCountLargerThanTagsPresent_isCaught()
{
    auto* sender = track(makeRequester(/*extSX*/ true));

    SafeMemFile w;
    w.writeUInt16(1);
    w.writeUInt32(Address::fromString(QStringLiteral("50.60.70.80")).toNetworkUint32());
    w.writeUInt16(4662);
    w.writeUInt8(255);   // claims 255 tags, supplies one
    Tag(CT_EMULE_SERVERTCP, uint32(4661)).writeNewEd2kTag(w);

    parseInto(w.buffer(), SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, sender,
              [](PartFile& pf) { QCOMPARE(pf.sourceCount(), 0); });
}

void tst_SourceExchangeCompat::declaredCountExceedsRecordsPresent_isCaught()
{
    auto* sender = track(makeRequester(/*extSX*/ true));

    SafeMemFile w;
    w.writeUInt16(5);   // five promised
    for (int i = 0; i < 2; ++i) {   // two delivered
        w.writeUInt32(Address::fromString(QStringLiteral("50.60.70.%1").arg(80 + i))
                          .toNetworkUint32());
        w.writeUInt16(static_cast<uint16>(4662 + i));
        w.writeUInt8(0);
    }

    parseInto(w.buffer(), SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, sender, [](PartFile& pf) {
        // Whatever arrived intact is kept; the phantom three cannot be invented.
        QCOMPARE(pf.sourceCount(), 2);
    });
}

// Skipping an unknown tag means skipping it *by type*, so a long string in the middle of a
// record must not shift the record that follows.
void tst_SourceExchangeCompat::oversizedStringTag_isSkippedWithoutDesync()
{
    auto* sender = track(makeRequester(/*extSX*/ true));

    SafeMemFile w;
    w.writeUInt16(2);

    w.writeUInt32(Address::fromString(QStringLiteral("50.60.70.80")).toNetworkUint32());
    w.writeUInt16(4662);
    {
        std::vector<Tag> tags;
        tags.emplace_back(uint8(0x79), QString(600, QLatin1Char('x')));   // unknown, long
        tags.emplace_back(CT_MOD_IP_V6,
                          Address::fromString(QStringLiteral("2a01:4f8::2")).ipv6Bytes().data());
        w.writeUInt8(uint8(tags.size()));
        for (const auto& t : tags)
            t.writeNewEd2kTag(w);
    }

    w.writeUInt32(Address::fromString(QStringLiteral("51.61.71.81")).toNetworkUint32());
    w.writeUInt16(4663);
    {
        std::vector<Tag> tags;
        tags.emplace_back(CT_MOD_IP_V6,
                          Address::fromString(QStringLiteral("2a01:4f8::3")).ipv6Bytes().data());
        w.writeUInt8(uint8(tags.size()));
        for (const auto& t : tags)
            t.writeNewEd2kTag(w);
    }

    parseInto(w.buffer(), SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, sender, [](PartFile& pf) {
        QCOMPARE(pf.sourceCount(), 2);
        QStringList v6s;
        for (auto* s : pf.srcList())
            v6s << s->userIPv6().toString();
        v6s.sort();
        QCOMPARE(v6s, (QStringList{ QStringLiteral("2a01:4f8::2"),
                                    QStringLiteral("2a01:4f8::3") }));
    });
}

// A tag is identified by name *and* type. CT_MOD_IP_V6 that is not a 16-byte hash is not
// an address, and reinterpreting whatever it holds would be how a peer gets us to dial
// something of its choosing.
void tst_SourceExchangeCompat::nonHashIpv6Tag_isIgnored()
{
    auto* sender = track(makeRequester(/*extSX*/ true));

    SafeMemFile w;
    w.writeUInt16(1);
    w.writeUInt32(Address::fromString(QStringLiteral("50.60.70.80")).toNetworkUint32());
    w.writeUInt16(4662);
    {
        std::vector<Tag> tags;
        tags.emplace_back(CT_MOD_IP_V6, uint32(0xDEADBEEF));   // right name, wrong type
        w.writeUInt8(uint8(tags.size()));
        for (const auto& t : tags)
            t.writeNewEd2kTag(w);
    }

    parseInto(w.buffer(), SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, sender, [](PartFile& pf) {
        QCOMPARE(pf.sourceCount(), 1);        // the v4 source is fine
        auto* s = pf.srcList().front();
        QVERIFY(!s->openIPv6());
        QVERIFY(s->userIPv6().isNull());
    });
}

void tst_SourceExchangeCompat::nonPublicIpv6Tag_isIgnored_data()
{
    QTest::addColumn<QString>("address");
    QTest::newRow("link local")    << QStringLiteral("fe80::1");
    QTest::newRow("unique local")  << QStringLiteral("fd00::1");
    QTest::newRow("documentation") << QStringLiteral("2001:db8::1");
    QTest::newRow("loopback")      << QStringLiteral("::1");
    QTest::newRow("multicast")     << QStringLiteral("ff02::1");
}

// The receive side re-validates rather than trusting the sender's validation, and it does
// so before the IPv6 can rescue an unusable IPv4 — otherwise a bogus v6 would resurrect
// records that should have been dropped.
void tst_SourceExchangeCompat::nonPublicIpv6Tag_isIgnored()
{
    QFETCH(QString, address);

    auto* sender = track(makeRequester(/*extSX*/ true));
    const auto v6 = Address::fromString(address);

    const auto oneRecord = [&](uint32 wireId) {
        SafeMemFile w;
        w.writeUInt16(1);
        w.writeUInt32(wireId);
        w.writeUInt16(4662);
        std::vector<Tag> tags;
        tags.emplace_back(CT_MOD_IP_V6, v6.ipv6Bytes().data());
        w.writeUInt8(uint8(tags.size()));
        for (const auto& t : tags)
            t.writeNewEd2kTag(w);
        return w.buffer();
    };

    // Usable IPv4 + unusable IPv6: kept, but with no IPv6.
    parseInto(oneRecord(htonl(Address::fromString(QStringLiteral("50.60.70.80")).toUint32())),
              SOURCEEXCHANGEEXT_VERSION, true, sender, [](PartFile& pf) {
        QCOMPARE(pf.sourceCount(), 1);
        QVERIFY(!pf.srcList().front()->openIPv6());
    });

    // No usable IPv4 either: neither family can reach it, so the record goes.
    parseInto(oneRecord(htonl(0xFFFFFFFFu)), SOURCEEXCHANGEEXT_VERSION, true, sender,
              [](PartFile& pf) { QCOMPARE(pf.sourceCount(), 0); });
}

void tst_SourceExchangeCompat::familiesJudgedIndependently_data()
{
    QTest::addColumn<bool>("v4Usable");
    QTest::addColumn<bool>("v6Usable");
    QTest::addColumn<int>("expectedSources");

    QTest::newRow("v4 only")   << true  << false << 1;
    QTest::newRow("v6 only")   << false << true  << 1;
    QTest::newRow("both")      << true  << true  << 1;
    QTest::newRow("neither")   << false << false << 0;
}

// A source is two possible routes, not one. Banning it on one family must not throw away
// the other; only a source unreachable on both is worth discarding.
void tst_SourceExchangeCompat::familiesJudgedIndependently()
{
    QFETCH(bool, v4Usable);
    QFETCH(bool, v6Usable);
    QFETCH(int, expectedSources);

    const Address v4 = Address::fromString(QStringLiteral("50.60.70.80"));
    const Address v6 = Address::fromString(QStringLiteral("2a01:4f8::5"));

    ClientList clientList;
    theApp.clientList = &clientList;
    if (!v4Usable)
        clientList.addBannedClient(v4);
    if (!v6Usable)
        clientList.addBannedClient(v6);

    auto* sender = track(makeRequester(/*extSX*/ true));

    SafeMemFile w;
    w.writeUInt16(1);
    w.writeUInt32(v4.toNetworkUint32());
    w.writeUInt16(4662);
    {
        std::vector<Tag> tags;
        tags.emplace_back(CT_MOD_IP_V6, v6.ipv6Bytes().data());
        w.writeUInt8(uint8(tags.size()));
        for (const auto& t : tags)
            t.writeNewEd2kTag(w);
    }

    parseInto(w.buffer(), SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, sender, [&](PartFile& pf) {
        QCOMPARE(pf.sourceCount(), expectedSources);
        if (expectedSources == 1) {
            auto* s = pf.srcList().front();
            QCOMPARE(s->openIPv6(), v6Usable);
            if (!v4Usable)
                QVERIFY(s->hasLowID());   // no dialable v4 was kept
        }
    });

    theApp.clientList = nullptr;
}

// Pinning ExtSX at version 1 makes the dialect a function of the peer's capability bit,
// which leaves one genuinely ambiguous case: a peer that advertises MODMISC_EXTXS and then
// sends real classic v1 records. There is no byte that distinguishes them, so this is a
// characterisation test — the requirement is that it stays bounded, not that it succeeds.
void tst_SourceExchangeCompat::extSxCapablePeerSendingClassicV1_doesNotCorrupt()
{
    auto* sender = track(makeRequester(/*extSX*/ true));

    QByteArray body;
    appendU16(body, 2);
    for (int i = 0; i < 2; ++i) {
        appendU32(body, Address::fromString(QStringLiteral("50.60.70.%1").arg(80 + i))
                            .toNetworkUint32());
        appendU16(body, static_cast<uint16>(4662 + i));
        appendU32(body, Address::fromString(QStringLiteral("1.2.3.4")).toNetworkUint32());
        appendU16(body, 4661);
    }
    QCOMPARE(body.size() - 2, qint64(2) * classicRecordSize(1));

    parseInto(body, SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, sender, [](PartFile& pf) {
        // Whatever the tag walker makes of the serverIP bytes, nothing may be admitted
        // that the address checks would have rejected.
        for (auto* s : pf.srcList()) {
            QVERIFY(s->userIPv6().isNull() || s->userIPv6().isPublicIP());
            QVERIFY(s->hasLowID() || isGoodIP(s->userAddress().toNetworkUint32()));
        }
    });
}

void tst_SourceExchangeCompat::zeroSourceCount_isNoOp()
{
    auto* sender = track(makeRequester(/*extSX*/ true));

    QByteArray body;
    appendU16(body, 0);

    parseInto(body, SOURCEEXCHANGEEXT_VERSION, /*isSX2*/ true, sender,
              [](PartFile& pf) { QCOMPARE(pf.sourceCount(), 0); });
}

// ===========================================================================
// D. Negotiation — which version byte we ask for
// ===========================================================================

void tst_SourceExchangeCompat::standaloneRequest_versionByteMatchesPeerCapability_data()
{
    QTest::addColumn<bool>("supportsSX2");
    QTest::addColumn<bool>("extSX");
    QTest::addColumn<uint8>("expectedOpcode");
    QTest::addColumn<int>("expectedSize");
    QTest::addColumn<int>("expectedVersion");   // -1 == no version byte

    QTest::newRow("ExtSX peer")   << true  << true  << uint8(OP_REQUESTSOURCES2) << 19 << 1;
    QTest::newRow("SX2 peer")     << true  << false << uint8(OP_REQUESTSOURCES2) << 19 << 4;
    QTest::newRow("SX1 only")     << false << false << uint8(OP_REQUESTSOURCES)  << 16 << -1;
}

// Asking for version 1 is what tells an ExtSX peer to answer in tag blocks; asking a
// classic peer for version 1 would get us the 50-source v1 branch instead of 500. The
// version byte therefore has to track the capability bit exactly.
void tst_SourceExchangeCompat::standaloneRequest_versionByteMatchesPeerCapability()
{
    QFETCH(bool, supportsSX2);
    QFETCH(bool, extSX);
    QFETCH(uint8, expectedOpcode);
    QFETCH(int, expectedSize);
    QFETCH(int, expectedVersion);

    DownloadQueue queue;
    theApp.downloadQueue = &queue;

    auto* pf = new PartFile;
    uint8 hash[16];
    std::memset(hash, 0x33, sizeof(hash));
    pf->setFileHash(hash);
    pf->setFileName(QStringLiteral("sx.bin"));
    pf->setFileSize(PARTSIZE);
    queue.addDownload(pf);

    auto* peer = track(makeRequester(extSX, /*skipTags*/ extSX, supportsSX2));
    RecordingSocket sock;
    peer->setSocket(&sock);
    peer->setReqFile(pf);
    peer->setReqUpFileId(pf->fileHash());
    peer->setTestDisableMultiPacket(true);   // force the separate-packets path

    peer->sendFileRequest();

    const auto* req = sock.find(expectedOpcode);
    QVERIFY2(req != nullptr, "no source request was sent");
    QCOMPARE(req->prot, uint8(OP_EMULEPROT));
    QCOMPARE(req->payload.size(), expectedSize);

    if (expectedVersion >= 0) {
        QCOMPARE(int(uint8(req->payload.at(0))), expectedVersion);
        QCOMPARE(readU16(req->payload.constData(), 1), uint16(0));      // options, reserved
        QCOMPARE(std::memcmp(req->payload.constData() + 3, hash, 16), 0);
    } else {
        QCOMPARE(std::memcmp(req->payload.constData(), hash, 16), 0);
    }
    // The two request opcodes are alternatives, never both.
    QCOMPARE(sock.countOf(OP_REQUESTSOURCES2) + sock.countOf(OP_REQUESTSOURCES), 1);

    peer->setSocket(nullptr);
    theApp.downloadQueue = nullptr;
    queue.deleteAll();
}

void tst_SourceExchangeCompat::multipacketRequest_versionByteMatchesPeerCapability_data()
{
    standaloneRequest_versionByteMatchesPeerCapability_data();
}

// Same rule for the bundled form. It is a separate code path in DownloadClient.cpp, so it
// gets its own coverage — a peer that receives the wrong version byte here answers in the
// wrong dialect just as surely.
void tst_SourceExchangeCompat::multipacketRequest_versionByteMatchesPeerCapability()
{
    QFETCH(bool, supportsSX2);
    QFETCH(bool, extSX);
    QFETCH(uint8, expectedOpcode);
    QFETCH(int, expectedVersion);

    DownloadQueue queue;
    theApp.downloadQueue = &queue;

    auto* pf = new PartFile;
    uint8 hash[16];
    std::memset(hash, 0x34, sizeof(hash));
    pf->setFileHash(hash);
    pf->setFileName(QStringLiteral("sx-mp.bin"));
    pf->setFileSize(PARTSIZE);
    queue.addDownload(pf);

    // Give the peer its capabilities the way a real one does — through a hello.
    auto* peer = track(new UpDownClient);
    std::vector<Tag> tags;
    tags.emplace_back(CT_EMULE_VERSION, kEmuleVersionTag);
    tags.emplace_back(CT_EMULE_MISCOPTIONS1, kMiscOptions1);
    tags.emplace_back(CT_EMULE_MISCOPTIONS2,
                      supportsSX2 ? kMiscOptions2 : (kMiscOptions2 & ~(uint32(1) << 10)));
    if (extSX)
        tags.emplace_back(CT_MOD_MISCOPTIONS, uint32(MODMISC_EXTXS | MODMISC_EXTXS_SKIPTAGS));
    const auto hello = buildHelloAnswer(0x77, 0x0A0B0C0D, 4662, tags);
    QVERIFY(peer->processHelloAnswer(reinterpret_cast<const uint8*>(hello.constData()),
                                     static_cast<uint32>(hello.size())));
    QCOMPARE(peer->supportsSourceExchange2(), supportsSX2);
    QCOMPARE(peer->supportsExtendedXS(), extSX);
    QVERIFY(peer->supportMultiPacket());

    RecordingSocket sock;
    peer->setSocket(&sock);
    peer->setReqFile(pf);
    peer->setReqUpFileId(pf->fileHash());

    peer->sendFileRequest();

    // Exactly one multipacket, and no standalone source request alongside it.
    QCOMPARE(sock.countOf(OP_REQUESTSOURCES2) + sock.countOf(OP_REQUESTSOURCES), 0);
    const RecordingSocket::Sent* mp = sock.find(OP_MULTIPACKET_EXT2);
    if (!mp) mp = sock.find(OP_MULTIPACKET_EXT);
    if (!mp) mp = sock.find(OP_MULTIPACKET);
    QVERIFY2(mp != nullptr, "no multipacket was sent");

    // Find the bundled sub-request. The sub-opcode is followed by the version byte only
    // for the SX2 form, which is precisely the thing under test.
    const int idx = static_cast<int>(mp->payload.indexOf(char(expectedOpcode)));
    QVERIFY2(idx > 0, "the multipacket carries no source sub-request");
    if (expectedVersion >= 0) {
        QCOMPARE(int(uint8(mp->payload.at(idx + 1))), expectedVersion);
        QCOMPARE(readU16(mp->payload.constData(), idx + 2), uint16(0));   // options
    }

    peer->setSocket(nullptr);
    theApp.downloadQueue = nullptr;
    queue.deleteAll();
}

// Our own advertisement has to survive our own parser: these three bits are what every
// ExtSX decision on both sides keys off.
void tst_SourceExchangeCompat::helloAdvertisesExtSxBits_roundTrip()
{
    auto* us = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    RecordingSocket sock;
    us->setSocket(&sock);

    us->sendHelloAnswer();

    const auto* hello = sock.find(OP_HELLOANSWER);
    QVERIFY2(hello != nullptr, "no hello answer was sent");

    auto* peer = track(new UpDownClient);
    QVERIFY(peer->processHelloAnswer(reinterpret_cast<const uint8*>(hello->payload.constData()),
                                     static_cast<uint32>(hello->payload.size())));

    QVERIFY(peer->supportsIPv6());
    QVERIFY(peer->supportsExtendedXS());
    QVERIFY(peer->supportsExtSXSkipTags());
    QVERIFY(peer->supportsSourceExchange2());
    QCOMPARE(peer->sourceExchange1Ver(), uint8(4));

    us->setSocket(nullptr);
}

// The other half of the same contract: a peer that says nothing about MODMISC_EXTXS is
// treated as a client that has never heard of it, all the way through to the bytes.
void tst_SourceExchangeCompat::helloWithoutModMiscOptions_getsClassicAnswer()
{
    auto* peer = track(new UpDownClient);
    std::vector<Tag> tags;
    tags.emplace_back(CT_EMULE_VERSION, kEmuleVersionTag);
    tags.emplace_back(CT_EMULE_MISCOPTIONS1, kMiscOptions1);
    tags.emplace_back(CT_EMULE_MISCOPTIONS2, kMiscOptions2);   // no CT_MOD_MISCOPTIONS
    const auto hello = buildHelloAnswer(0x66, 0x0A0B0C0D, 4662, tags);
    QVERIFY(peer->processHelloAnswer(reinterpret_cast<const uint8*>(hello.constData()),
                                     static_cast<uint32>(hello.size())));

    QVERIFY(!peer->supportsExtendedXS());
    QVERIFY(!peer->supportsExtSXSkipTags());
    QVERIFY(peer->supportsSourceExchange2());

    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    src->setUserIPv6(Address::fromString(QStringLiteral("2a01:4f8::1")));
    src->setOpenIPv6(true);

    auto file = makeFile({ src });
    auto packet = file->createSrcInfoPacket(peer, 4, 0);
    QVERIFY(packet != nullptr);

    QCOMPARE(uint8(packet->pBuffer[0]), uint8(4));
    QCOMPARE(qint64(packet->size), qint64(kSx2HeaderSize + 2 + classicRecordSize(4)));
    const LegacyParse parsed = legacyClassicReader(answerBody(*packet, true), 4, true);
    QVERIFY(parsed.accepted);
    QCOMPARE(int(parsed.sources.size()), 1);
}

// ===========================================================================
// E. The source request carried inside a multipacket
// ===========================================================================

void tst_SourceExchangeCompat::multipacketExt2_bundledSourceRequest_isAnswered_data()
{
    QTest::addColumn<bool>("extSX");
    QTest::addColumn<uint8>("requestedVersion");

    QTest::newRow("ExtSX peer asks v1") << true  << uint8(SOURCEEXCHANGEEXT_VERSION);
    QTest::newRow("classic peer asks v4") << false << uint8(SOURCEEXCHANGE2_VERSION);
}

// We bundle the source request into our own multipacket, and so does every current eMule.
// A peer that only handles the standalone opcode answers nothing at all — and, worse, reads
// the version byte as the next sub-opcode.
void tst_SourceExchangeCompat::multipacketExt2_bundledSourceRequest_isAnswered()
{
    QFETCH(bool, extSX);
    QFETCH(uint8, requestedVersion);

    SharedFileFixture fixture;
    auto* file = fixture.file;

    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    if (extSX) {
        src->setUserIPv6(Address::fromString(QStringLiteral("2a01:4f8::1")));
        src->setOpenIPv6(true);
    }
    file->addUploadingClient(src);

    auto* peer = track(makeRequester(extSX, extSX));
    RecordingSocket sock;
    peer->wireIncomingSocket(&sock);

    SafeMemFile mp;
    file->fileIdentifier().writeIdentifier(mp);
    mp.writeUInt8(OP_REQUESTSOURCES2);
    mp.writeUInt8(requestedVersion);
    mp.writeUInt16(0);
    const QByteArray body = mp.buffer();

    sock.deliverExt(body, OP_MULTIPACKET_EXT2);

    const auto* answer = sock.find(OP_ANSWERSOURCES2);
    QVERIFY2(answer != nullptr, "the bundled source request went unanswered");
    QCOMPARE(uint8(answer->payload.at(0)), extSX ? uint8(SOURCEEXCHANGEEXT_VERSION)
                                                 : requestedVersion);
    QCOMPARE(readU16(answer->payload.constData(), kSx2HeaderSize), uint16(1));

    peer->setSocket(nullptr);
}

// The deprecated multipacket carries the deprecated request, which has no version or
// options bytes at all — reading them anyway would consume the next sub-opcode.
void tst_SourceExchangeCompat::multipacketLegacy_bundledSourceRequest_isAnswered()
{
    SharedFileFixture fixture;
    auto* file = fixture.file;

    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    file->addUploadingClient(src);

    auto* peer = track(makeRequester(/*extSX*/ false));
    peer->setSourceExchange1Ver(3);
    RecordingSocket sock;
    peer->wireIncomingSocket(&sock);

    SafeMemFile mp;
    mp.writeHash16(file->fileHash());
    mp.writeUInt8(OP_REQUESTSOURCES);
    const QByteArray body = mp.buffer();

    sock.deliverExt(body, OP_MULTIPACKET);

    const auto* answer = sock.find(OP_ANSWERSOURCES);
    QVERIFY2(answer != nullptr, "the bundled SX1 request went unanswered");
    QCOMPARE(readU16(answer->payload.constData(), kSx1HeaderSize), uint16(1));

    peer->setSocket(nullptr);
}

// The regression that motivated the fix: the version and options bytes have to be consumed
// as part of the sub-request, or the very next loop iteration reads the version byte as a
// sub-opcode and everything after it is lost.
void tst_SourceExchangeCompat::multipacket_sourceRequestDoesNotDesyncFollowingSubOpcodes()
{
    SharedFileFixture fixture;
    auto* file = fixture.file;

    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    file->addUploadingClient(src);

    auto* peer = track(makeRequester(/*extSX*/ true, /*skipTags*/ true));
    RecordingSocket sock;
    peer->wireIncomingSocket(&sock);

    SafeMemFile mp;
    file->fileIdentifier().writeIdentifier(mp);
    mp.writeUInt8(OP_REQUESTFILENAME);
    mp.writeUInt8(OP_REQUESTSOURCES2);
    mp.writeUInt8(SOURCEEXCHANGEEXT_VERSION);
    mp.writeUInt16(0);
    mp.writeUInt8(OP_SETREQFILEID);           // reachable only if the above was consumed exactly
    const QByteArray body = mp.buffer();

    sock.deliverExt(body, OP_MULTIPACKET_EXT2);

    QVERIFY(sock.find(OP_ANSWERSOURCES2) != nullptr);

    const auto* mpAnswer = sock.find(OP_MULTIPACKETANSWER_EXT2);
    QVERIFY2(mpAnswer != nullptr, "no multipacket answer");

    // Both the sub-opcode before the source request and the one after it were handled.
    QVERIFY(mpAnswer->payload.contains(char(OP_REQFILENAMEANSWER)));
    QVERIFY(mpAnswer->payload.contains(char(OP_FILESTATUS)));

    peer->setSocket(nullptr);
}

// "We still send the source packet separately" — srchybrid/ListenSocket.cpp:987. A
// multipacket that asks only for sources produces an answer packet and no multipacket
// answer at all.
void tst_SourceExchangeCompat::multipacket_sourceAnswerIsSeparatePacket()
{
    SharedFileFixture fixture;
    auto* file = fixture.file;

    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    file->addUploadingClient(src);

    auto* peer = track(makeRequester(/*extSX*/ false));
    RecordingSocket sock;
    peer->wireIncomingSocket(&sock);

    SafeMemFile mp;
    file->fileIdentifier().writeIdentifier(mp);
    mp.writeUInt8(OP_REQUESTSOURCES2);
    mp.writeUInt8(SOURCEEXCHANGE2_VERSION);
    mp.writeUInt16(0);
    const QByteArray body = mp.buffer();

    sock.deliverExt(body, OP_MULTIPACKET_EXT2);

    QVERIFY(sock.find(OP_ANSWERSOURCES2) != nullptr);
    QVERIFY2(sock.find(OP_MULTIPACKETANSWER_EXT2) == nullptr,
             "the source answer must not ride inside the multipacket answer");

    peer->setSocket(nullptr);
}

// The rate limit is what stops a peer from using source exchange as an amplifier. The
// first ask is always allowed (MFC's bNeverAskedBefore); an immediate repeat is not.
void tst_SourceExchangeCompat::secondSourceRequestWithinIntervalIsRefused()
{
    SharedFileFixture fixture;
    auto* file = fixture.file;

    auto* src = track(makeHighIdClient(QStringLiteral("60.70.80.90"), 4662, 0xA1));
    file->addUploadingClient(src);

    auto* peer = track(makeRequester(/*extSX*/ false));
    RecordingSocket sock;
    peer->wireIncomingSocket(&sock);

    SafeMemFile req;
    req.writeUInt8(SOURCEEXCHANGE2_VERSION);
    req.writeUInt16(0);
    req.writeHash16(file->fileHash());
    const QByteArray body = req.buffer();

    const auto ask = [&] {
        sock.deliverExt(body, OP_REQUESTSOURCES2);
    };

    ask();
    QCOMPARE(sock.countOf(OP_ANSWERSOURCES2), 1);
    ask();
    QCOMPARE(sock.countOf(OP_ANSWERSOURCES2), 1);

    peer->setSocket(nullptr);
}

// ===========================================================================
// F. The compressed transport, end to end
//
// Every case above builds an answer and reads it back in the same process with the packet
// object in hand — extSxAnswerZlibRoundTrip even calls unPackPacket() itself. That hand
// unpack is a crutch: it proved the *format* survived compression while saying nothing
// about whether the receive path would ever inflate anything. It would not, so a peer
// running this same code sent us answers we dropped. These two go in through the socket.
// ===========================================================================

void tst_SourceExchangeCompat::packedExtSxAnswerFromPeerIsParsed()
{
    constexpr int kSources = 40;   // comfortably past the 354-byte pack threshold

    std::vector<UpDownClient*> srcs;
    srcs.reserve(kSources);
    for (int i = 0; i < kSources; ++i) {
        auto* c = track(makeHighIdClient(QStringLiteral("60.%1.%2.90").arg(i / 250).arg(i % 250 + 1),
                                         static_cast<uint16>(4662 + i), uint8(0x40 + i)));
        c->setUserIPv6(Address::fromString(QStringLiteral("2a01:4f8::%1").arg(i + 1, 0, 16)));
        c->setOpenIPv6(true);
        srcs.push_back(c);
    }

    auto file = makeFile(srcs);
    // One peer plays both parts: we build the answer it would send us, then it sends it.
    auto* peer = track(makeRequester(/*extSX*/ true, /*skipTags*/ true));

    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGEEXT_VERSION, 0);
    QVERIFY(packet != nullptr);
    QCOMPARE(packet->prot, uint8(OP_PACKEDPROT));   // our own emitter packed it

    const QByteArray wire(packet->pBuffer, static_cast<int>(packet->size));

    deliverAnswerInto(file->fileHash(), peer, packet->prot, packet->opcode, wire,
                      [&](PartFile& pf) {
        QCOMPARE(pf.sourceCount(), kSources);
        for (auto* s : pf.srcList()) {
            QVERIFY(s->openIPv6());
            QVERIFY(s->userIPv6().isPublicIP());
        }
    });
}

void tst_SourceExchangeCompat::packedClassicAnswerFromPeerIsParsed()
{
    constexpr int kSources = 40;

    std::vector<UpDownClient*> srcs;
    srcs.reserve(kSources);
    for (int i = 0; i < kSources; ++i) {
        srcs.push_back(track(makeHighIdClient(
            QStringLiteral("70.%1.%2.90").arg(i / 250).arg(i % 250 + 1),
            static_cast<uint16>(4662 + i), uint8(0x40 + i))));
    }

    auto file = makeFile(srcs);
    auto* peer = track(makeRequester(/*extSX*/ false));

    auto packet = file->createSrcInfoPacket(peer, SOURCEEXCHANGE2_VERSION, 0);
    QVERIFY(packet != nullptr);
    QCOMPARE(packet->prot, uint8(OP_PACKEDPROT));

    const QByteArray wire(packet->pBuffer, static_cast<int>(packet->size));

    // The classic dialect rides the same compressed transport, so a peer with no ExtSX
    // support was equally unreachable before the fix — this is not an ExtSX-only problem.
    deliverAnswerInto(file->fileHash(), peer, packet->prot, packet->opcode, wire,
                      [&](PartFile& pf) {
        QCOMPARE(pf.sourceCount(), kSources);
    });
}

QTEST_MAIN(tst_SourceExchangeCompat)
#include "tst_SourceExchangeCompat.moc"
