#include "pch.h"
#include "net/I2pdInstaller.h"

#include "archive/ArchiveReader.h"
#include "net/GuardedNetworkAccessManager.h"
#include "net/HttpStreamDownload.h"
#include "net/I2pdRelease.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <algorithm>

namespace eMule {

namespace {

const QString kStampName = QStringLiteral(".release");

QString readStamp(const QString& dir)
{
    QFile f(QDir(dir).filePath(kStampName));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

} // namespace

I2pdInstaller::I2pdInstaller(QObject* parent)
    : QObject(parent)
{
}

I2pdInstaller::~I2pdInstaller()
{
    abort();
}

I2pdInstaller::Asset I2pdInstaller::pinnedAsset()
{
    QString name;
    QString sha;
#if defined(Q_OS_MACOS)
    name = QStringLiteral("i2pd-macos-universal.tar.gz");
    sha = QLatin1StringView(i2pd_release::kMacUniversal);
#elif defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    name = QStringLiteral("i2pd-windows-x64.zip");
    sha = QLatin1StringView(i2pd_release::kWindowsX64);
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    name = QStringLiteral("i2pd-linux-x86_64.tar.gz");
    sha = QLatin1StringView(i2pd_release::kLinuxX64);
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_ARM_64)
    name = QStringLiteral("i2pd-linux-aarch64.tar.gz");
    sha = QLatin1StringView(i2pd_release::kLinuxArm64);
#endif
    if (name.isEmpty() || sha.size() != 64)
        return {};
    const QString tag = QLatin1StringView(i2pd_release::kTag);
    return {name, sha,
            QUrl(QStringLiteral("https://github.com/%1/releases/download/%2/%3")
                     .arg(QLatin1StringView(i2pd_release::kRepo), tag, name)),
            tag + QLatin1Char(' ') + sha};
}

QString I2pdInstaller::binaryName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("emuleqt-i2pd.exe");
#else
    return QStringLiteral("emuleqt-i2pd");
#endif
}

void I2pdInstaller::setDir(const QString& dir)
{
    if (!running())
        m_dir = dir;
}

QString I2pdInstaller::binary() const
{
    if (m_dir.isEmpty())
        return {};
    const QString p = QDir(m_dir).filePath(binaryName());
    return QFileInfo(p).isFile() ? p : QString();
}

bool I2pdInstaller::current() const
{
    const Asset a = pinnedAsset();
    return !a.name.isEmpty() && !binary().isEmpty() && readStamp(m_dir) == a.release;
}

bool I2pdInstaller::install()
{
    return install(pinnedAsset());
}

bool I2pdInstaller::install(const Asset& asset)
{
    if (running() || asset.name.isEmpty() || m_dir.isEmpty())
        return false;
    m_asset = asset;
    m_error.clear();
    m_progress = 0;
    const QString parent = QFileInfo(m_dir).absolutePath();
    if (!QDir().mkpath(parent)) {
        done(false, QStringLiteral("%1 not writable").arg(parent));
        return true;
    }
    if (!m_nam)
        m_nam = std::make_unique<GuardedNetworkAccessManager>();
    m_dl = std::make_unique<HttpStreamDownload>(m_nam.get());
    connect(m_dl.get(), &HttpStreamDownload::progress, this, [this](qint64 got, qint64 total) {
        const int pct = total > 0 ? int(std::min<qint64>(100, got * 100 / total)) : 0;
        if (pct != m_progress) {
            m_progress = pct;
            emit progressChanged(pct);
        }
    });
    connect(m_dl.get(), &HttpStreamDownload::finished, this, &I2pdInstaller::onDownloaded);
    const QString archive = QDir(parent).filePath(asset.name);
    logTorrent(QStringLiteral("I2P: downloading the router %1").arg(asset.url.toString()));
    if (!m_dl->start(asset.url, archive, {kMaxArchive, asset.sha256, 0})) {
        const QString err = m_dl->errorString();
        m_dl.reset();
        done(false, err.isEmpty() ? QStringLiteral("cannot write %1").arg(archive) : err);
    }
    return true;
}

void I2pdInstaller::abort()
{
    if (m_dl) {
        m_dl->disconnect(this);
        m_dl->abort();
        m_dl.reset();
    }
    if (m_unpackThread) {
        // a few MB in memory: short
        QThread* t = m_unpackThread;
        m_unpackThread = nullptr;
        t->disconnect(this);
        t->wait();
        delete t;
        QDir(tmpDir()).removeRecursively();
        QFile::remove(QDir(QFileInfo(m_dir).absolutePath()).filePath(m_asset.name));
    }
}

bool I2pdInstaller::running() const
{
    return m_dl || m_unpackThread;
}

void I2pdInstaller::onDownloaded(bool ok)
{
    if (!m_dl)
        return;
    const auto err = m_dl->error();
    const QString errText = m_dl->errorString();
    m_dl.release()->deleteLater();   // inside its signal
    if (!ok) {
        done(false, err == HttpStreamDownload::Error::HashMismatch
                        ? QStringLiteral("the download is damaged (SHA-256 mismatch)")
                        : errText);
        return;
    }
    unpack(QDir(QFileInfo(m_dir).absolutePath()).filePath(m_asset.name));
}

void I2pdInstaller::unpack(const QString& archive)
{
    const QString tmp = tmpDir();
    QDir(tmp).removeRecursively();
    auto error = std::make_shared<QString>();
    // libarchive in-process on every OS: no tar.exe on old Windows, and a GNU tar from
    // Git/MSYS on PATH reads no zip
    QThread* t = QThread::create([archive, tmp, error] {
        ArchiveReader reader;
        if (!reader.open(archive) || !reader.extractAll(tmp))
            *error = reader.lastError().isEmpty() ? QStringLiteral("unreadable archive") : reader.lastError();
    });
    m_unpackThread = t;
    connect(t, &QThread::finished, this, [this, t, archive, error] {
        if (m_unpackThread != t)
            return;
        m_unpackThread = nullptr;
        t->deleteLater();
        unpackDone(archive, *error);
    });
    t->start();
}

void I2pdInstaller::unpackDone(const QString& archive, const QString& error)
{
    QFile::remove(archive);
    const QString tmp = tmpDir();
    const QString program = QDir(tmp).filePath(binaryName());
    if (error.isEmpty() && !QFileInfo(program).isFile()) {
        QDir(tmp).removeRecursively();
        done(false, QStringLiteral("the archive has no %1").arg(binaryName()));
        return;
    }
    if (!error.isEmpty()) {
        QDir(tmp).removeRecursively();
        done(false, QStringLiteral("cannot unpack %1: %2").arg(m_asset.name, error));
        return;
    }
    // ArchiveReader keeps no modes
    QFile::setPermissions(program, QFile::permissions(program) | QFileDevice::ExeOwner | QFileDevice::ReadOwner
                                       | QFileDevice::ExeGroup | QFileDevice::ReadGroup);
    {
        QFile stamp(QDir(tmp).filePath(kStampName));
        if (stamp.open(QIODevice::WriteOnly))
            stamp.write(m_asset.release.toUtf8() + '\n');
    }
    // the old one aside first: a failed swap keeps it
    const QString old = m_dir + QStringLiteral(".old");
    QDir(old).removeRecursively();
    if (QFileInfo::exists(m_dir) && !QDir().rename(m_dir, old)) {
        QDir(tmp).removeRecursively();
        done(false, QStringLiteral("cannot replace %1 (in use?)").arg(m_dir));
        return;
    }
    if (!QDir().rename(tmp, m_dir)) {
        QDir().rename(old, m_dir);
        QDir(tmp).removeRecursively();
        done(false, QStringLiteral("cannot move the router to %1").arg(m_dir));
        return;
    }
    QDir(old).removeRecursively();
    logTorrent(QStringLiteral("I2P: router %1 installed in %2").arg(QLatin1StringView(i2pd_release::kTag), m_dir));
    done(true);
}

void I2pdInstaller::done(bool ok, const QString& error)
{
    m_error = error;
    m_progress = ok ? 100 : 0;
    // queued: install() may fail before its caller connected or returned
    QMetaObject::invokeMethod(this, [this, ok] { emit finished(ok); }, Qt::QueuedConnection);
}

} // namespace eMule
