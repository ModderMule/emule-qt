#pragma once

/// @file ArticleWriter.h
/// @brief Places decoded article bytes at absolute offsets in the target file.
///
/// Segments arrive out of order, and deliberately so: a yEnc part carries its
/// own `=ypart begin/end`, so it places itself with no knowledge of any other
/// part. That is what lets the scheduler fetch across many connections at once,
/// and later what makes streaming possible without re-architecting anything.
///
/// Sparse writes matter more here than they do for ED2K's chunk model, where
/// blocks are requested in order from a bounded window. On Usenet the first
/// article to arrive may belong at offset 800 MB.
///
/// Two file-system notes:
///   - `reserve()` sets the final size once, up front. Growing a file by
///     seeking past its end repeatedly is what fragments it badly.
///   - Given an ArticleFileCache, writers on the same file share one unbuffered
///     handle and each keeps its own position, so concurrent articles on one
///     file still land at their own offsets.

#include <QByteArrayView>
#include <QFile>
#include <QString>

#include <memory>

namespace eMule::usenet {

class ArticleFileCache;

class ArticleWriter {
public:
    /// @p cache may be null: the writer then opens a handle of its own.
    explicit ArticleWriter(ArticleFileCache* cache = nullptr) : m_cache(cache) {}
    ~ArticleWriter();

    ArticleWriter(const ArticleWriter&) = delete;
    ArticleWriter& operator=(const ArticleWriter&) = delete;

    /// Open @p path for writing, creating it if needed. Existing content is
    /// kept — a resumed download writes into the holes it left.
    bool open(const QString& path, QString& error);

    /// Set the final size once, up front, so the file system can lay the file
    /// out in one go instead of extending it article by article.
    bool reserve(qint64 size, QString& error);

    /// Position for the next write run. Offsets are absolute and zero-based —
    /// note that yEnc's `=ypart begin` is 1-based, so the caller passes
    /// `begin - 1`. YencDecoder::offset() has already done that subtraction.
    bool seekTo(qint64 offset, QString& error);

    bool write(QByteArrayView data, QString& error);

    /// Kept for callers; the handle is unbuffered, so there is nothing to push.
    /// Never an fsync: per-article fsync on a 100-part release is a measurable
    /// stall for no safety worth having.
    bool flush(QString& error);

    void close();

    [[nodiscard]] bool isOpen() const { return m_file && m_file->isOpen(); }
    [[nodiscard]] qint64 bytesWritten() const { return m_bytesWritten; }
    [[nodiscard]] QString path() const { return m_file ? m_file->fileName() : QString(); }

private:
    ArticleFileCache* m_cache = nullptr;
    std::shared_ptr<QFile> m_file;
    qint64 m_pos = 0;               ///< this writer's position; the handle may be shared
    qint64 m_bytesWritten = 0;
};

} // namespace eMule::usenet
