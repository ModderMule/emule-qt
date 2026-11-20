#include "pch.h"
/// @file ShareableFile.cpp
/// @brief Shareable file base class — port of MFC CShareableFile.

#include "files/ShareableFile.h"

#include "prefs/Preferences.h"
#include "utils/Opcodes.h"
#include "utils/Log.h"

namespace eMule {

ShareableFile::ShareableFile() = default;

void ShareableFile::updateFileRatingCommentAvail(bool /*forceUpdate*/)
{
    // Default no-op — subclasses (KnownFile) override when Kademlia is available.
}

void ShareableFile::setFileName(const QString& name,
                                bool replaceInvalidChars,
                                bool autoSetFileType,
                                bool removeControlChars)
{
    const QString oldName = fileName();
    AbstractFile::setFileName(name, replaceInvalidChars, autoSetFileType, removeControlChars);
    // Renamed from .avi to .ogg: the old "only the extension is wrong" no longer holds (#95)
    if (fileName() != oldName)
        m_containerCheck.reset();
}

QString ShareableFile::displayFileType() const
{
    const QString type = fileType();
    if (type == QLatin1StringView(ED2KFTSTR_AUDIO) && containerCheckIfResolved().hasVideo)
        return QStringLiteral(ED2KFTSTR_VIDEO);
    return type;
}

const QString& ShareableFile::sharedDirectory() const
{
    return m_sharedDirectory.isEmpty() ? m_directory : m_sharedDirectory;
}

const ContainerCheck& ShareableFile::containerCheck() const
{
    static const ContainerCheck kUnchecked;
    if (settleContainerCheckByName())
        return *m_containerCheck;

    // Deliberately not cached when unreadable. A part file whose first chunk has
    // not landed yet has told us nothing, and it will have something to say later.
    const auto src = containerHeadSource();
    if (!src)
        return kUnchecked;
    const auto head = readContainerHead(*src);
    if (!head)
        return kUnchecked;
    applyContainerHead(*head);
    return *m_containerCheck;
}

bool ShareableFile::settleContainerCheckByName() const
{
    if (m_containerCheck)
        return true;
    // The cheap half: an extension we hold no promise over is never worth an open,
    // and most of a shared folder is exactly that.
    if (expectedContainer(QFileInfo(fileName()).suffix().toLower()).isEmpty()) {
        m_containerCheck.emplace();
        return true;
    }
    return false;
}

std::optional<ShareableFile::ContainerHeadSource> ShareableFile::containerHeadSource() const
{
    return ContainerHeadSource{m_filePath, kContainerProbeBytes};
}

std::optional<QByteArray> ShareableFile::readContainerHead(const ContainerHeadSource& src)
{
    QFile file(src.path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.read(src.bytes);
}

void ShareableFile::applyContainerHead(const QByteArray& head) const
{
    m_containerCheck = checkHead(head, fileName());
    // Once per file: memoised from here on, and kept in known.met (FT_CONTAINERCHECK)
    // so a shared file is only read again after a rename or a sniffer version bump.
    // Shared files logged only on request.
    if (m_containerCheck->isSuspect() && (isPartFile() || thePrefs.logFakeCheckShared()))
        logWarning(QStringLiteral("File header check: %1 — %2")
                       .arg(fileName(), containerWarningText(*m_containerCheck, fileName())));
}

const ContainerCheck& ShareableFile::containerCheckIfResolved() const
{
    static const ContainerCheck kUnchecked;
    return m_containerCheck ? *m_containerCheck : kUnchecked;
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
