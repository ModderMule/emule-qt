#include "BtLocateFiles.h"

#include <libtorrent/hasher.hpp>
#include <libtorrent/torrent_info.hpp>

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cstring>
#include <set>
#include <unordered_map>

namespace lt = libtorrent;

namespace eMule::bt {

namespace {

constexpr qint64 kV2Block = 16 * 1024;
/// duplicates of one size around a shared piece: combinations tried
constexpr int kMaxCombos = 64;

using Options = std::map<int, QStringList>;

[[nodiscard]] bool cancelled(const std::atomic<bool>* cancel)
{
    return cancel && cancel->load(std::memory_order_relaxed);
}

[[nodiscard]] bool readAt(const QString& path, qint64 offset, char* out, qint64 length)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(offset))
        return false;
    qint64 done = 0;
    while (done < length) {
        const qint64 n = f.read(out + done, length - done);
        if (n <= 0)
            return false;
        done += n;
    }
    return true;
}

/// BEP 52 file root: 16 KiB leaves, zero leaves up to a power of two.
[[nodiscard]] bool rootMatches(const QString& path, qint64 size, const lt::sha256_hash& want, const std::atomic<bool>* cancel)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    std::vector<lt::sha256_hash> level;
    level.reserve(std::size_t((size + kV2Block - 1) / kV2Block));
    std::vector<char> block(static_cast<std::size_t>(kV2Block));
    for (qint64 left = size; left > 0;) {
        if (cancelled(cancel))
            return false;
        const qint64 want_n = std::min(left, kV2Block);
        qint64 got = 0;
        while (got < want_n) {
            const qint64 n = f.read(block.data() + got, want_n - got);
            if (n <= 0)
                return false;
            got += n;
        }
        level.push_back(lt::hasher256(lt::span<char const>(block.data(), std::ptrdiff_t(got))).final());
        left -= got;
    }
    std::size_t width = 1;
    while (width < level.size())
        width *= 2;
    lt::sha256_hash pad;   // zero: the leaves past the end
    while (width > 1) {
        std::vector<lt::sha256_hash> up;
        up.reserve(width / 2);
        for (std::size_t i = 0; i < width; i += 2) {
            if (i >= level.size()) {
                // all padding from here: one hash for the rest of this level
                break;
            }
            lt::hasher256 h;
            h.update(level.at(i));
            h.update(i + 1 < level.size() ? level.at(i + 1) : pad);
            up.push_back(h.final());
        }
        lt::hasher256 p;
        p.update(pad);
        p.update(pad);
        pad = p.final();
        level = std::move(up);
        width /= 2;
    }
    return !level.empty() && level.front() == want;
}

/// One piece: its slices of real files (pad files read as zeros).
class PieceCheck {
public:
    explicit PieceCheck(const lt::torrent_info& ti)
        : m_ti(ti)
        , m_fs(ti.layout())
        , m_buf(static_cast<std::size_t>(ti.piece_length()))
    {
    }

    [[nodiscard]] std::vector<lt::file_slice> slices(lt::piece_index_t p) const
    {
        return m_fs.map_block(p, 0, m_fs.piece_size(p));
    }
    [[nodiscard]] std::vector<int> realFiles(lt::piece_index_t p) const
    {
        std::vector<int> out;
        for (const auto& s : slices(p))
            if (!m_fs.pad_file_at(s.file_index))
                out.push_back(int(s.file_index));
        return out;
    }
    /// The piece read with @p pick (file → path) into the buffer; its size, -1 on a read error.
    [[nodiscard]] qint64 read(lt::piece_index_t p, const std::map<int, QString>& pick)
    {
        qint64 at = 0;
        for (const auto& s : slices(p)) {
            if (m_fs.pad_file_at(s.file_index)) {
                std::memset(m_buf.data() + at, 0, std::size_t(s.size));
            } else {
                const auto it = pick.find(int(s.file_index));
                if (it == pick.end() || !readAt(it->second, s.offset, m_buf.data() + at, s.size))
                    return -1;
            }
            at += s.size;
        }
        return at;
    }
    /// The piece read with @p pick passes its SHA-1.
    [[nodiscard]] bool passes(lt::piece_index_t p, const std::map<int, QString>& pick)
    {
        const qint64 n = read(p, pick);
        return n >= 0 && lt::hasher(lt::span<char const>(m_buf.data(), std::ptrdiff_t(n))).final() == m_ti.hash_for_piece(p);
    }
    [[nodiscard]] std::vector<char> take(qint64 n) const { return {m_buf.begin(), m_buf.begin() + n}; }

private:
    const lt::torrent_info& m_ti;
    const lt::file_storage& m_fs;
    std::vector<char> m_buf;
};

[[nodiscard]] std::pair<int, int> piecesOf(const lt::file_storage& fs, lt::file_index_t f)
{
    const auto first = fs.map_file(f, 0, 0).piece;
    const auto last = fs.map_file(f, fs.file_size(f) - 1, 0).piece;
    return {int(first), int(last)};
}

/// Options that pass a piece shared by files, kept per file; false: no combination passes.
[[nodiscard]] bool narrow(PieceCheck& check, lt::piece_index_t p, Options& options, const std::atomic<bool>* cancel)
{
    const std::vector<int> files = check.realFiles(p);
    std::vector<std::size_t> at(files.size(), 0);
    for (const int f : files)
        if (options[f].isEmpty())
            return false;
    std::map<int, QSet<QString>> good;
    bool any = false;
    for (int tries = 0; tries < kMaxCombos && !cancelled(cancel); ++tries) {
        std::map<int, QString> pick;
        for (std::size_t k = 0; k < files.size(); ++k)
            pick[files.at(k)] = options[files.at(k)].at(qsizetype(at.at(k)));
        if (check.passes(p, pick)) {
            any = true;
            for (const auto& [f, path] : pick)
                good[f].insert(path);
        }
        std::size_t k = 0;
        for (; k < files.size(); ++k) {
            if (++at.at(k) < std::size_t(options[files.at(k)].size()))
                break;
            at.at(k) = 0;
        }
        if (k == files.size())
            break;
    }
    if (!any)
        return false;
    for (const int f : files) {
        QStringList kept;
        for (const QString& path : std::as_const(options[f]))
            if (good[f].contains(path))
                kept << path;
        options[f] = kept;
    }
    return true;
}

} // namespace

BtLocateScan locateTorrentFiles(const lt::torrent_info& ti, const QStringList& folders, const std::vector<int>& targets,
                                const std::map<int, QString>& current, const std::atomic<bool>* cancel)
{
    BtLocateScan out;
    const lt::file_storage& fs = ti.layout();

    std::set<qint64> sizes;
    for (const int t : targets)
        if (t >= 0 && t < fs.num_files())
            sizes.insert(fs.file_size(lt::file_index_t(t)));

    std::unordered_map<qint64, QStringList> bySize;
    QSet<QString> seen;
    for (const QString& folder : folders) {
        QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext() && !cancelled(cancel)) {
            it.next();
            const QFileInfo info = it.fileInfo();
            ++out.files;
            if (!sizes.contains(info.size()))
                continue;
            const QString path = info.canonicalFilePath();
            if (path.isEmpty() || seen.contains(path))
                continue;
            seen.insert(path);
            bySize[info.size()] << path;
            ++out.candidates;
        }
    }

    const auto candidatesOf = [&](int t) {
        QStringList list = bySize[fs.file_size(lt::file_index_t(t))];
        if (const auto own = current.find(t); own != current.end()) {
            const QString canon = QFileInfo(own->second).canonicalFilePath();
            list.removeAll(canon.isEmpty() ? own->second : canon);
        }
        return list;
    };

    std::vector<int> v1;
    int withCandidates = 0;
    for (const int t : targets) {
        if (t < 0 || t >= fs.num_files() || cancelled(cancel))
            continue;
        const lt::file_index_t f(t);
        const QStringList list = candidatesOf(t);
        if (list.isEmpty())
            continue;
        ++withCandidates;
        const lt::sha256_hash root = fs.root(f);
        if (root.is_all_zeros()) {
            v1.push_back(t);
            continue;
        }
        for (const QString& path : list) {
            ++out.hashed;
            if (rootMatches(path, fs.file_size(f), root, cancel)) {
                out.found[t] = path;
                break;
            }
        }
    }

    if (!v1.empty() && ti.info_hashes().has_v1()) {
        PieceCheck check(ti);
        Options options;
        std::set<int> failed;
        std::set<int> shared;
        std::map<int, BtLocateCopy> whole;   // first candidate with every whole piece right
        // whole pieces first: most wrong files stop here
        for (const int t : v1) {
            const auto [first, last] = piecesOf(fs, lt::file_index_t(t));
            QStringList passed;
            std::vector<int> own;
            for (int p = first; p <= last; ++p)
                if (check.realFiles(lt::piece_index_t(p)).size() == 1)
                    own.push_back(p);
            for (const QString& path : candidatesOf(t)) {
                ++out.hashed;
                bool ok = !cancelled(cancel);
                for (std::size_t k = 0; k < own.size() && ok && !cancelled(cancel); ++k)
                    ok = check.passes(lt::piece_index_t(own.at(k)), {{t, path}});
                if (ok)
                    passed << path;
            }
            if (!passed.isEmpty() && !own.empty())
                whole[t] = {passed.front(), own};
            options[t] = passed;
            for (const int p : {first, last})
                if (check.realFiles(lt::piece_index_t(p)).size() > 1)
                    shared.insert(p);
        }
        // neighbours that are no target: as they are on disk
        for (const int p : shared)
            for (const int f : check.realFiles(lt::piece_index_t(p)))
                if (!options.contains(f)) {
                    const auto own = current.find(f);
                    options[f] = own != current.end() && QFileInfo(own->second).size() == fs.file_size(lt::file_index_t(f))
                        ? QStringList{own->second} : QStringList{};
                }
        const auto failTouching = [&](int p) {
            for (const int f : check.realFiles(lt::piece_index_t(p)))
                failed.insert(f);
        };
        for (const int p : shared)
            if (!narrow(check, lt::piece_index_t(p), options, cancel))
                failTouching(p);
        // one path per file, all its shared pieces together
        std::map<int, QString> pick;
        for (const auto& [f, list] : options)
            if (!list.isEmpty())
                pick[f] = list.front();
        for (const int p : shared)
            if (!check.passes(lt::piece_index_t(p), pick))
                failTouching(p);
        for (const int t : v1)
            if (!failed.contains(t) && pick.contains(t) && !cancelled(cancel))
                out.found[t] = pick.at(t);
        for (auto& [t, copy] : whole)
            if (!out.found.contains(t) && !cancelled(cancel))
                out.copies[t] = std::move(copy);
    }
    out.partial = withCandidates - int(out.found.size()) - int(out.copies.size());
    return out;
}

std::vector<char> readLocatedPiece(const lt::torrent_info& ti, int piece, int file, const QString& path)
{
    if (piece < 0 || piece >= ti.num_pieces())
        return {};
    PieceCheck check(ti);
    const qint64 n = check.read(lt::piece_index_t(piece), {{file, path}});
    return n < 0 ? std::vector<char>{} : check.take(n);
}

} // namespace eMule::bt
