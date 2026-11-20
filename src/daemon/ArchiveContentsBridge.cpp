#include "ArchiveContentsBridge.h"

#include "UsenetBridge.h"
#include "UsenetSession.h"
#include "nzb/NzbFile.h"
#include "prefs/Preferences.h"
#include "queue/UsenetQueue.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <memory>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kArchiveContents.h"
#include "Ed3kDownloads.h"
#include "Ed3kHosterJoin.h"
#include "Ed3kLocalScan.h"
#include "Ed3kSetManifest.h"
#endif
#ifdef EMULE_HAVE_BITTORRENT
#include "BtSession.h"
#endif

namespace eMule::ArchiveContentsBridge {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ArchiveContentsBridge", text);
}

#ifdef EMULE_HAVE_ED3K
ed3k::Ed3kArchiveContents* s_contents = nullptr;
ed3k::Ed3kDownloads* s_downloads = nullptr;
ed3k::Ed3kHosterJoin* s_hoster = nullptr;
/// #224 folder scan of s_contents' node; made on the first LocateLocalCopies
std::unique_ptr<ed3k::Ed3kLocalScan> s_scan;
#endif
#ifdef EMULE_HAVE_BITTORRENT
bt::BtSession* s_bt = nullptr;
#endif

#ifdef EMULE_HAVE_ED3K
std::optional<::ed3k::Root> rootOfHex(const QString& hex)
{
    const QByteArray b = QByteArray::fromHex(hex.toLatin1());
    if (b.size() != 32)
        return std::nullopt;
    ::ed3k::Root r;
    std::copy_n(reinterpret_cast<const std::uint8_t*>(b.constData()), 32, r.bytes.begin());
    return r;
}

QString hexOf(const ::ed3k::Root& r)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(r.bytes.data()), 32).toHex());
}

QCborMap toCbor(const ed3k::Ed3kArchiveContents::Found& f)
{
    QCborArray entries;
    for (std::size_t i = 0; i < f.entries.size(); ++i) {
        const auto& e = f.entries[i];
        QCborMap m{{QStringLiteral("path"), e.path},
                   {QStringLiteral("size"), qint64(e.size)},
                   {QStringLiteral("root"), hexOf(e.root)}};
        // #224: a whole file of it here (Shared Files, a download, a folder scan)
        if (i < f.localPaths.size() && !f.localPaths[i].isEmpty())
            m.insert(QStringLiteral("local"), true);
        entries.append(m);
    }
    QCborMap m{{QStringLiteral("name"), f.name},
               {QStringLiteral("state"), ed3k::Ed3kArchiveContents::stateName(f.state)},
               {QStringLiteral("sites"), qint64(f.sites)},
               {QStringLiteral("password"), f.password},
               {QStringLiteral("entries"), entries}};
    if (f.first)
        m.insert(QStringLiteral("first"), hexOf(*f.first));
    if (f.state == ed3k::Ed3kArchiveContents::State::Found)
        m.insert(QStringLiteral("manifest"), hexOf(f.manifest));
    if (f.allLocal())
        m.insert(QStringLiteral("allLocal"), true);
    return m;
}

/// Archive set parts by opening volume name (as looked up): every part's file name
using Parts = QHash<QString, QStringList>;

/// The opening volumes of the archive sets among @p names; each set's parts into @p parts.
QList<int> firstsOf(const QStringList& names, Parts& parts)
{
    QList<int> out;
    for (const QList<int>& set : ed3k::Ed3kArchiveContents::volumeSets(names)) {
        QStringList members;
        for (const int i : set)
            members << names.at(i).section(u'/', -1);
        parts.insert(names.at(set.constFirst()), members);
        out << set.constFirst();
    }
    return out;
}

/// The first volumes among @p names, as volumes to look up.
std::vector<ed3k::Ed3kArchiveContents::Volume> volumesOf(const QStringList& names, Parts& parts)
{
    std::vector<ed3k::Ed3kArchiveContents::Volume> out;
    for (const int i : firstsOf(names, parts))
        out.push_back({names.at(i), std::nullopt, {}, {}});
    return out;
}

/// web: {links: [{url, name}]}
bool webRequest(const QCborMap& p, ed3k::Ed3kArchiveContents::Request& r, Parts& parts)
{
    QStringList names;
    QStringList urls;
    for (const QCborValue& v : p.value(QStringLiteral("links")).toArray()) {
        const QCborMap l = v.toMap();
        names << l.value(QStringLiteral("name")).toString();
        urls << l.value(QStringLiteral("url")).toString();
    }
    for (const int i : firstsOf(names, parts))
        r.volumes.push_back({names.at(i), std::nullopt, urls.at(i), {}});
    return true;
}

/// nzb: {previewToken | usenetItem, files?: [int]}
bool nzbRequest(const QCborMap& p, ed3k::Ed3kArchiveContents::Request& r, Parts& parts, QString& error)
{
    usenet::NzbInfo info;
    if (const QString token = p.value(QStringLiteral("previewToken")).toString(); !token.isEmpty()) {
        const QByteArray nzb = UsenetBridge::previewNzb(token);
        QString parseError;
        if (nzb.isEmpty() || !usenet::NzbFile::parse(nzb, info, parseError)) {
            error = tr("The NZB is no longer available; open Details again.");
            return false;
        }
    } else {
        const auto* queue = usenet::theUsenetSession ? usenet::theUsenetSession->queue() : nullptr;
        const auto* item = queue ? queue->findItem(p.value(QStringLiteral("usenetItem")).toString()) : nullptr;
        if (!item) {
            error = tr("The download is gone.");
            return false;
        }
        info = item->nzb;
    }
    QStringList names;
    for (const auto& f : info.files)
        names << f.fileName;
    QList<int> firsts = firstsOf(names, parts);
    const auto ids = ed3k::Ed3kArchiveContents::nzbFileIds(info, firsts);
    for (qsizetype k = 0; k < firsts.size(); ++k)
        r.volumes.push_back({names.at(firsts.at(k)), std::nullopt, {}, ids.at(std::size_t(k))});
    return true;
}

/// torrent: {btih?, btmh?, names} | {torrentId}
bool torrentRequest(const QCborMap& p, ed3k::Ed3kArchiveContents::Request& r, Parts& parts, QString& error)
{
    QStringList names;
    QString btih = p.value(QStringLiteral("btih")).toString();
    QString btmh = p.value(QStringLiteral("btmh")).toString();
    for (const QCborValue& v : p.value(QStringLiteral("names")).toArray())
        names << v.toString();
#ifdef EMULE_HAVE_BITTORRENT
    if (const QString id = p.value(QStringLiteral("torrentId")).toString(); !id.isEmpty() && s_bt) {
        const auto row = s_bt->torrent(id);
        if (!row) {
            error = tr("The torrent is gone.");
            return false;
        }
        btih = row->hashV1;
        btmh = row->hashV2;
        for (const auto& f : s_bt->files(id))
            if (!f.pad)
                names << f.path;
    }
#endif
    r.btih = QByteArray::fromHex(btih.toLatin1());
    r.btmh = QByteArray::fromHex(btmh.toLatin1());
    if (r.btih.size() != 20)
        r.btih.clear();
    if (r.btmh.size() != 32)
        r.btmh.clear();
    if (r.btih.isEmpty() && r.btmh.isEmpty()) {
        error = tr("The torrent has no info hash.");
        return false;
    }
    r.volumes = volumesOf(names, parts);
    return true;
}

/// roots: {volumes: [{name, root}]} | {hosterPackage}
bool rootsRequest(const QCborMap& p, ed3k::Ed3kArchiveContents::Request& r, Parts& parts)
{
    QStringList names;
    std::vector<std::optional<::ed3k::Root>> roots;
    QStringList urls;
    if (p.contains(QStringLiteral("hosterPackage")) && s_hoster) {
        const auto pkg = quint32(p.value(QStringLiteral("hosterPackage")).toInteger());
        const auto one = quint32(p.value(QStringLiteral("hosterItem")).toInteger());
        for (const auto& it : s_hoster->items())
            if ((pkg != 0 && it.packageId == pkg) || (one != 0 && it.id == one)) {
                names << it.name;
                roots.push_back(rootOfHex(it.rootHex));
                urls << (it.generic ? QString() : it.url);
            }
    }
    for (const QCborValue& v : p.value(QStringLiteral("volumes")).toArray()) {
        const QCborMap m = v.toMap();
        names << m.value(QStringLiteral("name")).toString();
        roots.push_back(rootOfHex(m.value(QStringLiteral("root")).toString()));
        urls << QString();
    }
    for (const int i : firstsOf(names, parts))
        r.volumes.push_back({names.at(i), roots.at(std::size_t(i)), urls.at(i), {}});
    return true;
}

/// The kept entries of the queued download @p p names, and whether they can change; nullopt:
/// not a queued download.
std::optional<std::pair<QHash<QString, QStringList>, bool>> keptOf(const QCborMap& p)
{
    if (const QString id = p.value(QStringLiteral("usenetItem")).toString(); !id.isEmpty()) {
        const auto* queue = usenet::theUsenetSession ? usenet::theUsenetSession->queue() : nullptr;
        const auto* item = queue ? queue->findItem(id) : nullptr;
        if (!item)
            return std::nullopt;
        return std::pair{item->keptEntries, queue->keptEntriesEditable(id)};
    }
    if ((p.contains(QStringLiteral("hosterPackage")) || p.contains(QStringLiteral("hosterItem"))) && s_hoster) {
        bool editable = false;
        auto kept = s_hoster->keptEntries(quint32(p.value(QStringLiteral("hosterPackage")).toInteger()),
                                          quint32(p.value(QStringLiteral("hosterItem")).toInteger()), &editable);
        return std::pair{std::move(kept), editable};
    }
    return std::nullopt;
}
#endif

} // namespace

void setTargets(ed3k::Ed3kArchiveContents* contents, ed3k::Ed3kDownloads* downloads, ed3k::Ed3kHosterJoin* hoster,
                bt::BtSession* bt)
{
#ifdef EMULE_HAVE_ED3K
    if (contents != s_contents)
        s_scan.reset();   // its node goes with the old target
    s_contents = contents;
    s_downloads = downloads;
    s_hoster = hoster;
#else
    Q_UNUSED(contents) Q_UNUSED(downloads) Q_UNUSED(hoster)
#endif
#ifdef EMULE_HAVE_BITTORRENT
    s_bt = bt;
#else
    Q_UNUSED(bt)
#endif
}

void lookup(const QString& kind, const QCborMap& payload, Reply reply)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_contents) {
        reply(false, tr("ed3k is off."));
        return;
    }
    ed3k::Ed3kArchiveContents::Request r;
    Parts parts;
    QString error;
    bool ok = false;
    if (kind == QLatin1String("web"))
        ok = webRequest(payload, r, parts);
    else if (kind == QLatin1String("nzb"))
        ok = nzbRequest(payload, r, parts, error);
    else if (kind == QLatin1String("torrent"))
        ok = torrentRequest(payload, r, parts, error);
    else if (kind == QLatin1String("roots"))
        ok = rootsRequest(payload, r, parts);
    else
        error = QStringLiteral("unknown kind");
    if (!ok) {
        reply(false, error);
        return;
    }
    if (r.volumes.empty()) {
        reply(true, QCborMap{{QStringLiteral("archives"), QCborArray()}});
        return;
    }
    // a queued download: its extraction choice so far (hoster.md §6.1)
    std::optional<std::pair<QHash<QString, QStringList>, bool>> queued = keptOf(payload);
    const auto started = s_contents->lookup(std::move(r), [reply, queued, parts](const std::vector<ed3k::Ed3kArchiveContents::Found>& found) {
        QCborArray archives;
        for (const auto& f : found) {
            QCborMap a = toCbor(f);
            // the set's parts share the opening volume's record
            if (const auto p = parts.constFind(f.name); p != parts.constEnd() && p->size() > 1)
                a.insert(QStringLiteral("parts"), QCborArray::fromStringList(*p));
            if (queued) {
                const auto k = queued->first.constFind(f.name.section(u'/', -1).toLower());
                if (k != queued->first.constEnd())
                    a.insert(QStringLiteral("kept"), QCborArray::fromStringList(*k));
            }
            archives.append(a);
        }
        QCborMap out{{QStringLiteral("archives"), archives}};
        if (queued)
            out.insert(QStringLiteral("editable"), queued->second);
        reply(true, out);
    });
    if (!started)
        reply(false, tr("ed3k is not connected yet."));
#else
    Q_UNUSED(kind) Q_UNUSED(payload)
    reply(false, tr("ed3k is not built in."));
#endif
}

QHash<QString, QStringList> entriesFromCbor(const QCborMap& map)
{
    QHash<QString, QStringList> out;
    for (auto k = map.constBegin(); k != map.constEnd(); ++k) {
        QStringList paths;
        for (const QCborValue& p : k.value().toArray())
            if (const QString t = p.toString(); !t.isEmpty())
                paths << t;
        if (!paths.isEmpty())
            out.insert(k.key().toString().toLower(), paths);
    }
    return out;
}

void setEntries(const QCborMap& target, const QCborMap& entries, Reply reply)
{
    const QHash<QString, QStringList> kept = entriesFromCbor(entries);
    if (const QString id = target.value(QStringLiteral("usenetItem")).toString(); !id.isEmpty()) {
        auto* queue = usenet::theUsenetSession ? usenet::theUsenetSession->queue() : nullptr;
        if (!queue || !queue->findItem(id)) {
            reply(false, tr("The download is gone."));
            return;
        }
        if (!queue->setItemKeptEntries(id, kept)) {
            reply(false, tr("The download is unpacked already."));
            return;
        }
        reply(true, QCborValue(true));
        return;
    }
#ifdef EMULE_HAVE_ED3K
    if (s_hoster && (target.contains(QStringLiteral("hosterPackage")) || target.contains(QStringLiteral("hosterItem")))) {
        if (!s_hoster->setKeptEntries(quint32(target.value(QStringLiteral("hosterPackage")).toInteger()),
                                      quint32(target.value(QStringLiteral("hosterItem")).toInteger()), kept)) {
            reply(false, tr("The download is unpacked already, or gone."));
            return;
        }
        reply(true, QCborValue(true));
        return;
    }
#endif
    reply(false, tr("Not a queued download."));
}

void downloadEntries(const QCborArray& entries, const QString& folder, int category, Reply reply)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_downloads) {
        reply(false, tr("ed3k is off."));
        return;
    }
    const QString base = QDir(thePrefs.incomingDirForCategory(category))
                             .filePath(ed3k::ed3kSafeName(folder.isEmpty() ? QStringLiteral("archive") : folder));
    int added = 0;
    QString lastError;
    for (const QCborValue& v : entries) {
        const QCborMap m = v.toMap();
        const auto root = rootOfHex(m.value(QStringLiteral("root")).toString());
        const QString path = m.value(QStringLiteral("path")).toString();
        const QString target = ed3k::ed3kSetLocalPath(base, path);
        if (!root || target.isEmpty())
            continue;
        ed3k::Ed3kDownloadSpec spec;
        spec.root = *root;
        spec.size = std::uint64_t(m.value(QStringLiteral("size")).toInteger());
        spec.name = QFileInfo(target).fileName();
        spec.targetPath = target;
        spec.category = category;
        if (auto ok = s_downloads->add(spec); ok)
            ++added;
        else
            lastError = ok.error();
    }
    if (added == 0 && !lastError.isEmpty()) {
        reply(false, lastError);
        return;
    }
    reply(true, QCborMap{{QStringLiteral("added"), added}});
#else
    Q_UNUSED(entries) Q_UNUSED(folder) Q_UNUSED(category)
    reply(false, tr("ed3k is not built in."));
#endif
}

void takeFromDisk(const QCborMap& target, const QCborArray& archives, const QString& folder, int category, Reply reply)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_contents || !s_downloads) {
        reply(false, tr("ed3k is off."));
        return;
    }
    QCborArray entries;
    QSet<QString> volumes;   // lower-case file names of the archives' parts
    for (const QCborValue& av : archives) {
        const QCborMap a = av.toMap();
        volumes.insert(a.value(QStringLiteral("name")).toString().section(u'/', -1).toLower());
        for (const QCborValue& p : a.value(QStringLiteral("parts")).toArray())
            volumes.insert(p.toString().section(u'/', -1).toLower());
        for (const QCborValue& ev : a.value(QStringLiteral("entries")).toArray()) {
            const QCborMap e = ev.toMap();
            const auto root = rootOfHex(e.value(QStringLiteral("root")).toString());
            const auto size = std::uint64_t(e.value(QStringLiteral("size")).toInteger());
            // all of it or nothing: a missing entry needs the archive after all
            if (!root || s_contents->localPathOf(*root, size).isEmpty()) {
                reply(false, tr("\"%1\" is not on disk here; the archive is still needed.").arg(e.value(QStringLiteral("path")).toString()));
                return;
            }
            entries.append(e);
        }
    }
    if (entries.isEmpty()) {
        reply(false, tr("Nothing to take."));
        return;
    }
    downloadEntries(entries, folder, category, [target, volumes, reply](bool ok, const QCborValue& value) {
        if (!ok) {
            reply(false, value);
            return;
        }
        QCborMap out = value.toMap();
        int skipped = 0;
        QString note;
        if (const QString id = target.value(QStringLiteral("usenetItem")).toString(); !id.isEmpty()) {
            auto* queue = usenet::theUsenetSession ? usenet::theUsenetSession->queue() : nullptr;
            const auto* item = queue ? queue->findItem(id) : nullptr;
            QList<int> files;
            for (qsizetype i = 0; item && i < item->nzb.files.size(); ++i)
                if (volumes.contains(item->nzb.files.at(i).fileName.section(u'/', -1).toLower())
                    || volumes.contains(item->bestFileName(int(i)).section(u'/', -1).toLower()))
                    files << int(i);
            if (!files.isEmpty()) {
                note = queue->setFilesSkipped(id, files, true);
                if (note.isEmpty())
                    skipped = int(files.size());
            }
        } else if (s_hoster) {
            const auto pkg = quint32(target.value(QStringLiteral("hosterPackage")).toInteger());
            const auto one = quint32(target.value(QStringLiteral("hosterItem")).toInteger());
            std::vector<quint32> ids;
            for (const auto& it : s_hoster->items())
                if (((pkg != 0 && it.packageId == pkg) || (one != 0 && it.id == one))
                    && it.state != ed3k::Ed3kHosterJoin::State::Complete && volumes.contains(it.name.section(u'/', -1).toLower()))
                    ids.push_back(it.id);
            for (const quint32 id : ids)
                s_hoster->remove(id, true);
            skipped = int(ids.size());
        }
        out.insert(QStringLiteral("skipped"), skipped);
        if (!note.isEmpty())
            out.insert(QStringLiteral("note"), note);
        reply(true, out);
    });
#else
    Q_UNUSED(target) Q_UNUSED(archives) Q_UNUSED(folder) Q_UNUSED(category)
    reply(false, tr("ed3k is not built in."));
#endif
}

void locateLocalCopies(const QStringList& folders, Reply reply)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_contents) {
        reply(false, tr("ed3k is off."));
        return;
    }
    if (folders.isEmpty()) {
        reply(false, tr("No folder given."));
        return;
    }
    if (!s_scan)
        s_scan = std::make_unique<ed3k::Ed3kLocalScan>(s_contents->session());
    if (s_scan->running()) {
        reply(false, tr("A folder scan is running already."));
        return;
    }
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = QObject::connect(s_scan.get(), &ed3k::Ed3kLocalScan::finished, s_scan.get(),
                             [reply, conn](const ed3k::Ed3kLocalScan::Result& r) {
        QObject::disconnect(*conn);
        reply(true, QCborMap{{QStringLiteral("files"), r.files},
                             {QStringLiteral("candidates"), r.candidates},
                             {QStringLiteral("hashed"), r.hashed},
                             {QStringLiteral("matched"), r.matched},
                             {QStringLiteral("paths"), QCborArray::fromStringList(r.paths)}});
    });
    (void)s_scan->start(folders);   // false: finished() came already (nothing to look for)
#else
    Q_UNUSED(folders)
    reply(false, tr("ed3k is not built in."));
#endif
}

} // namespace eMule::ArchiveContentsBridge
