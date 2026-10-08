#pragma once

/// @file ConfidenceText.h
/// @brief A fake-file verdict in words, from the ids FakeFileDetector hands out.
///
/// One set of strings for the GUI lists and the web pages. translate() is spelled
/// out each time: lupdate files a local tr() helper under the wrong context.

#include <QCoreApplication>
#include <QString>
#include <QStringList>

namespace eMule {

/// Worst first; -1 for a row nothing was judged on (sorts before everything).
[[nodiscard]] inline int confidenceRank(const QString& id)
{
    static const QStringList order{QStringLiteral("spam"), QStringLiteral("likely_fake"),
                                   QStringLiteral("suspect"), QStringLiteral("caution"),
                                   QStringLiteral("looks_good"), QStringLiteral("genuine")};
    return static_cast<int>(order.indexOf(id));
}

[[nodiscard]] inline QString confidenceText(const QString& id, int score)
{
    switch (confidenceRank(id)) {
    case 0:  return QCoreApplication::translate("eMule::ConfidenceText", "Spam");
    case 1:  return QCoreApplication::translate("eMule::ConfidenceText", "Likely fake");
    case 2:  return QCoreApplication::translate("eMule::ConfidenceText", "Suspect");
    case 3:  return QCoreApplication::translate("eMule::ConfidenceText", "Caution: %1%").arg(score);
    case 4:  return QCoreApplication::translate("eMule::ConfidenceText", "Looks good");
    case 5:  return QCoreApplication::translate("eMule::ConfidenceText", "Genuine");
    default: return {};
    }
}

[[nodiscard]] inline QString fakeReasonText(const QString& reason)
{
    if (reason == QLatin1StringView("multiple_names"))            return QCoreApplication::translate("eMule::ConfidenceText", "Shared under names that describe different content");
    if (reason == QLatin1StringView("names_span_kinds"))          return QCoreApplication::translate("eMule::ConfidenceText", "Shared as different kinds of file (e.g. video and archive)");
    if (reason == QLatin1StringView("bad_signal_name"))           return QCoreApplication::translate("eMule::ConfidenceText", "A name matches a rule in FakeFileFilter.dat");
    if (reason == QLatin1StringView("bad_signal_comment"))        return QCoreApplication::translate("eMule::ConfidenceText", "A comment matches a rule in FakeFileFilter.dat");
    if (reason == QLatin1StringView("header_extension_mismatch")) return QCoreApplication::translate("eMule::ConfidenceText", "The first bytes are not what the extension claims");
    if (reason == QLatin1StringView("executable_masquerade"))     return QCoreApplication::translate("eMule::ConfidenceText", "It is a program named like a media file");
    if (reason == QLatin1StringView("archive_masquerade"))        return QCoreApplication::translate("eMule::ConfidenceText", "It is an archive named like a media file");
    if (reason == QLatin1StringView("claimed_type_mismatch"))     return QCoreApplication::translate("eMule::ConfidenceText", "The published type does not fit the extension");
    if (reason == QLatin1StringView("spam_score"))                return QCoreApplication::translate("eMule::ConfidenceText", "The spam filter rates it high");
    if (reason == QLatin1StringView("spam_status"))               return QCoreApplication::translate("eMule::ConfidenceText", "Counted as spam");
    if (reason == QLatin1StringView("bad_rating"))                return QCoreApplication::translate("eMule::ConfidenceText", "Rated poor, or a Kad note calls it a fake");
    if (reason == QLatin1StringView("fake_rating"))               return QCoreApplication::translate("eMule::ConfidenceText", "Rated as fake by users");
    if (reason == QLatin1StringView("multiple_aich"))             return QCoreApplication::translate("eMule::ConfidenceText", "Sources disagree on the AICH hash");
    if (reason == QLatin1StringView("implausible_media_length"))  return QCoreApplication::translate("eMule::ConfidenceText", "The length does not fit a file of this size");
    if (reason == QLatin1StringView("implausible_media_bitrate")) return QCoreApplication::translate("eMule::ConfidenceText", "The bitrate is not plausible");
    if (reason == QLatin1StringView("media_size_mismatch"))       return QCoreApplication::translate("eMule::ConfidenceText", "Length and bitrate do not add up to the size");
    if (reason == QLatin1StringView("name_media_tag_mismatch"))   return QCoreApplication::translate("eMule::ConfidenceText", "Artist, album and title appear in none of the names");
    return reason;
}

/// One line per reason; empty when there is none.
[[nodiscard]] inline QString confidenceTooltip(const QString& id, int score, const QStringList& reasons)
{
    if (confidenceRank(id) < 0)
        return {};
    QStringList lines{QCoreApplication::translate("eMule::ConfidenceText", "%1 (fake score %2 of 100)")
                          .arg(confidenceText(id, score)).arg(score)};
    for (const QString& reason : reasons)
        lines.push_back(QStringLiteral("• ") + fakeReasonText(reason));
    return lines.join(QLatin1Char('\n'));
}

} // namespace eMule
