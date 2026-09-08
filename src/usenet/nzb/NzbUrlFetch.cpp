/// @file NzbUrlFetch.cpp
/// @brief Fetching one .nzb from an http(s) URL, daemon-side.

#include "nzb/NzbUrlFetch.h"

#include "net/HttpFileDownload.h"
#include "utils/Log.h"

#include <QFileInfo>
#include <QLatin1StringView>
#include <QRegularExpression>

namespace eMule::usenet {

namespace {

/// Path segments that name an endpoint rather than a release. An indexer's
/// download URL is routinely `…/api?t=get&id=12345` or `…/getnzb/<id>`, and
/// calling the download "api" is worse than letting the NZB name itself.
[[nodiscard]] bool isGenericSegment(const QString& name)
{
    static const QStringList kGeneric{
        QStringLiteral("api"),      QStringLiteral("getnzb"),
        QStringLiteral("fetch"),    QStringLiteral("download"),
        QStringLiteral("nzb"),      QStringLiteral("get"),
    };
    return kGeneric.contains(name, Qt::CaseInsensitive);
}

} // namespace

QString NzbUrlFetch::rejectReason(const QUrl& url)
{
    if (!url.isValid() || url.scheme().isEmpty())
        return tr("That is not a valid URL.");

    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return tr("Only http:// and https:// links can be downloaded.");

    if (url.host().isEmpty())
        return tr("The link has no host name.");

    return {};
}

QString NzbUrlFetch::nameFromUrl(const QUrl& url)
{
    QString name = url.fileName(QUrl::FullyDecoded).trimmed();
    if (name.isEmpty())
        return {};

    // .nzb.gz is common: indexers compress the payload and HttpFileDownload
    // gunzips it transparently, so both suffixes come off.
    if (name.endsWith(QLatin1String(".gz"), Qt::CaseInsensitive))
        name.chop(3);
    if (name.endsWith(QLatin1String(".nzb"), Qt::CaseInsensitive))
        name.chop(4);

    name = name.trimmed();
    if (name.isEmpty() || isGenericSegment(name))
        return {};

    // Digits alone are a record id, not a name.
    static const QRegularExpression digits(QStringLiteral("^\\d+$"));
    if (digits.match(name).hasMatch())
        return {};

    // Never trust a path segment to be a bare name. The scratch directory is
    // keyed by UUID rather than by this, so it is defence in depth — but the
    // string is remote input and is treated as such.
    name = QFileInfo(name).fileName();
    name.remove(QLatin1Char('\r'));
    name.remove(QLatin1Char('\n'));
    return name.trimmed();
}

void NzbUrlFetch::fetch(QObject* context, const QUrl& url, Callback done)
{
    if (const QString why = rejectReason(url); !why.isEmpty()) {
        done(Result{{}, {}, why});
        return;
    }

    // The host only. A newznab link carries its api key in the query, and some
    // indexers put a token in the *path* instead, which redactApiKey() cannot
    // reach — so nothing but the host is ever logged.
    logInfo(QStringLiteral("Usenet: fetching NZB from %1").arg(url.host()));

    HttpFileDownload::Options opts;
    opts.timeoutMs = 30000;
    opts.maxBytes = kMaxNzbBytes;

    HttpFileDownload::get(context, url, opts,
                          [done = std::move(done), url](bool ok, const QByteArray& data,
                                                        const QString&, const QString& error) {
        if (!ok) {
            done(Result{{}, {}, tr("Could not download the NZB: %1").arg(error)});
            return;
        }
        if (data.isEmpty()) {
            done(Result{{}, {}, tr("The link returned an empty file.")});
            return;
        }

        // Deliberately not the entryName HttpFileDownload reports: for a
        // gunzipped payload that is libarchive's name for a raw stream, not the
        // original filename.
        done(Result{data, nameFromUrl(url), {}});
    });
}

} // namespace eMule::usenet
