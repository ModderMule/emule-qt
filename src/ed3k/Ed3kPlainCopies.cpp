/// @file Ed3kPlainCopies.cpp
/// @brief The opened plain copies of sealed set members — implementation.

#include "Ed3kPlainCopies.h"

#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace eMule::ed3k {

namespace {

const QString kPlainFile = QStringLiteral("plain.json");

} // namespace

Ed3kPlainCopies::Ed3kPlainCopies(const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_configDir(configDir)
{
    load();
}

Ed3kPlainCopies::~Ed3kPlainCopies() = default;

void Ed3kPlainCopies::add(const QString& path)
{
    const QString key = keyOf(path);
    if (key.isEmpty())
        return;
    {
        QMutexLocker lock(&m_mutex);
        if (m_paths.contains(key))
            return;
        m_paths.insert(key, QFileInfo(path).absoluteFilePath());
    }
    save();
    emit added(path);
}

bool Ed3kPlainCopies::contains(const QString& path) const
{
    const QString key = keyOf(path);
    QMutexLocker lock(&m_mutex);
    return !key.isEmpty() && m_paths.contains(key);
}

QStringList Ed3kPlainCopies::paths() const
{
    QMutexLocker lock(&m_mutex);
    QStringList out = m_paths.values();
    out.sort();
    return out;
}

QString Ed3kPlainCopies::statePath() const
{
    return QDir(stateDir(m_configDir)).filePath(kPlainFile);
}

QString Ed3kPlainCopies::keyOf(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toCaseFolded();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kPlainCopies::load()
{
    const QJsonObject root = QJsonDocument::fromJson(readBlob(statePath())).object();
    QMutexLocker lock(&m_mutex);
    for (const QJsonValue& v : root.value(QStringLiteral("plain")).toArray()) {
        const QString path = v.toString();
        // a copy the user deleted is no longer kept here
        if (!path.isEmpty() && QFileInfo::exists(path))
            m_paths.insert(keyOf(path), path);
    }
}

void Ed3kPlainCopies::save() const
{
    const QByteArray out = QJsonDocument(QJsonObject{{QStringLiteral("plain"), QJsonArray::fromStringList(paths())}}).toJson(QJsonDocument::Compact);
    QDir().mkpath(stateDir(m_configDir));
    // owner-only: the member paths of a hidden set tell what it holds
    writeBlobLater(statePath(), {reinterpret_cast<const std::byte*>(out.constData()), std::size_t(out.size())});
}

} // namespace eMule::ed3k
