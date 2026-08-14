#pragma once

/// @file TextLinks.h
/// @brief Turning text somebody else sent us into clickable anchors, and back again.
///
/// Shared by every pane that displays remote text: the Server Info greeting, friend
/// chat and IRC. The reference makes anything that looks like a URL clickable
/// (CHTRichEditCtrl::AppendText — srchybrid/HTRichEditCtrl.cpp:648-690). Doing that in
/// a QTextBrowser runs into a problem the reference never had: the click comes back as
/// a QUrl, and an eD2K link
///
///     ed2k://|file|Name.rar|4960062|95818F…|h=4XEIH…|/
///
/// has no QUrl spelling at all — everything between "ed2k://" and the first '/' is
/// parsed as the authority, and '|' is not a legal host character. The QUrl is
/// therefore invalid, and QUrl::toString() returns an EMPTY STRING for an invalid
/// URL, so the original link is simply gone by the time the handler sees it.
///
/// So the link text is never put anywhere QUrl can mangle it. The anchor's href is
/// an internal URL that is always valid and carries the real link percent-encoded:
///
///     <a href='emuleqt:link?u=ed2k%3A%2F%2F%7Cfile%7C…'>ed2k://|file|…</a>
///
/// toHref() writes that form, fromHref() reads it back. Everything else — magnet:?,
/// mailto:, a bare "www." — travels the same way, so the handler never has to care
/// which schemes Qt's parser happens to accept.
///
/// linkify() always scans the RAW text, never a rendered copy: the href has to carry
/// the URL exactly as it was sent. Callers that decorate their text (smileys, mIRC
/// colour codes) pass that work in as @p renderText, which runs on the gaps between
/// links only.

#include <QLatin1StringView>
#include <QObject>
#include <QString>
#include <QStringView>
#include <QTextBrowser>
#include <QUrl>
#include <QUrlQuery>

#include <array>
#include <functional>
#include <utility>

namespace eMule::TextLinks {

/// URL prefixes to linkify (s_apszSchemes — srchybrid/OtherFunctions.cpp:89-100).
inline constexpr std::array kSchemes = {
    QLatin1StringView{"ed2k://"},  QLatin1StringView{"http://"},
    QLatin1StringView{"https://"}, QLatin1StringView{"ftp://"},
    QLatin1StringView{"www."},     QLatin1StringView{"ftp."},
    QLatin1StringView{"mailto:"},  QLatin1StringView{"magnet:?"},
};

/// Scheme+path of the wrapper URL, up to the payload.
inline constexpr QLatin1StringView kHrefPrefix{"emuleqt:link?u="};

/// Wrap @p link so it survives a round trip through QUrl.
[[nodiscard]] inline QString toHref(const QString& link)
{
    // The default encoding spares only unreserved characters, so '|', ':', '/', '?',
    // '&' and '=' all come back percent-encoded and the result always parses as
    // scheme "emuleqt", path "link", query "u=…".
    return kHrefPrefix + QString::fromLatin1(QUrl::toPercentEncoding(link));
}

/// Recover the original link text from a clicked anchor.
///
/// Anything not written by toHref() — the banner's own "emuleqt:versioncheck"
/// sentinel, say — is returned as it arrived.
[[nodiscard]] inline QString fromHref(const QUrl& href)
{
    if (href.scheme() == QLatin1String("emuleqt") && href.path() == QLatin1String("link"))
        return QUrlQuery(href).queryItemValue(QStringLiteral("u"), QUrl::FullyDecoded);
    return href.toString();
}

/// True where a URL has to stop.
///
/// Whitespace, as the reference's AppendText does (_tcscspn(psz, " \t\r\n") —
/// srchybrid/HTRichEditCtrl.cpp:664), plus every C0 control byte: IRC carries mIRC
/// formatting as 0x02/0x03/0x0F/0x16/0x1D/0x1F, and "\x0304http://x\x03" must yield
/// the URL, not the URL with a colour-reset byte glued to its tail.
[[nodiscard]] inline bool endsUrl(QChar c)
{
    return c.isSpace() || c.unicode() < u' ';
}

/// Wrap every recognised URL in @p line in an anchor, rendering everything between the
/// links with @p renderText.
///
/// @p renderText MUST HTML-escape what it returns — it is the only thing standing
/// between remote text and the rich-text parser. It never sees a URL, so smiley and
/// formatting substitutions cannot corrupt one.
template <typename RenderText>
[[nodiscard]] QString linkify(const QString& line, RenderText&& renderText)
{
    QString out;
    qsizetype pos = 0;

    while (pos < line.size()) {
        // Find the earliest scheme match at or after pos.
        qsizetype bestStart = -1;
        for (const auto scheme : kSchemes) {
            const qsizetype at = line.indexOf(scheme, pos, Qt::CaseInsensitive);
            if (at >= 0 && (bestStart < 0 || at < bestStart))
                bestStart = at;
        }
        if (bestStart < 0)
            break;

        out += renderText(QStringView{line}.mid(pos, bestStart - pos).toString());

        qsizetype end = bestStart;
        while (end < line.size() && !endsUrl(line[end]))
            ++end;

        QString url = QStringView{line}.mid(bestStart, end - bestStart).toString();
        // Trailing sentence punctuation is almost never part of the link.
        while (!url.isEmpty() && QStringLiteral(".,;:!?)").contains(url.back()))
            url.chop(1);

        if (url.isEmpty()) {
            // A "scheme" that was nothing but punctuation. Cannot happen with the
            // table above, but leaving pos where it is would spin forever.
            out += renderText(QStringView{line}.mid(bestStart, end - bestStart).toString());
            pos = end;
            continue;
        }

        // "www."/"ftp." need a scheme before anything can open them. Prefixing here
        // rather than in the handler keeps the pane showing exactly what was sent
        // while the click still resolves.
        const QString link = url.startsWith(QLatin1String("www."), Qt::CaseInsensitive)
                                 ? QStringLiteral("http://") + url
                             : url.startsWith(QLatin1String("ftp."), Qt::CaseInsensitive)
                                 ? QStringLiteral("ftp://") + url
                                 : url;

        out += QStringLiteral("<a href='%1'>%2</a>")
                   .arg(toHref(link).toHtmlEscaped(), url.toHtmlEscaped());
        pos = bestStart + url.size();
    }

    out += renderText(QStringView{line}.mid(pos).toString());
    return out;
}

/// HTML-escape @p line and wrap every recognised URL in an <a> tag.
[[nodiscard]] inline QString escapeAndLinkify(const QString& line)
{
    return linkify(line, [](const QString& text) { return text.toHtmlEscaped(); });
}

/// Route @p browser's anchor clicks to @p onLink as plain text, owned by @p context.
///
/// openLinks has to go off, not just openExternalLinks: left on, QTextBrowser answers
/// a click by calling setSource() on the href, which navigates the pane away from the
/// log and blanks it.
inline void wireLinkClicks(QTextBrowser* browser, QObject* context,
                           std::function<void(const QString&)> onLink)
{
    browser->setOpenLinks(false);
    browser->setOpenExternalLinks(false);
    QObject::connect(browser, &QTextBrowser::anchorClicked, context,
                     [fn = std::move(onLink)](const QUrl& href) { fn(fromHref(href)); });
}

} // namespace eMule::TextLinks
