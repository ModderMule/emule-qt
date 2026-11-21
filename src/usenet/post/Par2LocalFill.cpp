/// @file Par2LocalFill.cpp
/// @brief #224 stored archives filled from disk — implementation.

#include "post/Par2LocalFill.h"

#include "post/Par2Verifier.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <map>
#include <memory>

namespace eMule::usenet::Par2LocalFill {

QList<Range> fill(const QString& par2Path, const QStringList& extraFiles, const QHash<QString, QString>& targets,
                  const std::atomic<bool>* cancel)
{
    QList<Range> out;
    if (extraFiles.isEmpty() || targets.isEmpty())
        return out;
    Par2Verifier v;
    if (cancel)
        v.setCancelCheck([cancel] { return cancel->load(); });
    QList<Par2FoundBlock> found = v.locate(par2Path, QFileInfo(par2Path).path(), extraFiles);
    std::ranges::sort(found, [](const Par2FoundBlock& a, const Par2FoundBlock& b) {
        return a.fileName != b.fileName ? a.fileName < b.fileName : a.offset < b.offset;
    });
    // one handle per file: sources read, targets written
    std::map<QString, std::unique_ptr<QFile>> sources;
    std::map<QString, std::unique_ptr<QFile>> written;
    for (const Par2FoundBlock& b : std::as_const(found)) {
        if (cancel && cancel->load())
            break;
        // a set's name may carry a path (untrusted): only its file name is looked up
        const QString target = targets.value(QFileInfo(b.fileName).fileName().toLower());
        if (target.isEmpty() || b.length <= 0)
            continue;
        auto& src = sources[b.source];
        if (!src) {
            src = std::make_unique<QFile>(b.source);
            if (!src->open(QIODevice::ReadOnly))
                continue;
        }
        if (!src->isOpen() || !src->seek(b.sourceOffset))
            continue;
        const QByteArray data = src->read(b.length);
        if (data.size() != b.length)
            continue;
        auto& dst = written[target];
        if (!dst) {
            if (!QFileInfo(target).dir().exists())
                continue;
            dst = std::make_unique<QFile>(target);
            if (!dst->open(QIODevice::ReadWrite)) {   // never truncated: articles may be there
                logUsenetWarning(QStringLiteral("Usenet: cannot write \"%1\" from disk").arg(target));
                continue;
            }
        }
        if (!dst->isOpen() || !dst->seek(b.offset) || dst->write(data) != data.size())
            continue;
        // adjacent to the last range of the same file: one range
        if (!out.isEmpty() && out.last().fileName == b.fileName && out.last().offset + out.last().length == b.offset)
            out.last().length += b.length;
        else
            out.append({b.fileName, b.offset, b.length});
    }
    for (auto& [path, f] : written)
        if (f->isOpen())
            f->flush();
    return out;
}

} // namespace eMule::usenet::Par2LocalFill
