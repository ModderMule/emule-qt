/// @file Ed3kDeclineUrls.cpp
/// @brief Decline lists fetched from URLs — implementation.

#include "Ed3kDeclineUrls.h"

#include "net/HttpFileDownload.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace eMule::ed3k {

namespace {

constexpr QLatin1String kUrlHeader{"# url: "};

int entriesOf(const DeclineList& l)
{
    return int(l.roots.size() + l.ed2k.size() + l.keys.size() + l.kinds.size()) + int(l.keywords.size());
}

} // namespace

Ed3kDeclineUrls::Ed3kDeclineUrls(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(kRefreshMs);
    connect(&m_timer, &QTimer::timeout, this, &Ed3kDeclineUrls::fetchAll);
}

Ed3kDeclineUrls::~Ed3kDeclineUrls()
{
    stop();
}

void Ed3kDeclineUrls::setDir(const QString& dir)
{
    if (dir == m_dir)
        return;
    m_dir = dir;
    reload();
}

void Ed3kDeclineUrls::setUrls(const QStringList& urls)
{
    QStringList clean;
    for (const QString& u : urls)
        if (const QString t = u.trimmed(); !t.isEmpty() && !clean.contains(t))
            clean << t;
    if (clean == m_urls)
        return;
    m_urls = clean;
    dropStaleCaches();
    reload();
    if (m_started)
        fetchAll();
}

void Ed3kDeclineUrls::start()
{
    if (m_started)
        return;
    m_started = true;
    m_timer.start();
    fetchAll();
}

void Ed3kDeclineUrls::stop()
{
    m_started = false;
    m_timer.stop();
    // their callbacks find another list (onFetched checks the URL) or none
    for (const auto& job : std::exchange(m_jobs, {}))
        if (job)
            job->abort(QStringLiteral("stopped"));
    for (Status& s : m_status)
        s.fetching = false;
}

void Ed3kDeclineUrls::fetchAll()
{
    for (int n = 0; n < int(m_urls.size()); ++n)
        fetch(n);
    if (!m_urls.isEmpty())
        emit statusChanged();
}

QString Ed3kDeclineUrls::cacheName(int n)
{
    return QStringLiteral("decline-url-%1.txt").arg(n);
}

// -- private ------------------------------------------------------------------

void Ed3kDeclineUrls::fetch(int n)
{
    if (n < 0 || n >= int(m_urls.size()) || std::size_t(n) >= m_status.size() || m_status[std::size_t(n)].fetching)
        return;
    const QString url = m_urls.at(n);
    const QUrl parsed(url);
    if (!parsed.isValid() || (parsed.scheme() != QLatin1String("https") && parsed.scheme() != QLatin1String("http"))) {
        onFetched(n, url, false, {}, QStringLiteral("not an http(s) URL"));
        return;
    }
    m_status[std::size_t(n)].fetching = true;
    HttpFileDownload::Options opts;
    opts.maxBytes = kMaxBytes;
    // the list's URL may hold a token: never in a log line
    opts.redactUrls = true;
    m_jobs.emplace_back(HttpFileDownload::fetch(this, parsed, opts, [this, n, url](const HttpFileDownload::Result& r) {
        std::erase_if(m_jobs, [](const QPointer<HttpDownloadJob>& j) { return !j || j->isFinished(); });
        if (!m_started)
            return;   // aborted by stop()
        QString error = r.error;
        if (r.ok && qsizetype(r.data.size()) > qsizetype(kMaxBytes))
            error = QStringLiteral("larger than %1 MiB").arg(kMaxBytes >> 20);
        onFetched(n, url, r.ok && error.isEmpty(), r.data, error);
    }));
}

void Ed3kDeclineUrls::onFetched(int n, const QString& url, bool ok, const QByteArray& data, const QString& error)
{
    // the list changed meanwhile: another URL at that place
    if (n >= int(m_urls.size()) || m_urls.at(n) != url || std::size_t(n) >= m_status.size())
        return;
    Status& s = m_status[std::size_t(n)];
    s.fetching = false;
    s.triedAt = QDateTime::currentMSecsSinceEpoch();
    s.error = ok ? QString() : error;
    if (!ok) {
        logEd3kWarning(QStringLiteral("ed3k: decline list %1 not fetched (%2); %3").arg(n + 1).arg(error,
                           s.fetchedAt > 0 ? QStringLiteral("its last copy stays in use") : QStringLiteral("none in use")));
        emit statusChanged();
        return;
    }
    QSaveFile f(cachePath(n));
    if (m_dir.isEmpty() || !QDir().mkpath(m_dir) || !f.open(QIODevice::WriteOnly)
        || f.write((kUrlHeader + url + QLatin1Char('\n')).toUtf8()) < 0 || f.write(data) < 0 || !f.commit()) {
        s.error = QStringLiteral("cannot write %1").arg(cacheName(n));
        logEd3kWarning(QStringLiteral("ed3k: decline list %1: %2").arg(n + 1).arg(s.error));
        emit statusChanged();
        return;
    }
    logEd3k(QStringLiteral("ed3k: decline list %1 fetched (%2 bytes)").arg(n + 1).arg(data.size()));
    reload();
    emit statusChanged();
}

void Ed3kDeclineUrls::reload()
{
    std::vector<Status> status;
    DeclineList merged;
    for (int n = 0; n < int(m_urls.size()); ++n) {
        Status s;
        s.url = m_urls.at(n);
        // what this run knew of the same URL
        if (const auto old = std::ranges::find(m_status, s.url, &Status::url); old != m_status.end()) {
            s.triedAt = old->triedAt;
            s.error = old->error;
            s.fetching = old->fetching;
        }
        if (!m_dir.isEmpty()) {
            QFile f(cachePath(n));
            if (f.open(QIODevice::ReadOnly)) {
                const QString text = QString::fromUtf8(f.readAll());
                const qsizetype eol = text.indexOf(QLatin1Char('\n'));
                if (eol > 0 && text.left(eol).trimmed() == QString(kUrlHeader + s.url).trimmed()) {
                    DeclineList l = parseDeclineList(text.mid(eol + 1));
                    l.invalid.clear();
                    s.entries = entriesOf(l);
                    s.fetchedAt = QFileInfo(f).lastModified().toMSecsSinceEpoch();
                    merged.merge(l);
                }
            }
        }
        status.push_back(std::move(s));
    }
    m_status = std::move(status);
    if (merged == m_merged)
        return;
    m_merged = std::move(merged);
    emit listsChanged();
}

void Ed3kDeclineUrls::dropStaleCaches() const
{
    if (m_dir.isEmpty())
        return;
    for (int n = int(m_urls.size());; ++n) {
        const QString path = cachePath(n);
        if (!QFileInfo::exists(path))
            break;
        QFile::remove(path);
    }
}

QString Ed3kDeclineUrls::cachePath(int n) const
{
    return QDir(m_dir).filePath(cacheName(n));
}

} // namespace eMule::ed3k
