#pragma once

/// @file Ed3kLocalSharer.h
/// @brief #224 test helper: another join of the same session serving files from disk (as
/// Shared Files does), so a download of the same root is read from there first.

#include "Ed3kJoinBase.h"
#include "Ed3kSession.h"

#include <QByteArray>
#include <QString>

#include <vector>

namespace eMule::ed3k::test {

struct LocalHashed {
    ::ed3k::Root root;
    std::uint64_t size = 0;
    ::ed3k::Bytes hashes;
};

inline LocalHashed localHashOf(const QByteArray& data)
{
    ::ed3k::Hasher h;
    (void)h.update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())});
    auto done = h.finish();
    LocalHashed out;
    if (done) {
        out.root = done->root;
        out.size = done->size;
        const auto b = done->hashes.bytes();
        out.hashes.assign(b.begin(), b.end());
    }
    return out;
}

class LocalSharer : public Ed3kJoinBase {
public:
    LocalSharer(Ed3kSession& session, const QString& dir) : Ed3kJoinBase(session, dir, QStringLiteral("local"), nullptr) {}
    ~LocalSharer() override { stop(); }
    void start() override { beginStart(); }
    void stop() override
    {
        if (isStarted())
            closeAll();
    }
    /// @p data is the file at @p path; @p servable: whether reads are answered (false: like
    /// unverified bytes at the sharer).
    bool shareFile(const QString& path, const QByteArray& data, bool servable = true)
    {
        const LocalHashed h = localHashOf(data);
        const int member = int(m_paths.size());
        m_paths.push_back(path);
        m_servable.push_back(servable);
        return shareMember(::ed3k::SharedFile{h.root, h.size, "local.bin", std::nullopt, std::nullopt, ::ed3k::Have::complete(), std::span{h.hashes}},
                           QStringLiteral("local"), member);
    }

protected:
    std::vector<std::uint64_t> wantedBlocks(const Download&) const override { return {}; }
    void onFileBlock(const Download&, const ::ed3k::event::Block&) override {}
    void onBlobFetched(const QString&, int, const ::ed3k::Root&, const QByteArray&) override {}
    std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override
    {
        if (s.member < 0 || std::size_t(s.member) >= m_paths.size() || !m_servable[std::size_t(s.member)])
            return std::nullopt;
        return FileRead{m_paths[std::size_t(s.member)], offset, len, qint64(s.size), -1};
    }
    bool hasOwner(const QString& owner) const override { return owner == QStringLiteral("local"); }
    void saveOwner(const QString&) override {}

private:
    std::vector<QString> m_paths;
    std::vector<bool> m_servable;
};

} // namespace eMule::ed3k::test
