/// @file BtTorrentCreator.cpp
/// @brief .torrent creation (#191) — see BtTorrentCreator.h. After qBittorrent's
/// TorrentCreator: natural file order, dot files skipped, tracker tiers by empty lines.

#include "create/BtTorrentCreator.h"

#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/entry.hpp>

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace eMule::bt {

namespace lt = libtorrent;

namespace {

struct Cancelled : std::runtime_error {
    Cancelled()
        : std::runtime_error("cancelled")
    {
    }
};

bool isDotName(const QString& path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return path.mid(slash + 1).startsWith(u'.');
}

lt::create_flags_t formatFlags(BtTorrentFormat f)
{
    switch (f) {
    case BtTorrentFormat::V1: return lt::create_torrent::v1_only;
    case BtTorrentFormat::V2: return lt::create_torrent::v2_only;
    case BtTorrentFormat::Hybrid: break;
    }
    return {};
}

/// relative names ('/' separators) below the root, the root's own name first
std::expected<std::vector<lt::create_file_entry>, QString> listFiles(const BtCreateParams& p)
{
    const QFileInfo root(p.path);
    if (!root.exists())
        return std::unexpected(QStringLiteral("Path not found: %1").arg(p.path));
    std::vector<lt::create_file_entry> files;
    if (root.isFile()) {
        files.emplace_back(root.fileName().toStdString(), root.size());
        return files;
    }
    if (!root.isDir())
        return std::unexpected(QStringLiteral("Not a file or folder: %1").arg(p.path));

    QCollator natural;
    natural.setNumericMode(true);
    natural.setCaseSensitivity(Qt::CaseInsensitive);
    const QDir dir(root.absoluteFilePath());
    const QString rootName = root.fileName();

    QStringList names;
    if (!p.onlyFiles.isEmpty()) {
        names = p.onlyFiles;
    } else {
        QDirIterator it(dir.path(), QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo fi = it.nextFileInfo();
            if (fi.isSymLink())
                continue;
            names << dir.relativeFilePath(fi.filePath());
        }
    }
    // folders first by path, then files by name: qBittorrent's order
    std::ranges::sort(names, [&natural](const QString& a, const QString& b) {
        const qsizetype sa = a.lastIndexOf(u'/');
        const qsizetype sb = b.lastIndexOf(u'/');
        const QString da = sa < 0 ? QString() : a.left(sa);
        const QString db = sb < 0 ? QString() : b.left(sb);
        if (const int c = natural.compare(da, db); c != 0)
            return c < 0;
        return natural.compare(a.mid(sa + 1), b.mid(sb + 1)) < 0;
    });
    for (const QString& name : names) {
        if (name.isEmpty() || name.startsWith(u'/') || name.split(u'/').contains(QStringLiteral("..")))
            return std::unexpected(QStringLiteral("Invalid file name: %1").arg(name));
        if (p.ignoreDotfiles && std::ranges::any_of(name.split(u'/'), [](const QString& s) { return isDotName(s); }))
            continue;
        const QFileInfo fi(dir.filePath(name));
        if (!fi.isFile())
            return std::unexpected(QStringLiteral("Not a file: %1").arg(fi.filePath()));
        files.emplace_back(QString(rootName + u'/' + name).toStdString(), fi.size());
    }
    if (files.empty())
        return std::unexpected(p.ignoreDotfiles ? QStringLiteral("No files (dot files are left out)")
                                                : QStringLiteral("No files"));
    return files;
}

} // namespace

BtTorrentCreator::BtTorrentCreator(BtCreateParams params, QObject* parent)
    : QObject(parent)
    , m_params(std::move(params))
{
    setAutoDelete(false);   // the owner deletes it after finished()
}

void BtTorrentCreator::run()
{
    BtCreateResult r;
    const QFileInfo root(m_params.path);
    r.name = root.fileName();
    r.savePath = root.absolutePath();
    emit progress(0);
    try {
        auto files = listFiles(m_params);
        if (!files)
            throw std::runtime_error(files.error().toStdString());
        lt::create_torrent t(std::move(*files), m_params.pieceSize, formatFlags(m_params.format));
        for (const QString& seed : m_params.webSeeds)
            if (const QString s = seed.trimmed(); !s.isEmpty())
                t.add_url_seed(s.toStdString());
        int tier = 0;
        for (const QString& line : m_params.trackers) {
            if (const QString url = line.trimmed(); url.isEmpty())
                ++tier;
            else
                t.add_tracker(url.toStdString(), tier);
        }
        const int pieces = t.num_pieces();
        lt::error_code ec;
        int shown = 0;
        lt::set_piece_hashes(t, r.savePath.toStdString(), [this, pieces, &shown](lt::piece_index_t n) {
            if (m_cancel.load(std::memory_order_relaxed))
                throw Cancelled();
            // one signal per percent, not per piece
            if (const int pct = pieces > 0 ? int((static_cast<int>(n) + 1) * 100LL / pieces) : 100; pct != shown) {
                shown = pct;
                emit progress(pct);
            }
        }, ec);
        if (ec)
            throw std::runtime_error(ec.message());
        if (!m_params.creator.isEmpty())
            t.set_creator(m_params.creator.toStdString().c_str());
        if (!m_params.comment.isEmpty())
            t.set_comment(m_params.comment.toStdString().c_str());
        t.set_priv(m_params.isPrivate);
        lt::entry e = t.generate();
        if (const QString s = m_params.source.trimmed(); !s.isEmpty())
            e["info"]["source"] = s.toStdString();
        std::vector<char> buf;
        lt::bencode(std::back_inserter(buf), e);
        r.data = QByteArray(buf.data(), qsizetype(buf.size()));
        r.pieceSize = t.piece_length();
        r.pieces = pieces;
        r.ok = true;
    } catch (const Cancelled&) {
        r.cancelled = true;
        r.error = QStringLiteral("Cancelled");
    } catch (const std::exception& e) {
        r.error = QString::fromLocal8Bit(e.what());
    }
    emit finished(r);
}

std::expected<int, QString> BtTorrentCreator::countPieces(const BtCreateParams& params)
{
    try {
        auto files = listFiles(params);
        if (!files)
            return std::unexpected(files.error());
        return lt::create_torrent(std::move(*files), params.pieceSize, formatFlags(params.format)).num_pieces();
    } catch (const std::exception& e) {
        return std::unexpected(QString::fromLocal8Bit(e.what()));
    }
}

} // namespace eMule::bt
