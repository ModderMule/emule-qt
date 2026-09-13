#include "post/Par2NameIndex.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace eMule::usenet {

namespace {

/// The lookup key: the length and the 16 KiB digest, together.
QByteArray keyFor(qint64 size, const QByteArray& hash16k)
{
    return QByteArray::number(size) + ':' + hash16k;
}

} // namespace

void Par2NameIndex::setFiles(const QList<Par2SetFile>& files)
{
    m_byKey.clear();
    m_claimed.clear();

    for (const Par2SetFile& f : files) {
        if (!f.recoverable || f.fileName.isEmpty() || f.size <= 0 || f.hash16k.size() != 16)
            continue;

        const QByteArray key = keyFor(f.size, f.hash16k);
        const auto it = m_byKey.constFind(key);
        if (it == m_byKey.constEnd()) {
            m_byKey.insert(key, f.fileName);
            continue;
        }

        // Two entries of the same length with the same opening bytes. Both names
        // are real; which one this file is, is not knowable from here. An empty
        // marker keeps the key present so a later insert cannot resurrect it.
        if (*it != f.fileName)
            m_byKey.insert(key, QString());
    }
}

qint64 Par2NameIndex::bytesNeededFor(qint64 declaredSize)
{
    if (declaredSize <= 0)
        return kPar2Hash16kBytes;
    return qMin<qint64>(kPar2Hash16kBytes, declaredSize);
}

QString Par2NameIndex::match(qint64 size, const QByteArray& hash16k) const
{
    if (size <= 0 || hash16k.size() != 16)
        return {};
    return m_byKey.value(keyFor(size, hash16k));
}

QString Par2NameIndex::matchFile(const QString& path, qint64 declaredSize) const
{
    if (m_byKey.isEmpty() || declaredSize <= 0)
        return {};

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    const qint64 want = bytesNeededFor(declaredSize);
    const QByteArray head = f.read(want);
    if (head.size() != want)
        return {};

    return match(declaredSize, QCryptographicHash::hash(head, QCryptographicHash::Md5));
}

bool Par2NameIndex::claim(const QString& name)
{
    if (name.isEmpty() || m_claimed.contains(name))
        return false;
    m_claimed.insert(name);
    return true;
}

} // namespace eMule::usenet
