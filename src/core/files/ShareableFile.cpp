#include "pch.h"
/// @file ShareableFile.cpp
/// @brief Shareable file base class — port of MFC CShareableFile.

#include "files/ShareableFile.h"

namespace eMule {

ShareableFile::ShareableFile() = default;

void ShareableFile::updateFileRatingCommentAvail(bool /*forceUpdate*/)
{
    // Default no-op — subclasses (KnownFile) override when Kademlia is available.
}

const QString& ShareableFile::sharedDirectory() const
{
    return m_sharedDirectory.isEmpty() ? m_directory : m_sharedDirectory;
}

QString ShareableFile::infoSummary(bool /*noFormatCommands*/) const
{
    QString summary;
    summary += QStringLiteral("Filename: %1\n").arg(fileName());
    summary += QStringLiteral("File size: %1 bytes\n").arg(static_cast<uint64>(fileSize()));
    if (!fileType().isEmpty())
        summary += QStringLiteral("File type: %1\n").arg(fileType());
    if (!m_directory.isEmpty())
        summary += QStringLiteral("Folder: %1\n").arg(m_directory);
    return summary;
}

} // namespace eMule
