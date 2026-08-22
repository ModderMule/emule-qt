#include "pch.h"
/// @file HttpCacheClient.cpp
/// @brief Downloader side of HTTP Cache — implementation.

#include "httpcache/HttpCacheClient.h"

#include "app/AppContext.h"
#include "client/ClientStructs.h"
#include "files/PartFile.h"
#include "net/EMSocket.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <iterator>

namespace eMule {

namespace {

/// Backoff between attempts, indexed by how many consecutive attempts have made
/// no progress. Short enough that a blip costs little, long enough that a server
/// having a bad minute is not hammered.
constexpr int kResumeDelaysMs[] = {2000, 6000, 15000};

/// Do not start another attempt for an offer about to lapse — the fetch would be
/// cut off by the server mid-stream and the bytes wasted.
constexpr qint64 kResumeExpiryMarginSeconds = 120;

} // namespace

HttpCacheClient::HttpCacheClient(QObject* parent)
    : URLClient(parent)
{
}

HttpCacheClient::~HttpCacheClient()
{
    // A client torn down mid-flight still owns its reservation; leaking it would
    // wedge the part permanently — getNextRequestedBlock would keep skipping
    // ranges no one is fetching.
    releaseReservation();
}

bool HttpCacheClient::beginFetch(const HttpCacheOffer& offer, PartFile* file,
                                 const std::array<uint8, 16>& offeringPeerHash,
                                 const Address& offeringPeerAddress)
{
    if (!file || !offer.isWellFormed())
        return false;

    if (offer.partIndex >= file->partCount())
        return false;

    if (file->isComplete(offer.partIndex))
        return false;

    m_offer = offer;
    m_peerHash = offeringPeerHash;
    m_peerAddress = offeringPeerAddress;

    if (!setUrl(m_offer.url, Address{}))
        return false;

    setRequestFile(file);
    setSourceFrom(SourceFrom::HttpCache);

    // Claim the part's gaps so ed2k sources do not spend upstream on bytes we are
    // already pulling. Released in finish(), whatever the outcome.
    if (file->reservePartForExternalTransfer(m_offer.partIndex, m_reserved) == 0) {
        // Every gap is already being fetched by somebody else — nothing to gain.
        return false;
    }

    // Registering as a source is what puts the fetch in the transfer list (the
    // downloads and clients models already paint SourceFrom::HttpCache), and it
    // is also what makes a long-lived resuming fetch safe: ~PartFile nulls
    // reqFile() on every client in m_srcList, so this object cannot be left
    // holding a dangling file pointer between attempts.
    file->addSource(this);

    logInfo(QStringLiteral("HTTP Cache: fetching part %1 of %2 from %3")
                .arg(m_offer.partIndex)
                .arg(file->fileName())
                .arg(QUrl(m_offer.url).host()));

    return tryToConnect();
}

// ---------------------------------------------------------------------------
// URLClient overrides
// ---------------------------------------------------------------------------

bool HttpCacheClient::sendHttpBlockRequests()
{
    if (m_finished)
        return false;

    // The base class calls this again every time a block completes
    // (UpDownClient::processBlockPacket -> sendBlockRequests), which is right for
    // a plain URL source: it fetches one 180 KB block per request. Here a single
    // request covers the whole part, so a second GET would be pipelined onto the
    // socket behind the response still being read — its reply would then be
    // parsed as a continuation of that body, and the transfer stalls at whatever
    // offset the extra request went out at. One request per connection.
    if (m_requestInFlight)
        return true;

    ++m_totalAttempts;
    m_progressAtAttemptStart = m_cipherConsumed;
    m_expectedBodyStart = m_cipherConsumed;
    m_headersOk = false;

    // Whatever was staged belonged to the connection that died; the resumed
    // request starts at the last block boundary and refetches it.
    m_stage.clear();

    // At offset 0 the chaining value is the offer's IV; after that it is the
    // ciphertext block immediately before the resume point, which was kept in
    // m_chain rather than refetched — asking the server for it again would feed
    // those 16 bytes to the running SHA-256 twice.
    //
    // Padding stays disabled for the whole transfer: with it on, EVP holds a
    // block back and buffers partial ones, so the decryptor's idea of "consumed"
    // would drift from the digest's and there would be no single restart point.
    // The PKCS#7 tail is checked by hand in verifyComplete() instead.
    const QByteArray chain = (m_cipherConsumed == 0) ? m_offer.iv : m_chain;
    if (chain.size() != kAesIvSize || !m_decryptor.beginAt(m_offer.key, chain, false)) {
        finish(HttpCacheResult::Corrupt);
        return false;
    }

    // One GET for the whole remaining ciphertext, not one per 180 KB block: the
    // saving only materialises if the cache serves the part in a single response.
    QByteArray request = buildGetHeader();
    request += "Range: bytes=";
    request += QByteArray::number(static_cast<qint64>(m_cipherConsumed));
    request += '-';
    request += QByteArray::number(static_cast<qint64>(m_offer.cipherLength - 1));
    request += "\r\n\r\n";

    m_requestInFlight = true;
    return sendRawRequest(request);
}

bool HttpCacheClient::processHttpDownResponse(const QList<QByteArray>& headers)
{
    if (headers.isEmpty()) {
        finish(HttpCacheResult::HttpFailed);
        return false;
    }

    const int code = parseStatusCode(headers.first());

    if (code == 416) {
        // The object no longer covers the range the offer described, so no amount
        // of retrying will help.
        logWarning(QStringLiteral("HTTP Cache: server rejected range %1- for part %2")
                       .arg(m_expectedBodyStart)
                       .arg(m_offer.partIndex));
        finish(HttpCacheResult::SizeMismatch);
        return false;
    }

    if (code != 200 && code != 206) {
        logDebug(QStringLiteral("HTTP Cache: server returned %1 for part %2")
                     .arg(code)
                     .arg(m_offer.partIndex));
        finish(HttpCacheResult::HttpFailed);
        return false;
    }

    // A 200 to a resumed request means the server ignored Range and is about to
    // send the whole object. Restarting costs the bytes already fetched, but it
    // still completes — failing here would hand the part back to ed2k for a
    // server behaviour we can simply absorb.
    if (code == 200 && m_expectedBodyStart != 0) {
        logWarning(QStringLiteral("HTTP Cache: server ignored Range on part %1, restarting from 0")
                       .arg(m_offer.partIndex));
        if (!restartFromZero()) {
            finish(HttpCacheResult::HttpFailed);
            return false;
        }
    }

    const uint64 expectedBody = m_offer.cipherLength - m_expectedBodyStart;

    // Whatever the server says the body is, it must be exactly the ciphertext
    // still outstanding. A different length means the offer and the blob
    // disagree, and decrypting it would just produce garbage that fails the MD4
    // check later at the cost of a full part's worth of bandwidth.
    const QByteArray contentLength = headerValue(headers, "content-length");
    if (!contentLength.isEmpty()) {
        bool ok = false;
        const qulonglong len = contentLength.toULongLong(&ok);
        if (ok && len != expectedBody) {
            logWarning(QStringLiteral("HTTP Cache: server offers %1 bytes, expected %2")
                           .arg(len)
                           .arg(expectedBody));
            finish(HttpCacheResult::SizeMismatch);
            return false;
        }
    }

    if (code == 206) {
        uint64 first = 0;
        uint64 last = 0;
        uint64 total = 0;
        const QByteArray contentRange = headerValue(headers, "content-range");

        if (!parseContentRange(contentRange, first, last, total)
            || first != m_expectedBodyStart
            || last != m_offer.cipherLength - 1
            || total != m_offer.cipherLength) {
            logWarning(QStringLiteral("HTTP Cache: unusable Content-Range '%1', wanted %2-%3/%4")
                           .arg(QString::fromLatin1(contentRange))
                           .arg(m_expectedBodyStart)
                           .arg(m_offer.cipherLength - 1)
                           .arg(m_offer.cipherLength));
            finish(HttpCacheResult::SizeMismatch);
            return false;
        }
    }

    m_headersOk = true;
    return true;
}

bool HttpCacheClient::processHttpDownResponseBody(const uint8* data, uint32 size)
{
    if (m_finished || !m_headersOk || !data || size == 0)
        return false;

    const uint64 held = m_cipherConsumed + static_cast<uint64>(m_stage.size());

    // Never take more than promised: a server that keeps sending must not be able
    // to push us past the end of the part.
    uint32 usable = size;
    if (held + usable > m_offer.cipherLength)
        usable = static_cast<uint32>(m_offer.cipherLength - held);

    if (usable == 0) {
        finish(HttpCacheResult::SizeMismatch);
        return false;
    }

    // Throughput accounting meters what crossed the wire, not what the cipher
    // was ready to consume.
    accumulateDownBytes(usable);
    m_stage.append(reinterpret_cast<const char*>(data), static_cast<qsizetype>(usable));

    if (!consumeStagedBlocks())
        return false;

    if (m_cipherConsumed < m_offer.cipherLength)
        return true;

    verifyComplete();
    return true;
}

QString HttpCacheClient::downloadStateDisplayString() const
{
    return URLClient::downloadStateDisplayString() + QStringLiteral(" (HTTP Cache)");
}

bool HttpCacheClient::disconnected(const QString& reason, bool fromSocket)
{
    // Whatever was outstanding died with the socket; a resumed attempt issues its
    // own request on a new connection.
    m_requestInFlight = false;

    const bool incomplete = !m_finished && m_cipherConsumed < m_offer.cipherLength;

    if (incomplete) {
        // An attempt that moved the stream forward earns a clean slate. Both
        // counters matter: resetting on progress alone lets a server dribbling a
        // single block per connection reconnect forever, and a fixed cap alone
        // would abandon a flaky link that is in fact getting there.
        if (m_cipherConsumed > m_progressAtAttemptStart)
            m_consecutiveRetries = 0;

        if (canResume())
            scheduleResume();
        else
            finish(HttpCacheResult::HttpFailed);
    }

    return URLClient::disconnected(reason, fromSocket);
}

void HttpCacheClient::sendCancelTransfer()
{
    // A withdrawn offer or a shutdown is final — never resume out of one.
    if (!m_finished)
        finish(HttpCacheResult::NotWanted);

    URLClient::sendCancelTransfer();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void HttpCacheClient::finish(HttpCacheResult result)
{
    if (m_finished)
        return;

    m_finished = true;
    releaseReservation();

    // Leave the transfer list the moment the fetch is over, and drop the file
    // reference so ~UpDownClient does not try to swap this client onto another
    // download the way it would for an ed2k source.
    if (PartFile* file = reqFile()) {
        file->removeSource(this);
        setReqFile(nullptr);
    }

    // Nothing else closes the socket: it is not a child of this object, it only
    // deletes itself once disconnected, and a completed fetch leaves a perfectly
    // healthy keep-alive connection behind.
    if (EMSocket* s = socket())
        s->disconnectFromHost();

    emit fetchFinished(this, result, m_plainWritten);
}

void HttpCacheClient::releaseReservation()
{
    if (m_reserved.empty())
        return;

    if (PartFile* file = reqFile()) {
        file->releaseReservedBlocks(m_reserved);
        return;
    }

    // The part file is already gone. ~PartFile only *clears* m_requestedBlocks,
    // because those entries are normally owned by a client's
    // Pending_Block_Struct — ours are not, so nobody else will ever free them.
    for (auto* block : m_reserved)
        delete block;

    m_reserved.clear();
}

bool HttpCacheClient::fetchStillWanted() const
{
    if (m_finished)
        return false;

    if (!thePrefs.httpCacheEnabled() || !thePrefs.httpCacheAllowDownload())
        return false;

    PartFile* file = reqFile();
    if (!file || file->isComplete(m_offer.partIndex))
        return false;

    if (m_offer.expiresAt != 0
        && static_cast<qint64>(m_offer.expiresAt)
               < QDateTime::currentSecsSinceEpoch() + kResumeExpiryMarginSeconds)
        return false;

    return true;
}

bool HttpCacheClient::canResume() const
{
    if (m_consecutiveRetries >= kMaxResumeAttempts || m_totalAttempts >= kMaxTotalAttempts)
        return false;

    return fetchStillWanted();
}

void HttpCacheClient::scheduleResume()
{
    const int slot = std::clamp(m_consecutiveRetries, 0,
                                static_cast<int>(std::size(kResumeDelaysMs)) - 1);
    const int delay = m_resumeDelayOverrideMs >= 0 ? m_resumeDelayOverrideMs : kResumeDelaysMs[slot];

    // Counted here rather than on failure: this is the retry, and canResume()
    // reads the count to decide whether another one is allowed.
    ++m_consecutiveRetries;

    logInfo(QStringLiteral("HTTP Cache: part %1 dropped at %2/%3 bytes, resuming in %4 ms")
                .arg(m_offer.partIndex)
                .arg(m_cipherConsumed)
                .arg(m_offer.cipherLength)
                .arg(delay));

    // Deferred on purpose. ClientReqSocket::disconnect() emits clientDisconnected
    // *before* tearing itself down, and UpDownClient::disconnected() sets
    // m_socket = nullptr on the way out — so a tryToConnect() called inline here
    // would have its brand-new socket nulled the moment this call stack unwinds.
    QTimer::singleShot(delay, this, [this] {
        if (m_finished)
            return;

        // Deliberately not canResume(): this retry was already granted and paid
        // for out of the budget above. Re-checking it here would spend the same
        // retry twice and cut the fetch one attempt short.
        if (!fetchStillWanted()) {
            finish(HttpCacheResult::HttpFailed);
            return;
        }

        if (tryToConnect())
            return;

        // Refused before a socket was even created — tooManySockets() is the
        // realistic cause and it is transient, so back off rather than give up.
        if (canResume())
            scheduleResume();
        else
            finish(HttpCacheResult::HttpFailed);
    });
}

bool HttpCacheClient::consumeStagedBlocks()
{
    const qsizetype whole = m_stage.size() - (m_stage.size() % kAesBlockSize);
    if (whole == 0)
        return true;

    const QByteArray blocks = m_stage.left(whole);
    m_stage.remove(0, whole);

    m_cipherHash.addData(blocks);

    const QByteArray plain = m_decryptor.update(blocks);
    if (plain.size() != whole) {
        // With padding disabled a whole-block input yields exactly as much
        // output, so a short result means the cipher context failed.
        logWarning(QStringLiteral("HTTP Cache: decrypt failed on part %1")
                       .arg(m_offer.partIndex));
        finish(HttpCacheResult::Corrupt);
        return false;
    }

    m_chain = blocks.right(kAesBlockSize);
    m_cipherConsumed += static_cast<uint64>(whole);

    // Split what came out into file bytes and the trailing PKCS#7 block, which
    // belongs to the cipher and never to the part.
    qsizetype toWrite = plain.size();
    if (m_plainWritten + static_cast<uint64>(toWrite) > m_offer.plainLength)
        toWrite = static_cast<qsizetype>(m_offer.plainLength - m_plainWritten);

    if (toWrite > 0) {
        PartFile* file = reqFile();
        if (!file) {
            finish(HttpCacheResult::NotWanted);
            return false;
        }

        const uint64 start = partStart() + m_plainWritten;
        const uint64 end = start + static_cast<uint64>(toWrite) - 1;

        // Same inclusive-end convention as every other writer into a part file.
        //
        // The sender recorded is the *peer that offered the chunk*, not this client,
        // whose address is the cache server's. The server is transparent here: it holds
        // an AES-256-CBC blob it has no key for, and verifyComplete() checks the
        // ciphertext against the SHA-256 the peer pinned in the offer before any of this
        // plaintext is trusted. Bytes that reach the part file are therefore provably
        // what the peer published, so a part that later fails MD4 is the peer's doing and
        // the server has no way to be at fault on its own — blaming it would take a
        // working cache away from every download for someone else's bad data.
        file->writeToBuffer(static_cast<uint64>(toWrite),
                            reinterpret_cast<const uint8*>(plain.constData()), start, end,
                            nullptr, m_peerAddress);

        m_plainWritten += static_cast<uint64>(toWrite);
        addPayloadDown(static_cast<uint64>(toWrite));
    }

    if (toWrite < plain.size())
        m_pad.append(plain.mid(toWrite));

    return true;
}

bool HttpCacheClient::restartFromZero()
{
    m_cipherHash.reset();
    m_cipherConsumed = 0;
    m_plainWritten = 0;
    m_expectedBodyStart = 0;
    m_progressAtAttemptStart = 0;
    m_stage.clear();
    m_pad.clear();
    m_chain.clear();

    return m_decryptor.beginAt(m_offer.key, m_offer.iv, false);
}

void HttpCacheClient::verifyComplete()
{
    // cipherLength is always a whole number of blocks — malformedReason() ties it
    // to cipherLengthFor(plainLength) — so nothing may be left staged here.
    if (!m_stage.isEmpty()) {
        finish(HttpCacheResult::SizeMismatch);
        return;
    }

    // The digest is over the ciphertext, so it separates "the server or the
    // network mangled the blob" from "the uploader published something wrong".
    // The MD4 part hash in flushBuffer() is still the final arbiter of the
    // plaintext — this only lets us fail earlier and blame more precisely. It
    // spans every attempt: each byte is hashed exactly once, in order, however
    // many connections it took to get them.
    const QByteArray digest = m_cipherHash.result();
    if (digest != m_offer.cipherSha256) {
        logWarning(QStringLiteral("HTTP Cache: ciphertext digest mismatch on part %1")
                       .arg(m_offer.partIndex));
        finish(HttpCacheResult::Corrupt);
        return;
    }

    // Padding is disabled on the decryptor, so releasing the context emits
    // nothing and the PKCS#7 block is ours to check.
    (void)m_decryptor.finish(nullptr);

    const uint64 pad = padLength();
    bool paddingOk = pad >= 1 && pad <= static_cast<uint64>(kAesBlockSize)
                  && static_cast<uint64>(m_pad.size()) == pad;

    if (paddingOk) {
        for (const char byte : m_pad) {
            if (static_cast<uint8>(byte) != pad) {
                paddingOk = false;
                break;
            }
        }
    }

    if (!paddingOk) {
        logWarning(QStringLiteral("HTTP Cache: bad padding on part %1 — wrong key?")
                       .arg(m_offer.partIndex));
        finish(HttpCacheResult::Corrupt);
        return;
    }

    if (m_plainWritten != m_offer.plainLength) {
        logWarning(QStringLiteral("HTTP Cache: decrypted %1 bytes, expected %2")
                       .arg(m_plainWritten)
                       .arg(m_offer.plainLength));
        finish(HttpCacheResult::Corrupt);
        return;
    }

    logInfo(QStringLiteral("HTTP Cache: part %1 complete (%2 bytes, %3 connection(s))")
                .arg(m_offer.partIndex)
                .arg(m_plainWritten)
                .arg(m_totalAttempts));

    finish(HttpCacheResult::Ok);
}

bool HttpCacheClient::parseContentRange(const QByteArray& value, uint64& first, uint64& last,
                                        uint64& total)
{
    // "bytes <first>-<last>/<total>" — RFC 9110 §14.4. Anything else, including
    // the unsatisfied "bytes */<total>" form, is not something we can follow.
    if (!value.startsWith("bytes "))
        return false;

    const QByteArray spec = value.mid(6).trimmed();
    const auto dash = spec.indexOf('-');
    const auto slash = spec.indexOf('/');
    if (dash <= 0 || slash <= dash)
        return false;

    bool okFirst = false;
    bool okLast = false;
    bool okTotal = false;

    first = spec.left(dash).trimmed().toULongLong(&okFirst);
    last = spec.mid(dash + 1, slash - dash - 1).trimmed().toULongLong(&okLast);
    total = spec.mid(slash + 1).trimmed().toULongLong(&okTotal);

    return okFirst && okLast && okTotal;
}

uint64 HttpCacheClient::partStart() const
{
    return static_cast<uint64>(m_offer.partIndex) * PARTSIZE;
}

} // namespace eMule
