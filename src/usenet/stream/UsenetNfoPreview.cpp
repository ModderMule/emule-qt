#include "pch.h"
/// @file UsenetNfoPreview.cpp
/// @brief .nfo text of a release — see UsenetNfoPreview.h.

#include "stream/UsenetNfoPreview.h"

#include <QFileInfo>
#include <QPointer>
#include <QStringDecoder>

#include <array>

namespace eMule::usenet {

namespace {

/// CP437 0x80-0xFF.
constexpr std::array<char16_t, 128> kCp437High{
    u'Ç', u'ü', u'é', u'â', u'ä', u'à', u'å', u'ç', u'ê', u'ë', u'è', u'ï', u'î', u'ì', u'Ä', u'Å',
    u'É', u'æ', u'Æ', u'ô', u'ö', u'ò', u'û', u'ù', u'ÿ', u'Ö', u'Ü', u'¢', u'£', u'¥', u'₧', u'ƒ',
    u'á', u'í', u'ó', u'ú', u'ñ', u'Ñ', u'ª', u'º', u'¿', u'⌐', u'¬', u'½', u'¼', u'¡', u'«', u'»',
    u'░', u'▒', u'▓', u'│', u'┤', u'╡', u'╢', u'╖', u'╕', u'╣', u'║', u'╗', u'╝', u'╜', u'╛', u'┐',
    u'└', u'┴', u'┬', u'├', u'─', u'┼', u'╞', u'╟', u'╚', u'╔', u'╩', u'╦', u'╠', u'═', u'╬', u'╧',
    u'╨', u'╤', u'╥', u'╙', u'╘', u'╒', u'╓', u'╫', u'╪', u'┘', u'┌', u'█', u'▄', u'▌', u'▐', u'▀',
    u'α', u'ß', u'Γ', u'π', u'Σ', u'σ', u'µ', u'τ', u'Φ', u'Θ', u'Ω', u'δ', u'∞', u'φ', u'ε', u'∩',
    u'≡', u'±', u'≥', u'≤', u'⌠', u'⌡', u'÷', u'≈', u'°', u'∙', u'·', u'√', u'ⁿ', u'²', u'■', u' '};

} // namespace

QList<int> UsenetNfoPreview::plan(const NzbInfo& nzb)
{
    QList<int> nfos;
    for (int i = 0; i < nzb.files.size() && nfos.size() < kMaxNfos; ++i) {
        const NzbFileInfo& f = nzb.files.at(i);
        const QString name = f.fileName.isEmpty() ? f.subject : f.fileName;
        if (!f.segments.isEmpty() && !f.isPar2() && QFileInfo(name).suffix().toLower() == QLatin1String("nfo")
            && f.encodedBytes() <= kMaxNfoBytes) {
            nfos.append(i);
        }
    }
    return nfos;
}

QString UsenetNfoPreview::decode(const QByteArray& data)
{
    QByteArray bytes = data;
    // a missing article leaves zeros: cut at the first one
    if (const qsizetype nul = bytes.indexOf('\0'); nul >= 0)
        bytes.truncate(nul);
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8(bytes);
    if (utf8.hasError()) {
        text.clear();
        text.reserve(bytes.size());
        for (const char c : std::as_const(bytes)) {
            const auto b = static_cast<unsigned char>(c);
            text.append(b < 0x80 ? QChar(char16_t(b)) : QChar(kCp437High[b - 0x80]));
        }
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(u'\r', u'\n');
    // trailing blank lines and the DOS end-of-file mark
    while (text.endsWith(u'\n') || text.endsWith(u' ') || text.endsWith(QChar(0x1A)))
        text.chop(1);
    return text;
}

void UsenetNfoPreview::start(const NzbInfo& nzb)
{
    m_nzb = nzb;
    m_plan = plan(nzb);
    if (m_plan.isEmpty()) {
        finish();
        return;
    }
    if (QString note; !canStart(note)) {
        finish(note);
        return;
    }
    QList<Job> jobs;
    for (int fi : std::as_const(m_plan))
        jobs.append(allSegments(fi));
    QPointer<UsenetNfoPreview> self(this);
    fetchJobs(std::move(jobs), [self] {
        if (!self)
            return;
        for (int fi : std::as_const(self->m_plan)) {
            const QString text = decode(self->readReceived(fi));
            if (text.trimmed().isEmpty())
                continue;
            const NzbFileInfo& f = self->m_nzb.files.at(fi);
            self->m_nfos.append({QFileInfo(f.fileName.isEmpty() ? f.subject : f.fileName).fileName(), text});
        }
        self->finish(self->m_nfos.isEmpty() ? tr("The NFO is not on the server.") : QString());
    });
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void UsenetNfoPreview::finish(const QString& note)
{
    if (stop())
        emit finished(m_nfos, note);
}

} // namespace eMule::usenet
