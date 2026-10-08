#include "stats/CountryCensus.h"

#include "utils/Log.h"

#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>

#include <algorithm>

namespace eMule {

namespace {

constexpr quint8 kFormatVersion = 1;
constexpr quint16 kMaxCountries = 26 * 26 + 1;

// "DE" → 0x4445; anything that is not two upper-case letters → 0 (unknown).
uint16 packCountry(const QString& cc)
{
    if (cc.size() != 2)
        return 0;
    const char16_t a = cc[0].unicode();
    const char16_t b = cc[1].unicode();
    if (a < u'A' || a > u'Z' || b < u'A' || b > u'Z')
        return 0;
    return static_cast<uint16>((a << 8) | b);
}

QString unpackCountry(uint16 key)
{
    if (key == 0)
        return {};
    return QString(QChar(key >> 8)) + QChar(key & 0xFF);
}

void mergeTally(std::map<uint16, CardinalitySketch>& into,
                const std::map<uint16, CardinalitySketch>& from)
{
    for (const auto& [key, sketch] : from)
        into[key].merge(sketch);
}

} // namespace

CountryCensus::CountryCensus(const QString& dir, const QString& baseName, quint32 magic)
    : m_dir(dir)
    , m_baseName(baseName)
    , m_magic(magic)
{
    // The salt has to outlive the session: sketches hashed with another one
    // cannot be merged. A lost or unreadable file therefore starts clean.
    if (m_dir.isEmpty() || !QFileInfo::exists(filePath()) || !readFrom(filePath(), m_base, true)) {
        m_base = {};
        newSalt();
    }
}

void CountryCensus::addByCountry(uint64 lo, uint64 hi, const QString& cc)
{
    m_session.byCountry[packCountry(cc)].add(hashOf(lo, hi));
    m_dirty = true;
}

void CountryCensus::addSecondary(uint64 lo, uint64 hi)
{
    m_session.secondary.add(hashOf(lo, hi));
    m_dirty = true;
}

uint64 CountryCensus::total(Scope scope) const
{
    const Tally tally = scope == Scope::Session ? m_session : cumulative();
    CardinalitySketch all;
    for (const auto& [key, sketch] : tally.byCountry)
        all.merge(sketch);
    return all.estimate();
}

uint64 CountryCensus::secondary(Scope scope) const
{
    return scope == Scope::Session ? m_session.secondary.estimate()
                                   : cumulative().secondary.estimate();
}

QList<CountryCensus::CountryCount> CountryCensus::countries(Scope scope) const
{
    const Tally tally = scope == Scope::Session ? m_session : cumulative();
    QList<CountryCount> out;
    out.reserve(static_cast<qsizetype>(tally.byCountry.size()));
    for (const auto& [key, sketch] : tally.byCountry) {
        if (const uint64 count = sketch.estimate(); count > 0)
            out.append({unpackCountry(key), count});
    }
    std::ranges::stable_sort(out, [](const CountryCount& a, const CountryCount& b) {
        return a.count != b.count ? a.count > b.count : a.cc < b.cc;
    });
    return out;
}

bool CountryCensus::save()
{
    if (m_dir.isEmpty() || !m_dirty)
        return true;
    if (!writeTo(filePath(), cumulative()))
        return false;
    m_dirty = false;
    return true;
}

void CountryCensus::reset()
{
    if (!m_dir.isEmpty() && !writeTo(backupPath(), cumulative()))
        logWarning(QStringLiteral("Census: could not write %1").arg(backupPath()));
    m_base = {};
    m_dirty = true;
    save();
}

bool CountryCensus::restore()
{
    if (m_dir.isEmpty() || !QFileInfo::exists(backupPath()))
        return false;

    // Stash what is about to be replaced, then put it where the backup was.
    const QString undoPath = backupPath() + QStringLiteral(".undo");
    if (!writeTo(undoPath, cumulative()))
        return false;

    Tally restored;
    if (!readFrom(backupPath(), restored, false)) {
        QFile::remove(undoPath);
        return false;
    }
    QFile::remove(backupPath());
    if (!QFile::rename(undoPath, backupPath()))
        logWarning(QStringLiteral("Census: could not swap %1").arg(undoPath));

    m_base = std::move(restored);
    m_dirty = true;
    return save();
}

QString CountryCensus::filePath() const
{
    return m_dir + QLatin1Char('/') + m_baseName + QStringLiteral(".dat");
}

QString CountryCensus::backupPath() const
{
    return m_dir + QLatin1Char('/') + m_baseName + QStringLiteral(".bak");
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

uint64 CountryCensus::hashOf(uint64 lo, uint64 hi) const
{
    return CardinalitySketch::keyedHash(lo, hi, m_salt0, m_salt1);
}

CountryCensus::Tally CountryCensus::cumulative() const
{
    Tally out = m_base;
    mergeTally(out.byCountry, m_session.byCountry);
    out.secondary.merge(m_session.secondary);
    return out;
}

bool CountryCensus::writeTo(const QString& path, const Tally& tally) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_0);
    out.setByteOrder(QDataStream::LittleEndian);
    out << m_magic << kFormatVersion << static_cast<quint8>(CardinalitySketch::kPrecision)
        << static_cast<quint64>(m_salt0) << static_cast<quint64>(m_salt1);
    tally.secondary.write(out);
    out << static_cast<quint16>(tally.byCountry.size());
    for (const auto& [key, sketch] : tally.byCountry) {
        out << static_cast<quint16>(key);
        sketch.write(out);
    }
    return out.status() == QDataStream::Ok && file.commit();
}

bool CountryCensus::readFrom(const QString& path, Tally& tally, bool adoptSalt)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);
    in.setByteOrder(QDataStream::LittleEndian);

    quint32 magic = 0;
    quint8 version = 0;
    quint8 precision = 0;
    quint64 salt0 = 0;
    quint64 salt1 = 0;
    in >> magic >> version >> precision >> salt0 >> salt1;
    if (in.status() != QDataStream::Ok || magic != m_magic || version != kFormatVersion
        || precision != CardinalitySketch::kPrecision) {
        logWarning(QStringLiteral("Census: %1 is not readable, ignoring it").arg(path));
        return false;
    }
    // A backup hashed with another salt (the census file was lost in between)
    // cannot be merged with what this session counts.
    if (!adoptSalt && (salt0 != m_salt0 || salt1 != m_salt1)) {
        logWarning(QStringLiteral("Census: %1 belongs to another census").arg(path));
        return false;
    }

    Tally loaded;
    quint16 count = 0;
    if (!loaded.secondary.read(in))
        return false;
    in >> count;
    if (in.status() != QDataStream::Ok || count > kMaxCountries)
        return false;
    for (quint16 i = 0; i < count; ++i) {
        quint16 key = 0;
        in >> key;
        if (!loaded.byCountry[key].read(in))
            return false;
    }

    if (adoptSalt) {
        m_salt0 = salt0;
        m_salt1 = salt1;
    }
    tally = std::move(loaded);
    return true;
}

void CountryCensus::newSalt()
{
    m_salt0 = QRandomGenerator::system()->generate64();
    m_salt1 = QRandomGenerator::system()->generate64();
}

} // namespace eMule
