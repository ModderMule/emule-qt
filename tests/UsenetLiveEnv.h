#pragma once

/// @file UsenetLiveEnv.h
/// @brief The environment every live Usenet test shares: a provider, a
///        directory of .nzb files, and a Preferences redirect that survives a
///        failure.
///
/// Credentials never come from the repository and never from the user's real
/// preferences.yml — only from the environment, with the gitignored project
/// `.env` filling in whatever the process did not already set
/// (`TestHelpers.h::loadProjectEnv`). Unset, every case skips: an unconfigured
/// checkout must not look broken.
///
///   EMULE_NNTP_HOST     news.example.com
///   EMULE_NNTP_PORT     563                     (optional)
///   EMULE_NNTP_TLS      implicit|starttls|none  (optional, default implicit)
///   EMULE_NNTP_USER     (optional)
///   EMULE_NNTP_PASS     (optional)
///   EMULE_NNTP_MAXCONN  (optional, default 8)   — never exceed what the
///                       provider sold you; an over-subscribed account gets
///                       suspended, not throttled.
///   EMULE_NZB_DIR       a directory of .nzb files; every one becomes a row.
///                       Any .xml in it is read as a recorded indexer feed --
///                       see feedFilesInDir(), which needs no provider at all.
///   EMULE_NZB_MAX_MB    skip a release larger than this (optional)
///
/// This header used to be three copies of `providerFromEnv()` living in
/// separate test files.

#include "TestHelpers.h"

#include "nntp/NewsServer.h"
#include "prefs/Preferences.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QString>
#include <QStringList>
#include <QTest>

namespace eMule::testing::usenet {

using eMule::usenet::NewsServer;
using eMule::usenet::TlsMode;

/// One environment variable, trimmed. Trimming matters: a value that reached us
/// through a `.env` line keeps whatever spacing the file had.
inline QString liveEnv(const char* name)
{
    return qEnvironmentVariable(name).trimmed();
}

/// A provider is configured. Everything else is optional — an open server needs
/// no user and no password.
inline bool haveLiveProvider()
{
    return !liveEnv("EMULE_NNTP_HOST").isEmpty();
}

inline NewsServer providerFromEnv()
{
    NewsServer s;
    s.name = QStringLiteral("live");
    s.host = liveEnv("EMULE_NNTP_HOST");
    s.user = liveEnv("EMULE_NNTP_USER");
    s.pass = liveEnv("EMULE_NNTP_PASS");

    const QString mode = liveEnv("EMULE_NNTP_TLS").toLower();
    if (mode == QLatin1String("none"))
        s.tlsMode = TlsMode::None;
    else if (mode == QLatin1String("starttls"))
        s.tlsMode = TlsMode::StartTls;
    else
        s.tlsMode = TlsMode::Implicit;

    const int port = liveEnv("EMULE_NNTP_PORT").toInt();
    s.port = port > 0 ? static_cast<quint16>(port)
                      : (s.tlsMode == TlsMode::Implicit ? eMule::usenet::kDefaultNntpTlsPort
                                                        : eMule::usenet::kDefaultNntpPort);

    const int maxConn = liveEnv("EMULE_NNTP_MAXCONN").toInt();
    if (maxConn > 0)
        s.maxConnections = maxConn;

    return s;
}

/// The directory named by EMULE_NZB_DIR, or empty.
inline QString nzbDir()
{
    return liveEnv("EMULE_NZB_DIR");
}

/// Every .nzb in it, by name. Empty when the variable is unset or the directory
/// does not exist — the caller skips, it does not fail.
inline QFileInfoList nzbFilesInDir()
{
    const QString dir = nzbDir();
    if (dir.isEmpty() || !QDir(dir).exists())
        return {};

    return QDir(dir).entryInfoList({QStringLiteral("*.nzb")}, QDir::Files, QDir::Name);
}

/// One QTest data row per .nzb, named after the file so a failure says which
/// release broke. Returns how many rows were added.
inline int addNzbRows()
{
    const QFileInfoList files = nzbFilesInDir();
    for (const QFileInfo& fi : files)
        QTest::newRow(qPrintable(fi.completeBaseName())) << fi.absoluteFilePath();

    return int(files.size());
}

/// Every .xml in EMULE_NZB_DIR, by name -- a recorded newznab/torznab response
/// saved next to the releases it came from. Empty when the variable is unset or
/// the directory does not exist; the caller skips, it does not fail.
inline QFileInfoList feedFilesInDir()
{
    const QString dir = nzbDir();
    if (dir.isEmpty() || !QDir(dir).exists())
        return {};

    return QDir(dir).entryInfoList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name);
}

/// One QTest data row per recorded feed, named after the file. Returns how many
/// rows were added. Mirrors addNzbRows().
inline int addFeedRows()
{
    const QFileInfoList files = feedFilesInDir();
    for (const QFileInfo& fi : files)
        QTest::newRow(qPrintable(fi.completeBaseName())) << fi.absoluteFilePath();

    return int(files.size());
}

/// EMULE_NZB_MAX_MB, or 0 for "no cap".
inline qint64 nzbSizeCapBytes()
{
    const qint64 mb = liveEnv("EMULE_NZB_MAX_MB").toLongLong();
    return mb > 0 ? mb * 1024 * 1024 : 0;
}

/// Point Preferences at a throwaway tree, and put it back afterwards.
///
/// Unlike `useTempPrefs()` in UsenetPostingHarness.h, this restores what it
/// found. A live test runs on a machine with a real configuration behind it, and
/// leaving `thePrefs` pointing into a deleted QTemporaryDir would poison every
/// later case in the same binary.
class LivePrefsGuard {
public:
    explicit LivePrefsGuard(const TempDir& tmp)
        : m_configDir(thePrefs.configDir())
        , m_tempDirs(thePrefs.tempDirs())
        , m_incomingDir(thePrefs.incomingDir())
        , m_sharedDirs(thePrefs.sharedDirs())
    {
        thePrefs.setConfigDir(tmp.path());
        thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
        thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
        thePrefs.setSharedDirs({});
    }

    ~LivePrefsGuard()
    {
        thePrefs.setConfigDir(m_configDir);
        thePrefs.setTempDirs(m_tempDirs);
        thePrefs.setIncomingDir(m_incomingDir);
        thePrefs.setSharedDirs(m_sharedDirs);
    }

    LivePrefsGuard(const LivePrefsGuard&) = delete;
    LivePrefsGuard& operator=(const LivePrefsGuard&) = delete;

private:
    QString     m_configDir;
    QStringList m_tempDirs;
    QString     m_incomingDir;
    QStringList m_sharedDirs;
};

} // namespace eMule::testing::usenet
