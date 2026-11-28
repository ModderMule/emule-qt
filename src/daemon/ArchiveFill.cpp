#include "ArchiveFill.h"

#include "Ed3kArchiveContents.h"
#include "post/Par2LocalFill.h"
#include "post/Par2Verifier.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"
#include "utils/Log.h"
#include "utils/ArgStr.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QThreadPool>

namespace eMule {

ArchiveFill::ArchiveFill(ed3k::Ed3kArchiveContents& contents, usenet::UsenetQueue& queue, QObject* parent)
    : QObject(parent)
    , m_contents(contents)
    , m_queue(queue)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    connect(&m_queue, &usenet::UsenetQueue::itemAdded, this, &ArchiveFill::onItemAdded);
    connect(&m_queue, &usenet::UsenetQueue::itemChanged, this, &ArchiveFill::onItemChanged);
    connect(&m_queue, &usenet::UsenetQueue::itemRemoved, this, [this](const QString& id, bool) { onItemRemoved(id); });
    connect(&m_queue, &usenet::UsenetQueue::articleDecoded, this, [this](const QString& id, int file) {
        const auto it = m_items.find(id);
        if (it == m_items.end())
            return;
        if (it->second.pending.contains(file))
            acceptPending(id);
        // the index .par2 may be sealed by this article: queued, after the queue's bookkeeping
        if (!it->second.done && !it->second.running && !it->second.sources.isEmpty())
            QMetaObject::invokeMethod(this, [this, id] { tryFill(id); }, Qt::QueuedConnection);
    });
}

ArchiveFill::~ArchiveFill()
{
    m_cancel->store(true);
    for (const auto& [id, item] : m_items)
        if (item.lookup)
            m_contents.cancelLookup(item.lookup);
}

QStringList ArchiveFill::sourcesOf(const QString& itemId) const
{
    const auto it = m_items.find(itemId);
    return it == m_items.end() ? QStringList() : it->second.sources;
}

void ArchiveFill::setSources(const QString& itemId, const QStringList& paths)
{
    if (paths.isEmpty() || !m_queue.findItem(itemId))
        return;
    Item& item = m_items[itemId];
    item.sources = paths;
    item.lookup = 0;
    logUsenet(QStringLiteral("Usenet: %1 file(s) inside the archives of \"%2\" are on disk here; the volumes are filled from them")
                  .arg(argStr(paths.size()),
                       argStr(itemId)));
    tryFill(itemId);
}

void ArchiveFill::onItemAdded(const QString& id)
{
    const auto* item = m_queue.findItem(id);
    if (!m_enabled || !item || m_items.contains(id))
        return;
    // the archives' first volumes, by their NZB file ids (usenet.md §1)
    QStringList names;
    for (const auto& f : item->nzb.files)
        names << f.fileName;
    const QList<int> firsts = ed3k::Ed3kArchiveContents::firstVolumes(names);
    if (firsts.isEmpty())
        return;
    const auto ids = ed3k::Ed3kArchiveContents::nzbFileIds(item->nzb, firsts);
    ed3k::Ed3kArchiveContents::Request r;
    for (qsizetype k = 0; k < firsts.size(); ++k)
        r.volumes.push_back({names.at(firsts.at(k)), std::nullopt, {}, ids.at(std::size_t(k))});
    QPointer<ArchiveFill> self(this);
    const auto lookup = m_contents.lookup(std::move(r), [self, id](const std::vector<ed3k::Ed3kArchiveContents::Found>& found) {
        if (!self)
            return;
        if (auto it = self->m_items.find(id); it != self->m_items.end())
            it->second.lookup = 0;
        QStringList paths;
        for (const auto& f : found)
            for (const QString& p : f.localPaths)
                if (!p.isEmpty() && !paths.contains(p))
                    paths << p;
        if (paths.isEmpty()) {
            self->m_items.erase(id);
            return;
        }
        self->setSources(id, paths);
    });
    if (lookup)
        m_items[id].lookup = *lookup;
}

void ArchiveFill::onItemChanged(const QString& id)
{
    if (const auto it = m_items.find(id); it != m_items.end() && !it->second.sources.isEmpty() && !it->second.done)
        tryFill(id);
}

void ArchiveFill::onItemRemoved(const QString& id)
{
    const auto it = m_items.find(id);
    if (it == m_items.end())
        return;
    if (it->second.lookup)
        m_contents.cancelLookup(it->second.lookup);
    m_items.erase(it);
}

void ArchiveFill::tryFill(const QString& id)
{
    const auto it = m_items.find(id);
    const auto* item = m_queue.findItem(id);
    if (!m_enabled || it == m_items.end() || !item || it->second.running || it->second.done || it->second.sources.isEmpty())
        return;
    // the index .par2 complete on disk; the volumes still open, by name
    QStringList par2;
    QHash<QString, QString> targets;
    QHash<QString, int> indexOf;
    for (qsizetype i = 0; i < item->files.size() && i < item->nzb.files.size(); ++i) {
        const auto& st = item->files.at(i);
        const QString name = QFileInfo(item->bestFileName(int(i))).fileName();
        if (name.endsWith(QStringLiteral(".par2"), Qt::CaseInsensitive)) {
            // was: st.finalized && !st.finalPath.isEmpty(); a sealed file keeps its tempPath
            // (renamed in the work folder) until the release is published: never filled
            const QString path = !st.finalPath.isEmpty() ? st.finalPath : st.tempPath;
            if (st.finalized && !path.isEmpty())
                par2 << path;
            continue;
        }
        if (!st.finalized && !st.tempPath.isEmpty()) {
            targets.insert(name.toLower(), st.tempPath);
            indexOf.insert(name.toLower(), int(i));
        }
    }
    const QString index = usenet::Par2Verifier::chooseIndexFile(par2);
    if (index.isEmpty() || QFileInfo(index).fileName().contains(QStringLiteral(".vol"), Qt::CaseInsensitive))
        return;   // not yet: the index file first
    if (targets.isEmpty()) {
        it->second.done = true;
        return;
    }
    it->second.running = true;
    QPointer<ArchiveFill> self(this);
    const auto cancel = m_cancel;
    const QStringList sources = it->second.sources;
    QThreadPool::globalInstance()->start([self, cancel, id, index, sources, targets, indexOf] {
        const auto ranges = usenet::Par2LocalFill::fill(index, sources, targets, cancel.get());
        QMetaObject::invokeMethod(self.data(), [self, id, ranges, indexOf] {
            if (!self)
                return;
            const auto it = self->m_items.find(id);
            if (it == self->m_items.end())
                return;
            it->second.running = false;
            it->second.done = true;
            qint64 bytes = 0;
            for (const auto& r : ranges) {
                const int file = indexOf.value(QFileInfo(r.fileName).fileName().toLower(), -1);
                if (file < 0)
                    continue;
                bytes += r.length;
                it->second.pending[file].emplace_back(r.offset, r.length);
            }
            logUsenet(QStringLiteral("Usenet: \"%1\": %2 byte(s) of its volumes from disk").arg(argStr(id), argStr(bytes)));
            emit self->filled(id, bytes);
            self->acceptPending(id);
        }, Qt::QueuedConnection);
    });
}

void ArchiveFill::acceptPending(const QString& id)
{
    const auto it = m_items.find(id);
    const auto* item = m_queue.findItem(id);
    if (it == m_items.end() || !item)
        return;
    auto& pending = it->second.pending;
    for (auto p = pending.begin(); p != pending.end();) {
        const int file = p->first;
        if (file < 0 || file >= item->files.size()) {
            p = pending.erase(p);
            continue;
        }
        // was: accepted at once; the part size borrowed then may be a lone small .par2's
        if (item->files.at(file).partLength <= 0) {
            ++p;
            continue;
        }
        int articles = 0;
        for (const auto& [offset, length] : p->second)
            articles += m_queue.acceptExternalRange(id, file, offset, length);
        logUsenet(QStringLiteral("Usenet: \"%1\": %2 article(s) of file %3 not fetched (on disk here)").arg(argStr(id), argStr(articles), argStr(file)));
        p = pending.erase(p);
        emit accepted(id, file, articles);
    }
}

} // namespace eMule
