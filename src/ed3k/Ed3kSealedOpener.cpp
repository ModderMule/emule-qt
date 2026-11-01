/// @file Ed3kSealedOpener.cpp
/// @brief Sealed set members opened into plain files — implementation.

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

struct Ed3kSealedOpener::Job {
    Job(const QString& s, const QString& p, const ffi::Key32& k) : sealed(s), plain(p), key(k), in(s), out(p + QStringLiteral(".part")) {}

    QString sealed;
    QString plain;
    Secret key;
    std::array<std::uint8_t, 12> nonce{};
    QFile in;
    QFile out;
    bool begun = false;
    std::uint64_t offset = 0;   ///< plain bytes written
    std::uint64_t size = 0;     ///< plain size
};

Ed3kSealedOpener::Ed3kSealedOpener(QObject* parent)
    : QObject(parent)
{
}

Ed3kSealedOpener::~Ed3kSealedOpener()
{
    // half-done ones: their .part goes, a later run begins anew
    for (auto& j : m_jobs) {
        j->out.close();
        QFile::remove(j->out.fileName());
    }
}

bool Ed3kSealedOpener::open(const QString& sealedPath, const QString& plainPath, const ffi::Key32& kData)
{
    if (busy(plainPath) || QFileInfo::exists(plainPath))
        return false;
    m_jobs.push_back(std::make_unique<Job>(sealedPath, plainPath, kData));
    schedule();
    return true;
}

bool Ed3kSealedOpener::busy(const QString& plainPath) const
{
    return std::ranges::any_of(m_jobs, [&](const auto& j) { return j->plain == plainPath; });
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kSealedOpener::schedule()
{
    if (m_scheduled || m_jobs.empty())
        return;
    m_scheduled = true;
    QTimer::singleShot(0, this, &Ed3kSealedOpener::step);
}

void Ed3kSealedOpener::step()
{
    m_scheduled = false;
    if (m_jobs.empty())
        return;
    Job& job = *m_jobs.front();
    if (!run(job)) {
        const QString plain = job.plain;
        const bool ok = !QFileInfo::exists(job.out.fileName()) && QFileInfo::exists(plain);
        m_jobs.pop_front();
        emit opened(plain, ok);
    }
    schedule();
}

bool Ed3kSealedOpener::run(Job& job)
{
    const auto fail = [&job](const char* why) {
        logEd3kWarning(QStringLiteral("ed3k: a sealed member was not opened (%1): %2").arg(QLatin1String(why), job.plain));
        job.in.close();
        job.out.close();
        QFile::remove(job.out.fileName());
        return false;
    };
    if (!job.begun) {
        job.begun = true;
        if (!job.in.open(QIODevice::ReadOnly) || job.in.size() < kNonce)
            return fail("unreadable or short");
        if (job.in.read(reinterpret_cast<char*>(job.nonce.data()), kNonce) != kNonce)
            return fail("unreadable");
        job.size = std::uint64_t(job.in.size() - kNonce);
        QDir().mkpath(QFileInfo(job.plain).absolutePath());
        if (!job.out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail("not writable");
    }
    const qint64 want = qint64(std::min<std::uint64_t>(std::uint64_t(kChunk), job.size - job.offset));
    if (want > 0) {
        QByteArray buf(want, Qt::Uninitialized);
        if (job.in.read(buf.data(), want) != want)
            return fail("short read");
        std::span<std::byte> view(reinterpret_cast<std::byte*>(buf.data()), std::size_t(buf.size()));
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
    // never over a file the user has (file-set.md §5.3)
    if (QFileInfo::exists(job.plain) || !QFile::rename(job.out.fileName(), job.plain))
        return fail("the plain file is there");
    logEd3kDebug(QStringLiteral("ed3k: a sealed member opened: %1").arg(job.plain));
    return false;
}

} // namespace eMule::ed3k
