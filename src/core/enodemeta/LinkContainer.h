#pragma once

/// @file LinkContainer.h
/// @brief Link containers (.dlc, .ccf, .rsdf) — port of enodemeta/linkcontainer (Go).
///
/// Encrypted with keys of the services that made them, so never read: the
/// identity is the SHA-256 of the bytes, and the file is saved as served.

#include <QString>

namespace eMule::enodemeta {

/// Largest container a client accepts; real ones are a few KiB.
inline constexpr int kMaxContainerSize = 1 << 20;

/// "dlc"/"ccf"/"rsdf" from a file name or address, "" for none of them.
[[nodiscard]] inline QString containerFormat(QString name)
{
    for (qsizetype i = 0; i < name.size(); ++i) {
        if (name.at(i) == u'?' || name.at(i) == u'#') {
            name.truncate(i);
            break;
        }
    }
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot < 0 || name.indexOf(u'/', dot) >= 0)
        return {};
    const QString ext = name.mid(dot + 1).toLower();
    return ext == u"dlc" || ext == u"ccf" || ext == u"rsdf" ? ext : QString();
}

/// "dlc"/"ccf"/"rsdf" a content type names ("application/x-dlc"), "" for none.
[[nodiscard]] inline QString containerFormatOf(QString contentType)
{
    if (const qsizetype semi = contentType.indexOf(u';'); semi >= 0)
        contentType.truncate(semi);
    contentType = contentType.trimmed().toLower();
    for (const char* f : {"dlc", "ccf", "rsdf"})
        if (contentType == QStringLiteral("application/x-") + QLatin1StringView(f))
            return QString::fromLatin1(f);
    return {};
}

} // namespace eMule::enodemeta
