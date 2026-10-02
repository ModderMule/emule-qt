#pragma once

/// @file ArticleFileCache.h
/// @brief One open handle per output file, shared by a worker's in-flight articles.
///
/// A worker runs ~a dozen connections, mostly on consecutive segments of the
/// same file, and a pipelined follower overlaps the article ahead of it. Each
/// article used to mkpath() the directory and open, flush and close the file.
/// Now the first article opens it and the rest share the handle; it closes when
/// the last ArticleWriter on it lets go, exactly when it used to, so sealFile()'s
/// rename never meets a handle this cache still holds.
///
/// Single-threaded: owned by one UsenetWorker and used only on its thread.

#include <QHash>
#include <QString>

#include <memory>

class QFile;

namespace eMule::usenet {

class ArticleFileCache {
public:
    /// The shared handle for @p path, opened ReadWrite and unbuffered (writers
    /// position themselves per write). The directory is created only when the
    /// open fails for want of it — a user may delete the temp tree mid-run.
    [[nodiscard]] std::shared_ptr<QFile> acquire(const QString& path, QString& error);

    /// Handles currently open. For tests.
    [[nodiscard]] int openCount() const;

    /// Open @p path the way acquire() does, without sharing.
    [[nodiscard]] static std::shared_ptr<QFile> openFile(const QString& path, QString& error);

private:
    QHash<QString, std::weak_ptr<QFile>> m_files;
};

} // namespace eMule::usenet
