#include "pch.h"
/// @file CoreOps.cpp
/// @brief User actions on the running core — implementation.

#include "app/CoreOps.h"

#include "app/AppContext.h"
#include "enodemeta/MetaHash.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/Kademlia.h"
#include "net/BindAddress.h"
#include "net/HttpFileDownload.h"
#include "portmap/PortMapper.h"
#include "prefs/Preferences.h"
#include "protocol/ED2KLink.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "transfer/DownloadQueue.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QUrl>

namespace eMule::ops {

namespace {

[[nodiscard]] Status unavailable(const QString& what)
{
    return Status::fail(503, QStringLiteral("%1 unavailable").arg(what));
}

[[nodiscard]] bool hexToHash(const QString& hex, uint8* out)
{
    return hex.size() == 32 && decodeBase16(hex, out, 16) == 16;
}

[[nodiscard]] uint32 validCategory(qint64 category)
{
    return category > 0 && category < static_cast<qint64>(thePrefs.categoryCount())
        ? static_cast<uint32>(category) : 0;
}

[[nodiscard]] QUrl checkedUrl(const QString& text, Status& st)
{
    const QUrl url(text.trimmed());
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty()
        || (scheme != QLatin1StringView("http") && scheme != QLatin1StringView("https")))
        st = Status::fail(400, QStringLiteral("Not an http(s) URL"));
    return url;
}

} // namespace

// ---------------------------------------------------------------------------
// Downloads
// ---------------------------------------------------------------------------

PartFile* findDownload(const QString& hashHex, Status& st)
{
    if (!theApp.downloadQueue) {
        st = unavailable(QStringLiteral("Download queue"));
        return nullptr;
    }
    uint8 hash[16]{};
    if (!hexToHash(hashHex, hash)) {
        st = Status::fail(400, QStringLiteral("Invalid hash"));
        return nullptr;
    }
    PartFile* file = theApp.downloadQueue->fileByID(hash);
    if (!file)
        st = Status::fail(404, QStringLiteral("Download not found"));
    return file;
}

Status pauseDownload(const QString& hashHex)
{
    Status st;
    if (PartFile* file = findDownload(hashHex, st))
        file->pauseFile();
    return st;
}

Status resumeDownload(const QString& hashHex)
{
    Status st;
    if (PartFile* file = findDownload(hashHex, st))
        file->resumeFile();
    return st;
}

Status stopDownload(const QString& hashHex)
{
    Status st;
    if (PartFile* file = findDownload(hashHex, st))
        file->stopFile(false);
    return st;
}

Status cancelDownload(const QString& hashHex)
{
    Status st;
    if (PartFile* file = findDownload(hashHex, st))
        cancelDownload(file);
    return st;
}

void cancelDownload(PartFile* file)
{
    if (file && theApp.downloadQueue)
        theApp.downloadQueue->cancelFile(file);
}

Status setDownloadPriority(const QString& hashHex, uint8 priority, bool isAuto)
{
    Status st;
    PartFile* file = findDownload(hashHex, st);
    if (!file)
        return st;
    file->setAutoDownPriority(isAuto);
    if (!isAuto)
        file->setDownPriority(priority);
    else
        file->updateAutoDownPriority();
    return st;
}

Status renameDownload(const QString& hashHex, const QString& newNameIn)
{
    Status st;
    PartFile* file = findDownload(hashHex, st);
    if (!file)
        return st;

    // MFC MPG_F2 (DownloadListCtrl.cpp:1405): not once completing/complete, and the
    // name must survive the ed2k link format (IsValidEd2kString: no '|').
    const QString newName = newNameIn.trimmed();
    if (newName.isEmpty() || newName.contains(u'|'))
        return Status::fail(400, QStringLiteral("Invalid file name"));
    const PartFileStatus state = file->status();
    if (state == PartFileStatus::Complete || state == PartFileStatus::Completing)
        return Status::fail(409, QStringLiteral("Download already completed"));

    // Only the display/target name; the .part files keep their numbered names.
    const bool shared = theApp.sharedFileList && theApp.sharedFileList->isFilePtrInList(file);
    if (shared)
        theApp.sharedFileList->removeKeywords(file);
    file->setFileName(newName, true);
    if (shared)
        theApp.sharedFileList->addKeywords(file);
    file->savePartFile();
    return st;
}

Status setDownloadCategory(const QString& hashHex, uint32 category)
{
    Status st;
    PartFile* file = findDownload(hashHex, st);
    if (!file)
        return st;
    // The category is an index, and it decides where the file lands when it
    // completes. An out-of-range one would silently resolve back to the global
    // incoming dir, which looks like the assignment worked.
    if (category >= static_cast<uint32>(thePrefs.categoryCount()))
        return Status::fail(400, QStringLiteral("Unknown category"));
    file->setCategory(category);
    return st;
}

int clearCompletedDownloads(const QSet<QString>& onlyIn)
{
    if (!theApp.downloadQueue)
        return -1;

    QSet<QString> only;
    for (const QString& hash : onlyIn)
        only.insert(hash.toUpper());

    // Collect first, then remove (avoid modifying during iteration)
    std::vector<PartFile*> completed;
    for (auto* file : theApp.downloadQueue->files()) {
        if (file->status() != PartFileStatus::Complete)
            continue;
        if (!only.isEmpty() && !only.contains(md4str(file->fileHash()).toUpper()))
            continue;
        completed.push_back(file);
    }
    for (auto* file : completed)
        theApp.downloadQueue->removeFile(file);
    return static_cast<int>(completed.size());
}

AddOutcome addDownloadFromLink(const QString& linkIn, qint64 category, bool paused)
{
    AddOutcome out;
    if (!theApp.downloadQueue) {
        out.status = unavailable(QStringLiteral("Download queue"));
        return out;
    }

    const QString link = linkIn.trimmed();
    const auto parsed = parseED2KLink(link);
    const auto* fileLink = parsed ? std::get_if<ED2KFileLink>(&*parsed) : nullptr;
    if (!fileLink) {
        out.status = Status::fail(400, QStringLiteral("Not an ed2k file link"));
        return out;
    }
    // a meta hash is not an eD2K file — never queue it
    if (enodemeta::isMetaHash(fileLink->hash.data())) {
        out.status = Status::fail(400, QStringLiteral("Torrent/Usenet results are not eD2K downloads"));
        return out;
    }

    out.hash = md4str(fileLink->hash.data());
    const bool queuedBefore = theApp.downloadQueue->fileByID(fileLink->hash.data()) != nullptr;
    out.added = theApp.downloadQueue->addDownloadFromED2KLink(
        link, DownloadQueue::defaultTempDir(), validCategory(category), paused);
    if (!out.added && !queuedBefore) {
        // Already shared or known, or the part file could not be created.
        out.status = theApp.downloadQueue->isFileExisting(fileLink->hash.data())
            ? Status::fail(409, QStringLiteral("File is already shared or downloaded"))
            : Status::fail(500, QStringLiteral("Could not create the download"));
    }
    return out;
}

AddOutcome addDownloadFromSearch(const QString& hashHex, const QString& fileName,
                                 uint64 fileSize, const QString& rawLink, qint64 category,
                                 uint32 searchID, std::optional<bool> paused)
{
    AddOutcome out;
    if (!theApp.downloadQueue) {
        out.status = unavailable(QStringLiteral("Download queue"));
        return out;
    }

    uint8 hash[16]{};
    const bool hashOk = hexToHash(hashHex, hash);
    if (hashOk && enodemeta::isMetaHash(hash)) {
        out.status = Status::fail(400, QStringLiteral("Torrent/Usenet results are not eD2K downloads"));
        return out;
    }
    out.hash = hashHex;

    // Prefer the original link text when there is one: it carries the AICH hash,
    // part hashes and source hints, none of which survive a hash/name/size round-trip.
    // Rebuilding is only for search results, which never had a link — and it has to
    // re-encode the name, since a '%' or '|' in it would otherwise corrupt the link.
    const QString link = rawLink.startsWith(QStringLiteral("ed2k://"), Qt::CaseInsensitive)
        ? rawLink
        : ed2kFileLink(fileName, fileSize, hashHex);

    out.added = theApp.downloadQueue->addDownloadFromED2KLink(
        link, DownloadQueue::defaultTempDir(), validCategory(category),
        paused.value_or(thePrefs.addNewFilesPaused()));

    // A rebuilt link carries nothing but hash, name and size: hand over what the search
    // result knows (MFC DownloadQueue.cpp:175-200). An already queued download gains from
    // it too, hence not gated on added.
    if (hashOk && searchID != 0 && theApp.searchList) {
        const SearchFile* result = theApp.searchList->searchFileByHash(hash, searchID);
        PartFile* file = theApp.downloadQueue->fileByID(hash);
        if (result && file)
            theApp.downloadQueue->seedFromSearchResult(file, *result);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Servers
// ---------------------------------------------------------------------------

AddServerOutcome addServer(const QString& address, uint16 port, const QString& name,
                           const QString& otherFamilyAddress)
{
    AddServerOutcome out;
    if (!theApp.serverList) {
        out.status = unavailable(QStringLiteral("ServerList"));
        return out;
    }
    if (port == 0 || address.trimmed().isEmpty() || address.contains(u'|')) {
        out.status = Status::fail(400, QStringLiteral("Invalid address or port"));
        return out;
    }

    // No DNS here. fromAddressString() keeps a hostname as a dynIP, which ServerSocket
    // re-resolves on every connect (A then AAAA).
    auto server = Server::fromAddressString(address, port);
    if (!server) {
        out.status = Status::fail(400, QStringLiteral("Invalid address or port"));
        return out;
    }
    if (!name.isEmpty())
        server->setName(name);
    if (thePrefs.manualServerHighPriority())
        server->setPreference(ServerPriority::High);

    // The other-family address of a dual-stack server (server.met entry with an IPv4
    // header plus ST_IPV6).
    if (const Address addr6 = Address::fromString(otherFamilyAddress);
        !addr6.isNull() && !server->hasDynIP() && addr6.isIPv4() != server->ipAddress().isIPv4()
        && ServerList::isGoodServerIP(addr6))
        server->addAddress(addr6);

    // addServer() drops a LAN/loopback literal under filterLANIPs with the same null a
    // duplicate gets, so a pasted 127.0.0.1 link vanished without a word. Say why, once,
    // here on the user path — not inside addServer, which server.met loads hit in bulk.
    if (!server->hasDynIP() && !ServerList::isGoodServerIP(*server)) {
        const QString text = QCoreApplication::translate("eMule::IpcClientHandler",
                                 "Server %1:%2 not added — LAN address filtered "
                                 "(\"Filter server and client LAN IPs\" is on)")
                                 .arg(server->bracketedAddress())
                                 .arg(port);
        logWarning(text);
        out.lanFiltered = true;
        out.status = Status::fail(422, text);
        return out;
    }

    const QString key = server->address();
    out.added = theApp.serverList->addServer(std::move(server), ServerOrigin::Manual) != nullptr;
    if (!out.added) {
        // MFC parity: update name on existing duplicate if name is meaningful
        if (!name.isEmpty() && !name.startsWith(QStringLiteral("Server"))) {
            if (auto* existing = theApp.serverList->findByAddress(key, port))
                existing->setName(name);
        }
    }
    return out;
}

Server* findServer(const QString& address, uint16 port)
{
    if (!theApp.serverList)
        return nullptr;
    QString host = address.trimmed();
    if (host.startsWith(u'[') && host.endsWith(u']'))
        host = host.mid(1, host.size() - 2);
    if (const Address addr = Address::fromString(host); !addr.isNull())
        return theApp.serverList->findByIPTcp(addr, port);
    return theApp.serverList->findByAddress(host, port);
}

Status removeServer(const QString& address, uint16 port)
{
    if (!theApp.serverList)
        return unavailable(QStringLiteral("ServerList"));
    Server* server = findServer(address, port);
    if (!server)
        return Status::fail(404, QStringLiteral("Server not found"));
    theApp.serverList->removeServer(server);
    return {};
}

Status connectToServer(Server* server)
{
    if (!theApp.serverConnect)
        return unavailable(QStringLiteral("ServerConnect"));
    if (!BindAddress::outboundAllowed())
        return Status::fail(409, BindAddress::current().reason);

    // An explicit user action: it proceeds regardless of the "eD2K network" pref,
    // which gates auto-connect at startup only.
    if (!server) {
        theApp.serverConnect->connectToAnyServer();
        return {};
    }

    if (theApp.serverConnect->isConnected()) {
        const auto* current = theApp.serverConnect->currentServer();
        if (current && current->serverId() == server->serverId())
            return {};   // already there
        theApp.serverConnect->disconnect();
    }
    theApp.serverConnect->connectToServer(server);
    return {};
}

Status disconnectFromServer()
{
    if (!theApp.serverConnect)
        return unavailable(QStringLiteral("ServerConnect"));
    if (theApp.serverConnect->isConnecting())
        theApp.serverConnect->stopConnectionTry();
    theApp.serverConnect->disconnect();
    return {};
}

void importServerMetFromUrl(const QString& urlText, std::function<void(const Status&, int)> done)
{
    Status st;
    const QUrl url = checkedUrl(urlText, st);
    if (st.ok() && !theApp.serverList)
        st = unavailable(QStringLiteral("ServerList"));
    if (!st.ok()) {
        done(st, 0);
        return;
    }

    HttpFileDownload::Options opts;
    opts.preferredNames = {QStringLiteral("server.met")};
    HttpFileDownload::get(QCoreApplication::instance(), url, opts,
        [done = std::move(done)](bool ok, const QByteArray& data, const QString&,
                                 const QString& error) {
            if (!ok || data.isEmpty()) {
                done(Status::fail(502, ok ? QStringLiteral("Downloaded server.met is empty")
                                          : error), 0);
                return;
            }
            if (!theApp.serverList) {
                done(unavailable(QStringLiteral("ServerList")), 0);
                return;
            }
            QTemporaryFile file;
            if (!file.open() || file.write(data) != data.size()) {
                done(Status::fail(500, QStringLiteral("Could not store the download")), 0);
                return;
            }
            file.flush();
            const auto before = theApp.serverList->servers().size();
            if (!theApp.serverList->addServerMetToList(file.fileName(), /*merge*/ true)) {
                done(Status::fail(422, QStringLiteral("Not a server.met file")), 0);
                return;
            }
            done({}, static_cast<int>(theApp.serverList->servers().size() - before));
        });
}

// ---------------------------------------------------------------------------
// Kad
// ---------------------------------------------------------------------------

Status startKad(const QString& host, uint16 port)
{
    auto* kad = kad::Kademlia::instance();
    if (!kad)
        return unavailable(QStringLiteral("Kademlia"));
    if (!BindAddress::outboundAllowed())
        return Status::fail(409, BindAddress::current().reason);

    // MFC starts Kad before bootstrapping from an address (KademliaWnd.cpp:282-286)
    if (!kad->isRunning())
        kad->start();
    if (!host.isEmpty() && port > 0)
        kad->bootstrap(host, port);
    return {};
}

Status stopKad()
{
    auto* kad = kad::Kademlia::instance();
    if (kad && kad->isRunning())
        kad->stop();
    return {};
}

void importKadNodesFromUrl(const QString& urlText, std::function<void(const Status&)> done)
{
    Status st;
    const QUrl url = checkedUrl(urlText, st);
    if (st.ok() && !kad::Kademlia::instance())
        st = unavailable(QStringLiteral("Kademlia"));
    if (st.ok() && !BindAddress::outboundAllowed())
        st = Status::fail(409, BindAddress::current().reason);
    if (!st.ok()) {
        done(st);
        return;
    }

    // nodes.dat mirrors are commonly gzipped; unwrapping is transparent for a plain one.
    HttpFileDownload::Options opts;
    opts.preferredNames = {QStringLiteral("nodes.dat")};
    HttpFileDownload::get(QCoreApplication::instance(), url, opts,
        [done = std::move(done)](bool ok, const QByteArray& data, const QString&,
                                 const QString& error) {
            if (!ok || data.isEmpty()) {
                done(Status::fail(502, ok ? QStringLiteral("Downloaded nodes.dat is empty") : error));
                return;
            }
            auto* kad = kad::Kademlia::instance();
            if (!kad) {
                done(unavailable(QStringLiteral("Kademlia")));
                return;
            }
            const QString path = QDir(thePrefs.configDir()).filePath(QStringLiteral("nodes.dat"));
            if (kad->isRunning()) {
                // Running: the file on disk is rewritten on stop; read the new nodes in.
                QTemporaryFile file;
                if (!file.open() || file.write(data) != data.size()) {
                    done(Status::fail(500, QStringLiteral("Could not store the download")));
                    return;
                }
                file.flush();
                if (auto* zone = kad->getRoutingZone())
                    zone->readFile(file.fileName());
                done({});
                return;
            }
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
                done(Status::fail(500, QStringLiteral("Failed to save nodes.dat: %1")
                                           .arg(file.errorString())));
                return;
            }
            file.close();
            kad->start();
            done({});
        });
}

Status recheckFirewall()
{
    // "Am I reachable" and "is my port forwarded" are the same question to a user,
    // and a stale mapping is a common reason a re-check keeps coming back negative.
    if (theApp.portMapper)
        theApp.portMapper->reprobe();

    auto* kad = kad::Kademlia::instance();
    if (!kad || !kad->isRunning())
        return Status::fail(409, QStringLiteral("Kad is not running"));

    switch (kad->recheckFirewalled()) {
    case kad::RecheckFirewallResult::Started:
        return {};
    case kad::RecheckFirewallResult::AlreadyRunning:
        return Status::fail(409, QStringLiteral("A firewall re-check is already in progress"));
    case kad::RecheckFirewallResult::LanMode:
        return Status::fail(409, QStringLiteral("Kad is running in LAN mode — firewall checks are disabled"));
    case kad::RecheckFirewallResult::NotRunning:
        break;
    }
    return Status::fail(409, QStringLiteral("Kad is not running"));
}

// ---------------------------------------------------------------------------
// Shared files
// ---------------------------------------------------------------------------

Status reloadSharedFiles(bool metaDataOnly)
{
    if (!theApp.sharedFileList)
        return unavailable(QStringLiteral("Shared file list"));
    if (metaDataOnly)
        theApp.sharedFileList->rebuildMetaData();
    else
        theApp.sharedFileList->reload();
    return {};
}

Status setSharedDirectories(const QStringList& dirsIn)
{
    QStringList dirs;
    for (const QString& entry : dirsIn) {
        const QFileInfo info(entry.trimmed());
        if (!info.isAbsolute() || !info.isDir())
            return Status::fail(400, QStringLiteral("Not an existing directory: %1").arg(entry));
        const QString path = QDir::cleanPath(info.absoluteFilePath());
        if (!dirs.contains(path))
            dirs.append(path);
    }
    thePrefs.setSharedDirs(dirs);
    if (!thePrefs.save())
        return Status::fail(500, QStringLiteral("Could not write preferences.yml"));
    if (theApp.sharedFileList)
        theApp.sharedFileList->reload();
    return {};
}

} // namespace eMule::ops
