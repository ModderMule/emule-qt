#pragma once

/// @file Par2NameIndex.h
/// @brief Matching a file on disk to the name a PAR2 set gives it.
///
/// The point of this existing at all is *when* it can answer. PAR2 identifies a
/// file by its length plus the MD5 of its first 16 KiB, so a release can be
/// named while it is still downloading — long before par2 itself could verify
/// anything, and long before post-processing, by which time every decision the
/// name could have informed (which volume opens the set, which file the seek map
/// walks, what the GUI shows) has already been made wrongly.
///
/// Deliberately free of par2 headers: it works on Par2SetFile values and
/// QCryptographicHash, so it compiles in an EMULE_HAVE_PAR2=OFF build and needs
/// none of the -fno-rtti care Par2Verifier.cpp does.

#include "post/Par2Verifier.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

namespace eMule::usenet {

class Par2NameIndex {
public:
    /// Take the recovery set's file list. Non-recoverable entries are ignored:
    /// the set only lists those, it does not describe them.
    void setFiles(const QList<Par2SetFile>& files);

    [[nodiscard]] bool isEmpty() const { return m_byKey.isEmpty(); }
    [[nodiscard]] int size() const { return int(m_byKey.size()); }

    /// How many bytes of a file must be readable from offset 0 before match()
    /// can answer for it.
    [[nodiscard]] static qint64 bytesNeededFor(qint64 declaredSize);

    /// The set entry of this length whose first 16 KiB hash to @p hash16k.
    ///
    /// Empty on **two** matches as well as none. A set can legitimately hold two
    /// byte-identical files — two copies of an .nfo, say — under different
    /// names, and guessing between them swaps them. Empty is also what an
    /// uncovered file gets, which is normal: a par2 set covers the archive
    /// volumes, not the sample, the .nfo or itself.
    [[nodiscard]] QString match(qint64 size, const QByteArray& hash16k) const;

    /// match() over the first bytes of @p path.
    ///
    /// @p declaredSize is yEnc's `=ybegin size=`, never QFileInfo::size(): the
    /// scratch file is written at absolute offsets and its length on disk is the
    /// highest byte written so far, not how much of it is real.
    [[nodiscard]] QString matchFile(const QString& path, qint64 declaredSize) const;

    /// Take @p name so no second file can claim it. False when it is already
    /// taken. Claiming happens in NZB file order, so a restart reaches the same
    /// answer as the first run did.
    bool claim(const QString& name);

    [[nodiscard]] bool isClaimed(const QString& name) const { return m_claimed.contains(name); }

private:
    /// (size, hash16k) -> name, or an empty name where two entries collide.
    ///
    /// Size alone would be useless: every volume of a RAR set is the same length
    /// but the last, which is exactly why the hash has to be exposed at all.
    QHash<QByteArray, QString> m_byKey;
    QSet<QString> m_claimed;
};

} // namespace eMule::usenet
