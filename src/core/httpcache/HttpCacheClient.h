#pragma once

/// @file HttpCacheClient.h
/// @brief Downloader side of HTTP Cache — fetches one encrypted part over HTTP.
///
/// A URLClient with a decrypt stage. URLClient already is an UpDownClient that
/// speaks HTTP with Range headers, is registered with the bandwidth throttler
/// and the transfer list, and funnels received bytes into
/// PartFile::writeToBuffer(); all this adds is:
///
///   - one GET for the whole part rather than one per 180 KB block,
///   - streaming AES-256-CBC decryption between socket and part file,
///   - a SHA-256 check over the ciphertext, so a mangled blob is distinguishable
///     from a lying uploader before the MD4 part hash even runs,
///   - Range-based resume, using the preceding ciphertext block as the CBC IV.
///
/// The plaintext still lands in the ordinary part-file write path, so the usual
/// MD4/AICH verification in PartFile::flushBuffer() is what finally decides the
/// data was good. Nothing here is trusted.
///
/// **Resume.** CBC maps ciphertext byte i to plaintext byte i, so a restart only
/// has to land on a multiple of kAesBlockSize with the preceding ciphertext block
/// as its IV. The bookkeeping is the delicate part: the SHA-256 runs over the
/// whole ciphertext and QCryptographicHash cannot rewind, so the digest must see
/// each byte exactly once across all attempts. That is why incoming bytes are
/// staged and only ever advance in whole blocks — the hash and the decryptor are
/// fed the identical block-aligned stream, and m_cipherConsumed is the single
/// authoritative position both of them share.

#include "client/URLClient.h"
#include "crypto/AesCbc.h"
#include "httpcache/HttpCacheOffer.h"

#include <QByteArray>
#include <QCryptographicHash>

#include <vector>

namespace eMule {

class HttpCacheClient : public URLClient {
    Q_OBJECT

public:
    /// Retries allowed since the stream last moved forward. The initial request
    /// is not a retry, so a fetch makes at most kMaxResumeAttempts + 1 attempts
    /// without ever making progress.
    static constexpr int kMaxResumeAttempts = 3;

    /// Hard ceiling on attempts for one fetch, progress or not. Without it a
    /// server that dribbles one block per connection would reconnect forever,
    /// because every attempt would look like progress.
    static constexpr int kMaxTotalAttempts = 10;

    explicit HttpCacheClient(QObject* parent = nullptr);
    ~HttpCacheClient() override;

    /// Bind this client to an offer and a target file.
    ///
    /// @param offeringPeerHash     userhash of the peer that made the offer, used to
    ///                             send it the outcome even if it reconnects meanwhile.
    /// @param offeringPeerAddress  connect address of the same peer. This is what a
    ///                             corrupt part is blamed on — never this client's own
    ///                             address, which is the cache server's. See the note
    ///                             at the writeToBuffer() call in consumeStagedBlocks().
    /// @return false when the offer does not describe something we can use
    bool beginFetch(const HttpCacheOffer& offer, PartFile* file,
                    const std::array<uint8, 16>& offeringPeerHash,
                    const Address& offeringPeerAddress);

    [[nodiscard]] const HttpCacheOffer& offer() const { return m_offer; }
    [[nodiscard]] const std::array<uint8, 16>& offeringPeerHash() const { return m_peerHash; }
    [[nodiscard]] const Address& offeringPeerAddress() const { return m_peerAddress; }
    [[nodiscard]] uint32 partIndex() const { return m_offer.partIndex; }
    [[nodiscard]] bool isFinished() const { return m_finished; }

    /// Plaintext bytes successfully written so far.
    [[nodiscard]] uint64 plainWritten() const { return m_plainWritten; }

    /// Ciphertext bytes hashed and decrypted so far — always block-aligned, and
    /// the offset a resumed request starts from.
    [[nodiscard]] uint64 cipherConsumed() const { return m_cipherConsumed; }

    /// Connection attempts made, including the first. Lets a test prove there is
    /// no runaway reconnect loop.
    [[nodiscard]] int attemptCount() const { return m_totalAttempts; }

    /// Test seam: collapse the backoff table to a fixed delay, so a resume test
    /// need not wait out 21 s of production backoff. Negative = the real table.
    void setResumeDelayOverrideMsForTest(int milliseconds) { m_resumeDelayOverrideMs = milliseconds; }

    // -- URLClient overrides -------------------------------------------------

    bool sendHttpBlockRequests() override;
    bool processHttpDownResponse(const QList<QByteArray>& headers) override;
    bool processHttpDownResponseBody(const uint8* data, uint32 size) override;
    bool disconnected(const QString& reason, bool fromSocket = false) override;
    void sendCancelTransfer() override;

    /// "Downloading (HTTP Cache)" and friends, so the transfer list shows at a
    /// glance that these bytes are not costing the uploader anything.
    [[nodiscard]] QString downloadStateDisplayString() const override;

signals:
    /// Emitted exactly once per client, whatever the outcome. The manager sends
    /// the peer an HCOP_RESULT and drops this object on it.
    void fetchFinished(HttpCacheClient* self, HttpCacheResult result, uint64 bytesFetched);

private:
    // Private helpers after the public interface, per the house style.

    void finish(HttpCacheResult result);
    void releaseReservation();

    /// Is this fetch still worth finishing at all — feature on, file alive, part
    /// still missing, offer not about to lapse? Says nothing about the budget.
    [[nodiscard]] bool fetchStillWanted() const;

    /// fetchStillWanted() and the retry budget still has room.
    [[nodiscard]] bool canResume() const;

    /// Queue the next attempt. Deferred on purpose — see the comment at the call
    /// site in disconnected().
    void scheduleResume();

    /// Take every whole block out of m_stage: hash it, decrypt it, write the
    /// plaintext. Returns false when the stream must be abandoned.
    bool consumeStagedBlocks();

    /// Everything received so far is worthless (the server ignored our Range):
    /// rewind the digest, the decryptor and both counters back to zero.
    bool restartFromZero();

    /// Final SHA-256, PKCS#7 and length checks once the last block is in.
    void verifyComplete();

    /// Parse "bytes <first>-<last>/<total>" (RFC 9110 §14.4). False when the
    /// header is absent, malformed, or the unsatisfied "bytes */<total>" form.
    [[nodiscard]] static bool parseContentRange(const QByteArray& value, uint64& first,
                                                uint64& last, uint64& total);

    /// Byte offset of this part within the file.
    [[nodiscard]] uint64 partStart() const;

    /// PKCS#7 pad length this offer implies. Pinned to [1, kAesBlockSize] by
    /// HttpCacheOffer::malformedReason(), which is checked before we get here.
    [[nodiscard]] uint64 padLength() const { return m_offer.cipherLength - m_offer.plainLength; }

    HttpCacheOffer m_offer;
    std::array<uint8, 16> m_peerHash{};
    Address m_peerAddress;

    AesCbcDecryptor m_decryptor;
    QCryptographicHash m_cipherHash{QCryptographicHash::Sha256};

    /// Ciphertext bytes hashed *and* decrypted. Always a multiple of
    /// kAesBlockSize, and therefore a legal CBC restart point.
    uint64 m_cipherConsumed = 0;

    /// Ciphertext tail shorter than one block, not yet hashed or decrypted.
    /// Dropped on a disconnect: the resumed request refetches it.
    QByteArray m_stage;

    /// The last full ciphertext block fed to the decryptor — the CBC chaining
    /// value for m_cipherConsumed. Kept rather than refetched: re-requesting it
    /// would feed those 16 bytes to the digest twice.
    QByteArray m_chain;

    /// Plaintext produced past plainLength, i.e. the PKCS#7 block. Verified at
    /// the end and never written to the file.
    QByteArray m_pad;

    uint64 m_plainWritten = 0;     ///< plaintext bytes handed to the part file
    bool m_headersOk = false;
    bool m_finished = false;

    /// A GET is outstanding on the current connection. One request covers the
    /// whole part here, so the base class's "ask for the next block" calls must
    /// not put a second one on the wire — see sendHttpBlockRequests().
    bool m_requestInFlight = false;

    /// Where the current response's body is expected to start, for validating
    /// Content-Range against what we actually asked for.
    uint64 m_expectedBodyStart = 0;

    int m_consecutiveRetries = 0;
    int m_totalAttempts = 0;
    uint64 m_progressAtAttemptStart = 0;
    int m_resumeDelayOverrideMs = -1;

    /// Gaps of this part claimed for the duration of the fetch, so ed2k sources
    /// do not race us for the same bytes. Released in finish().
    std::vector<Requested_Block_Struct*> m_reserved;
};

} // namespace eMule
