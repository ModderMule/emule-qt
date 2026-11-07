/// @file Ed3kSealedOpener.cpp
/// @brief Sealed set members opened into plain files (and sealed, batch 15) — implementation.

#include "Ed3kSealedOpener.h"

#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

struct Ed3kSealedCipher::Job {
    Job(const QString& i, const QString& o, const ffi::Key32& k, const std::optional<Nonce>& n)
        : input(i), output(o), key(k), seal(n.has_value()), nonce(n.value_or(Nonce{})), in(i), out(o + QStringLiteral(".part"))
    {
    }

    QString input;
    QString output;
    Secret key;
    bool seal = false;          ///< plain → sealed (else sealed → plain)
    Nonce nonce{};
    QFile in;
    QFile out;
    bool begun = false;
    std::uint64_t offset = 0;   ///< plain bytes done
    std::uint64_t size = 0;     ///< plain size
};

Ed3kSealedCipher::Ed3kSealedCipher(QObject* parent)
    : QObject(parent)
{
}

Ed3kSealedCipher::~Ed3kSealedCipher()
{
    // half-done ones: their .part goes, a later run begins anew
    for (auto& j : m_jobs) {
        j->out.close();
        QFile::remove(j->out.fileName());
    }
}

bool Ed3kSealedCipher::busy(const QString& outPath) const
{
    return std::ranges::any_of(m_jobs, [&](const auto& j) { return j->output == outPath; });
}

bool Ed3kSealedCipher::enqueue(const QString& inPath, const QString& outPath, const ffi::Key32& kData, const std::optional<Nonce>& sealNonce)
{
    if (busy(outPath))
        return false;
    m_jobs.push_back(std::make_unique<Job>(inPath, outPath, kData, sealNonce));
    schedule();
    return true;
}

Ed3kSealedOpener::Ed3kSealedOpener(QObject* parent)
    : Ed3kSealedCipher(parent)
{
}

Ed3kSealedOpener::~Ed3kSealedOpener() = default;

bool Ed3kSealedOpener::open(const QString& sealedPath, const QString& plainPath, const ffi::Key32& kData)
{
    if (busy(plainPath) || QFileInfo::exists(plainPath))
        return false;
    return enqueue(sealedPath, plainPath, kData, std::nullopt);
}

Ed3kSealedSealer::Ed3kSealedSealer(QObject* parent)
    : Ed3kSealedCipher(parent)
{
}

Ed3kSealedSealer::~Ed3kSealedSealer() = default;

bool Ed3kSealedSealer::seal(const QString& plainPath, const QString& sealedPath, const ffi::Key32& kData, const Nonce& nonce)
{
    return enqueue(plainPath, sealedPath, kData, nonce);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kSealedCipher::schedule()
{
    if (m_scheduled || m_jobs.empty())
        return;
    m_scheduled = true;
    QTimer::singleShot(0, this, &Ed3kSealedCipher::step);
}

void Ed3kSealedCipher::step()
{
    m_scheduled = false;
    if (m_jobs.empty())
        return;
    Job& job = *m_jobs.front();
    if (!run(job)) {
        const QString output = job.output;
        const bool ok = !QFileInfo::exists(job.out.fileName()) && QFileInfo::exists(output);
        m_jobs.pop_front();
        jobDone(output, ok);
    }
    schedule();
}

bool Ed3kSealedCipher::run(Job& job)
{
    const auto fail = [&job](const char* why) {
        logEd3kWarning(QStringLiteral("ed3k: a sealed member was not %1 (%2): %3")
                           .arg(job.seal ? QStringLiteral("sealed") : QStringLiteral("opened"), QLatin1String(why), job.output));
        job.in.close();
        job.out.close();
        QFile::remove(job.out.fileName());
        return false;
    };
    if (!job.begun) {
        job.begun = true;
        if (job.seal) {
            if (!job.in.open(QIODevice::ReadOnly))
                return fail("unreadable");
            if (job.in.size() > kMaxPlain)
                return fail("too large");
            job.size = std::uint64_t(job.in.size());
        } else {
            if (!job.in.open(QIODevice::ReadOnly) || job.in.size() < kNonce)
                return fail("unreadable or short");
            if (job.in.read(reinterpret_cast<char*>(job.nonce.data()), kNonce) != kNonce)
                return fail("unreadable");
            job.size = std::uint64_t(job.in.size() - kNonce);
        }
        QDir().mkpath(QFileInfo(job.output).absolutePath());
        if (!job.out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail("not writable");
        // the sealed file begins with its nonce (§8.1)
        if (job.seal && job.out.write(reinterpret_cast<const char*>(job.nonce.data()), kNonce) != kNonce)
            return fail("write");
    }
    const qint64 want = qint64(std::min<std::uint64_t>(std::uint64_t(kChunk), job.size - job.offset));
    if (want > 0) {
        QByteArray buf(want, Qt::Uninitialized);
        if (job.in.read(buf.data(), want) != want)
            return fail("short read");
        std::span<std::byte> view(reinterpret_cast<std::byte*>(buf.data()), std::size_t(buf.size()));
        // ChaCha20 is its own inverse: the same call seals and opens
        if (!ffi::sealedReadAt(job.key.expose(), job.nonce, job.offset, view))
            return fail("cipher");
        const bool written = job.out.write(buf) == want;
        // the plain bytes of a paid member: not left in a freed buffer
        wipe(buf);
        if (!written)
            return fail("write");
        job.offset += std::uint64_t(want);
    }
    if (job.offset < job.size)
        return true;
    job.in.close();
    if (!job.out.flush())
        return fail("write");
    job.out.close();
    if (job.seal) {
        // the caller's own file: an older one goes
        if (QFileInfo::exists(job.output))
            QFile::remove(job.output);
        if (!QFile::rename(job.out.fileName(), job.output))
            return fail("not renamed");
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: a member sealed: %1").arg(job.output); });
        return false;
    }
    // never over a file the user has (file-set.md §5.3)
    if (QFileInfo::exists(job.output) || !QFile::rename(job.out.fileName(), job.output))
        return fail("the plain file is there");
    logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: a sealed member opened: %1").arg(job.output); });
    return false;
}

} // namespace eMule::ed3k
