#include "ArchiveFill.h"

#include "Ed3kArchiveContents.h"
#include "post/Par2LocalFill.h"
#include "post/Par2Verifier.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"
#include "utils/Log.h"

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
                  .arg(paths.size())
                  .arg(itemId));
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
            if (st.finalized && !st.finalPath.isEmpty())
                par2 << st.finalPath;
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
            int articles = 0;
            for (const auto& r : ranges) {
                const int file = indexOf.value(QFileInfo(r.fileName).fileName().toLower(), -1);
                if (file < 0)
                    continue;
                bytes += r.length;
                articles += self->m_queue.acceptExternalRange(id, file, r.offset, r.length);
            }
            logUsenet(QStringLiteral("Usenet: \"%1\": %2 byte(s) of its volumes from disk, %3 article(s) not fetched")
                          .arg(id).arg(bytes).arg(articles));
            emit self->filled(id, bytes);
        }, Qt::QueuedConnection);
    });
}

} // namespace eMule
