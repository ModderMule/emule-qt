#include "PasswordFlagStore.h"

#include "IndexerCapsStore.h"

#include "utils/Log.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>

namespace eMule::indexer {

namespace {

constexpr int kVersion = 1;
constexpr int kSaltBytes = 16;
constexpr int kIdBytes = 8;
constexpr int kSaveDelayMs = 5000;

} // namespace

PasswordFlagStore::PasswordFlagStore(const QString& path, QObject* parent)
    : QObject(parent)
    , m_path(path)
{
    load();
    if (m_salt.size() != kSaltBytes) {
        m_tallies.clear();
        m_salt.resize(kSaltBytes);
        QRandomGenerator::system()->generate(m_salt.begin(), m_salt.end());
    }
}

PasswordFlagStore::~PasswordFlagStore()
{
    flush();
}

QString PasswordFlagStore::defaultPath()
{
    return QDir(IndexerCapsStore::directory()).filePath(QStringLiteral("PasswordFlags.json"));
}

void PasswordFlagStore::apply(const QString& slug, IndexerSearchPage& page)
{
    if (page.results.isEmpty())
        return;
    m_tallies[idFor(slug)].apply(page, [this, &slug](const QString& flag) {
        return idFor(slug, flag);
    });
    scheduleSave();
}

void PasswordFlagStore::forget(const QString& slug)
{
    if (m_tallies.remove(idFor(slug)) > 0)
        scheduleSave();
}

void PasswordFlagStore::flush()
{
    if (m_saveTimer)
        m_saveTimer->stop();
    if (!m_dirty || m_path.isEmpty())
        return;
    m_dirty = false;

    QJsonObject indexers;
    for (auto it = m_tallies.cbegin(); it != m_tallies.cend(); ++it) {
        QJsonObject flags;
        const QHash<QString, int>& counts = it->counts();
        for (auto c = counts.cbegin(); c != counts.cend(); ++c)
            flags.insert(c.key(), c.value());
        indexers.insert(it.key(), QJsonObject{{QStringLiteral("rows"), it->rows()},
                                              {QStringLiteral("flags"), flags}});
    }

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(QJsonObject{
                                        {QStringLiteral("version"), kVersion},
                                        {QStringLiteral("salt"), QString::fromLatin1(m_salt.toHex())},
                                        {QStringLiteral("indexers"), indexers}})
                          .toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) {
        logWarning(QStringLiteral("Indexers: could not store the password flag counts: %1")
                       .arg(file.errorString()));
    }
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void PasswordFlagStore::load()
{
    if (m_path.isEmpty())
        return;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != kVersion)
        return;
    m_salt = QByteArray::fromHex(root.value(QStringLiteral("salt")).toString().toLatin1());
    if (m_salt.size() != kSaltBytes)
        return;

    const QJsonObject indexers = root.value(QStringLiteral("indexers")).toObject();
    for (auto it = indexers.constBegin(); it != indexers.constEnd(); ++it) {
        const QJsonObject entry = it->toObject();
        QHash<QString, int> counts;
        const QJsonObject flags = entry.value(QStringLiteral("flags")).toObject();
        for (auto f = flags.constBegin(); f != flags.constEnd(); ++f)
            counts.insert(f.key(), f->toInt());
        PasswordFlagTally tally;
        tally.restore(entry.value(QStringLiteral("rows")).toInt(), counts);
        if (tally.rows() > 0)
            m_tallies.insert(it.key(), tally);
    }
}

void PasswordFlagStore::scheduleSave()
{
    m_dirty = true;
    if (m_path.isEmpty())
        return;
    if (!m_saveTimer) {
        m_saveTimer = new QTimer(this);
        m_saveTimer->setSingleShot(true);
        m_saveTimer->setInterval(kSaveDelayMs);
        connect(m_saveTimer, &QTimer::timeout, this, [this] { flush(); });
    }
    if (!m_saveTimer->isActive())
        m_saveTimer->start();
}

QString PasswordFlagStore::idFor(const QString& slug, const QString& flag) const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(m_salt);
    hash.addData(slug.toUtf8());
    if (!flag.isEmpty()) {
        hash.addData(QByteArrayView("\0", 1));
        hash.addData(flag.toUtf8());
    }
    return QString::fromLatin1(hash.result().left(kIdBytes).toHex());
}

} // namespace eMule::indexer
