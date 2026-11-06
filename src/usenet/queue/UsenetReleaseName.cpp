/// @file UsenetReleaseName.cpp
/// @brief Episode tokens: SxxEyy, NxNN, air dates; fix tags after them.

#include "queue/UsenetReleaseName.h"

#include "queue/UsenetHealth.h"

#include <QRegularExpression>

namespace eMule::usenet {

namespace {

using RE = QRegularExpression;

/// S01E02, S01.E02, S1E2E3, S01E02-E03
const RE& reSeasonEpisode()
{
    static const RE re(QStringLiteral(R"((?<![a-z0-9])s(\d{1,2})[ ._-]?e(\d{1,3})(?:[ ._-]?e?\d{1,3})*(?![a-z0-9]))"),
                       RE::CaseInsensitiveOption);
    return re;
}

/// 1x02; 1920x1080 is no episode: one or two digits before the x only
const RE& reCross()
{
    static const RE re(QStringLiteral(R"((?<![a-z0-9])(\d{1,2})x(\d{2,3})(?![a-z0-9]))"),
                       RE::CaseInsensitiveOption);
    return re;
}

/// 2024.03.05 / 2024-03-05
const RE& reDateYmd()
{
    static const RE re(QStringLiteral(R"((?<!\d)((?:19|20)\d{2})[ ._-](0[1-9]|1[0-2])[ ._-](0[1-9]|[12]\d|3[01])(?!\d))"));
    return re;
}

/// 05.03.2024
const RE& reDateDmy()
{
    static const RE re(QStringLiteral(R"((?<!\d)(0[1-9]|[12]\d|3[01])[ ._-](0[1-9]|1[0-2])[ ._-]((?:19|20)\d{2})(?!\d))"));
    return re;
}

const RE& reFix()
{
    static const RE re(QStringLiteral(R"((?<![a-z0-9])(?:proper|repack|rerip|real)(?![a-z0-9]))"),
                       RE::CaseInsensitiveOption);
    return re;
}

QString foldedTitle(const QString& name, qsizetype tokenStart)
{
    QString t = usenetFoldedReleaseName(name.left(tokenStart));
    // "Show (2019)" and "Show 2019" are one show
    t.remove(QLatin1Char('(')).remove(QLatin1Char(')')).remove(QLatin1Char('['))
        .remove(QLatin1Char(']'));
    return t.simplified();
}

} // namespace

QString ReleaseEpisode::key() const
{
    if (airDate.isValid())
        return title + QLatin1Char('/') + airDate.toString(Qt::ISODate);
    return title + QLatin1Char('/') + QString::number(season) + QLatin1Char('/')
        + QString::number(episode);
}

QString ReleaseEpisode::label() const
{
    if (airDate.isValid())
        return airDate.toString(Qt::ISODate);
    return QStringLiteral("S%1E%2").arg(season, 2, 10, QLatin1Char('0'))
        .arg(episode, 2, 10, QLatin1Char('0'));
}

std::optional<ReleaseEpisode> parseReleaseEpisode(const QString& name)
{
    ReleaseEpisode ep;
    qsizetype tokenStart = -1;
    qsizetype tokenEnd = -1;

    if (const auto m = reSeasonEpisode().match(name); m.hasMatch()) {
        ep.season = m.captured(1).toInt();
        ep.episode = m.captured(2).toInt();
        tokenStart = m.capturedStart();
        tokenEnd = m.capturedEnd();
    } else if (const auto c = reCross().match(name); c.hasMatch()) {
        ep.season = c.captured(1).toInt();
        ep.episode = c.captured(2).toInt();
        tokenStart = c.capturedStart();
        tokenEnd = c.capturedEnd();
    } else if (const auto d = reDateYmd().match(name); d.hasMatch()) {
        ep.airDate = QDate(d.captured(1).toInt(), d.captured(2).toInt(), d.captured(3).toInt());
        tokenStart = d.capturedStart();
        tokenEnd = d.capturedEnd();
    } else if (const auto e = reDateDmy().match(name); e.hasMatch()) {
        ep.airDate = QDate(e.captured(3).toInt(), e.captured(2).toInt(), e.captured(1).toInt());
        tokenStart = e.capturedStart();
        tokenEnd = e.capturedEnd();
    } else {
        return std::nullopt;
    }

    if (ep.season < 0 && !ep.airDate.isValid())
        return std::nullopt;
    ep.title = foldedTitle(name, tokenStart);
    if (ep.title.isEmpty())
        return std::nullopt;
    ep.proper = reFix().match(name, tokenEnd).hasMatch();
    return ep;
}

} // namespace eMule::usenet
