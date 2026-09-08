#pragma once

/// @file ContainerSniffer.h
/// @brief Whether a media file's first bytes are the container its name claims.
///
/// An incoming folder off the ed2k network is full of files whose extension is a
/// lie. The cheap, provable part of that -- "a .wmv that does not start with the
/// ASF signature is not an ASF" -- is decided here, from twelve bytes, so both
/// front ends can say so where the file is listed rather than after a player has
/// already sat at 0:00.

#include <QByteArray>
#include <QString>

namespace eMule {

/// Enough for every signature below, incl. RIFF's form type at offset 8.
inline constexpr qint64 kContainerHeadBytes = 12;

enum class ContainerVerdict : quint8 {
    Unchecked,          ///< extension we make no promise about, or bytes not readable yet
    Matches,            ///< the normal case: it is what it says it is
    WrongContainer,     ///< an MP4 named .avi -- we know what it really is
    NoKnownContainer,   ///< the promised signature is absent and nothing else matches
};

struct ContainerCheck {
    ContainerVerdict verdict = ContainerVerdict::Unchecked;
    QString expected;   ///< container the extension promises, e.g. "ASF"
    QString actual;     ///< container the bytes are; empty for NoKnownContainer
    QString mimeType;   ///< MIME for `actual`; empty when we cannot name it

    /// True when the name is provably wrong. Note that Unchecked is not suspect:
    /// not knowing is not the same as knowing something is off.
    [[nodiscard]] bool isSuspect() const
    {
        return verdict == ContainerVerdict::WrongContainer
            || verdict == ContainerVerdict::NoKnownContainer;
    }
};

/// The container a file's first bytes actually are; empty when unrecognised.
[[nodiscard]] QString sniffContainer(const QByteArray& head);

/// The container an extension promises; empty when we promise nothing for it.
[[nodiscard]] QString expectedContainer(const QString& ext);

/// What to send for a container we identified from its bytes.
[[nodiscard]] QString containerMimeType(const QString& container);

/// Judge an already-read head against a name.
///
/// A head shorter than kContainerHeadBytes is not a free pass: if the extension
/// promises a signature, a file too short to carry it does not have it. Only an
/// extension we make no promise about comes back Unchecked.
[[nodiscard]] ContainerCheck checkHead(const QByteArray& head, const QString& fileName);

/// Same, reading the head from disk. Unreadable is Unchecked, not suspect --
/// a locked or vanished file has told us nothing about its contents.
[[nodiscard]] ContainerCheck checkFile(const QString& absPath, const QString& fileName);

/// One sentence saying why the file is not what its name claims; empty when it is
/// not suspect. Lives here rather than in each front end because every list that
/// draws the mark has to explain it -- the GUI tooltips, the web listings and the
/// web UI titles -- and three separately written copies had already drifted apart.
[[nodiscard]] QString containerWarningText(const ContainerCheck& check, const QString& fileName);

} // namespace eMule
