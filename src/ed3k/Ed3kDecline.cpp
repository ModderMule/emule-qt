#include "Ed3kDecline.h"

#include <QByteArray>
#include <QFile>

namespace eMule::ed3k {

DeclineList parseDeclineList(const QString& text)
{
    DeclineList out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.size() == 64) {
            const QByteArray bytes = QByteArray::fromHex(line.toLatin1());
            // fromHex skips bad characters: a root only when all 64 were hex
            if (bytes.size() == 32 && QString::fromLatin1(bytes.toHex()).compare(line, Qt::CaseInsensitive) == 0) {
                ::ed3k::Root root;
                std::copy(bytes.cbegin(), bytes.cend(), root.bytes.begin());
                out.roots.push_back(root);
                continue;
            }
        }
        out.keywords.append(line);
    }
    return out;
}

DeclineList loadDeclineList(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return parseDeclineList(QString::fromUtf8(file.readAll()));
}

} // namespace eMule::ed3k
