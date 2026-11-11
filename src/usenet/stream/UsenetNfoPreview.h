#pragma once

/// @file UsenetNfoPreview.h
/// @brief The .nfo text posted with a release, for search Details › NFO.
///
/// Fetched only when the tab is opened. NFOs are small (one or two articles),
/// so every .nfo the NZB lists is read in full, up to kMaxNfos.

#include "stream/UsenetArticleGrab.h"

#include <QList>
#include <QString>

namespace eMule::usenet {

struct UsenetNfoText {
    QString name;
    QString text;
};

class UsenetNfoPreview : public UsenetArticleGrab {
    Q_OBJECT

public:
    static constexpr int kMaxNfos = 4;
    static constexpr qint64 kMaxNfoBytes = 2 * 1024 * 1024;   ///< encoded, per file

    using UsenetArticleGrab::UsenetArticleGrab;

    /// The NZB's .nfo files, NZB order. Pure, for tests and the Details tab test.
    [[nodiscard]] static QList<int> plan(const NzbInfo& nzb);
    /// UTF-8 when it is valid UTF-8, else CP437 (the DOS code page NFO art is drawn in).
    [[nodiscard]] static QString decode(const QByteArray& data);

    /// Fetch and decode. finished() fires once, also when nothing was found.
    void start(const NzbInfo& nzb);

signals:
    void finished(const QList<eMule::usenet::UsenetNfoText>& nfos, const QString& note);

private:
    void finish(const QString& note = {});

    QList<int> m_plan;
    QList<UsenetNfoText> m_nfos;
};

} // namespace eMule::usenet

Q_DECLARE_METATYPE(eMule::usenet::UsenetNfoText)
