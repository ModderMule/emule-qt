#pragma once

/// @file Ed3kHosterUnpack.h
/// @brief Unpacking of finished hoster archives (Ed3kHosterJoin), one set at a time.
///
/// The archives wait in Temp (`hoster-archives/<package>/`); a set is extracted into a
/// scratch folder next to them, then moved into incoming/<package>/ under a staging
/// suffix the share scan skips, so no half-written file is ever hashed. Volume detection
/// and extraction are UsenetUnpacker's (libarchive, external tool for encrypted RAR).

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

namespace eMule::ed3k {

/// One archive set to unpack.
struct HosterUnpackJob {
    QString firstVolume;   ///< opens the set; also the job's key
    QString archiveDir;    ///< every volume of the set is here
    QSet<QString> skip;    ///< first volumes of the other sets in archiveDir
    QString destDir;       ///< incoming or incoming/<package>
    QString scratchDir;    ///< emptied first, removed after
    QString password;
    QStringList passwords; ///< tried in turn after password while the set wants one (a container's)
    QString externalTool;  ///< usenetExternalUnpacker; empty = libarchive only
    int nestedDepth = 0;   ///< archives inside archives, levels; 0 = off
    bool deleteNestedArchives = true;   ///< an opened inner archive is not published
};

/// What an unpack came to.
struct HosterUnpackResult {
    QString firstVolume;
    bool ok = false;
    bool passwordRequired = false;
    QString error;
    QStringList files;     ///< published files in destDir (subfolders kept)
    QStringList volumes;   ///< archive files consumed
};

class Ed3kHosterUnpack : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    /// Whether @p fileName is a volume of an archive this can unpack.
    [[nodiscard]] static bool isArchive(const QString& fileName);
    /// The key all volumes of one set share (lower case, without the volume suffix);
    /// empty when @p fileName is no archive.
    [[nodiscard]] static QString setKey(const QString& fileName);
    /// The sets in @p dir as {first volume, volumes, headless}.
    struct Set {
        QString firstVolume;
        QStringList volumes;
        bool headless = false;
    };
    [[nodiscard]] static QList<Set> setsIn(const QString& dir);

    [[nodiscard]] bool busy() const { return !m_running.isEmpty(); }
    [[nodiscard]] const QString& running() const { return m_running; }
    /// Runs @p job on the global pool; finished() follows on this object's thread.
    /// False: one runs already.
    bool start(const HosterUnpackJob& job);

    /// The blocking part (tests call it directly).
    [[nodiscard]] static HosterUnpackResult run(const HosterUnpackJob& job,
                                                const std::function<void(int)>& progress = {});

signals:
    void progress(const QString& firstVolume, int percent);
    void finished(const eMule::ed3k::HosterUnpackResult& result);

private:
    QString m_running;   ///< first volume of the running job
};

} // namespace eMule::ed3k
