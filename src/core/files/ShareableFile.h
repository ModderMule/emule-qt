#pragma once

/// @file ShareableFile.h
/// @brief Shareable file base class — replaces MFC CShareableFile.
///
/// Extends AbstractFile with directory path, shared directory, and
/// verified file type information. Base class for KnownFile.

#include "files/AbstractFile.h"
#include "utils/OtherFunctions.h"

#include <QString>

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

protected:
    QString m_directory;
    QString m_filePath;
    QString m_sharedDirectory;
    FileType m_verifiedFileType = FileType::Unknown;
};

} // namespace eMule
