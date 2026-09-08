#pragma once

/// @file ShareableFile.h
/// @brief Shareable file base class — replaces MFC CShareableFile.
///
/// Extends AbstractFile with directory path, shared directory, and
/// verified file type information. Base class for KnownFile.

#include "files/AbstractFile.h"
#include "media/ContainerSniffer.h"
#include "utils/OtherFunctions.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace eMule {

class ShareableFile : public AbstractFile {
public:
    ShareableFile();

    void updateFileRatingCommentAvail(bool forceUpdate = false) override;

    [[nodiscard]] FileType verifiedFileType() const { return m_verifiedFileType; }
    void setVerifiedFileType(FileType type) { m_verifiedFileType = type; }

    [[nodiscard]] const QString& path() const { return m_directory; }
    void setPath(const QString& path) { m_directory = path; }

    [[nodiscard]] const QString& sharedDirectory() const;
    void setSharedDirectory(const QString& dir) { m_sharedDirectory = dir; }
    [[nodiscard]] bool isShellLinked() const { return !m_sharedDirectory.isEmpty(); }

    [[nodiscard]] const QString& filePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }

    [[nodiscard]] virtual QString infoSummary(bool noFormatCommands = false) const;

    /// Whether the first bytes are the container the name claims. Computed once
    /// and kept -- the answer cannot change for bytes already on disk, and the
    /// download list asks for it on every poll.
    ///
    /// Not thread-safe by design: the IPC handler and the web routes both call
    /// this on the daemon's main thread, and nothing else needs it.
    [[nodiscard]] const ContainerCheck& containerCheck() const;

    /// The verdict if something already looked, without ever touching the disk.
    /// Unchecked until then.
    ///
    /// This is what the list payloads use. handleGetSharedFiles() walks the whole
    /// share on every poll, so it may not open files; SharedFileList's sweep
    /// (warmContainerChecks) does the reading, off the poll path.
    [[nodiscard]] const ContainerCheck& containerCheckIfResolved() const;

    /// Whether containerCheck() has settled on an answer. False both before the
    /// first look and while the bytes are still unreadable.
    [[nodiscard]] bool containerCheckResolved() const { return m_containerCheck.has_value(); }

protected:
    /// The first kContainerHeadBytes to judge the name against. False when they
    /// are not available yet, which is not a verdict -- ask again later.
    [[nodiscard]] virtual bool readContainerHead(QByteArray& head) const;

    QString m_directory;
    QString m_filePath;
    QString m_sharedDirectory;
    FileType m_verifiedFileType = FileType::Unknown;

    // Unset until the bytes were readable; see containerCheck().
    mutable std::optional<ContainerCheck> m_containerCheck;
};

} // namespace eMule
