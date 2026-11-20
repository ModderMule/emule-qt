#pragma once

/// @file Par2LocalFill.h
/// @brief #224 stored archives: the volumes of a release filled from files on disk here.
///
/// A stored (-m0) RAR volume holds its entries' bytes as they are. When those entries are on
/// disk here already (ed3k's contents record names them by root, eMule Qt has the roots),
/// par2's rolling scan finds every block of the volumes that lies in them
/// (Par2Verifier::locate). Those bytes are copied into the volumes' part files and their
/// articles need not be fetched; the headers come from Usenet or the recovery blocks. The
/// copy is checked by par2's MD5 of each block, and the release is verified as always.

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>

namespace eMule::usenet {

namespace Par2LocalFill {

/// Bytes written into one file of the set.
struct Range {
    QString fileName;   ///< as the set names it
    qint64 offset = 0;
    qint64 length = 0;
};

/// Locates the blocks of the set of @p par2Path in @p extraFiles and writes each into the
/// file @p targets names for its set file (name lower case → path; others left out). A
/// target is made when missing (its folder must be there). Adjacent blocks come back as one
/// range per file, in order. Blocking: a worker. Empty: nothing found or written.
[[nodiscard]] QList<Range> fill(const QString& par2Path, const QStringList& extraFiles,
                                const QHash<QString, QString>& targets, const std::atomic<bool>* cancel = nullptr);

} // namespace Par2LocalFill

} // namespace eMule::usenet
