#include "pch.h"
/// @file MassRename.cpp
/// @brief Line-wise filename edits behind the Mass Rename dialog — implementation.

#include "utils/MassRename.h"

#include <QSet>

#include <algorithm>

namespace eMule::massrename {

namespace {

void reconstructHex(QString& str)
{
    // "%xx" back to a char; control codes stay escaped (MorphXT ReconstructCharacters)
    for (qsizetype p = 0; p + 2 < str.size(); ++p) {
        if (str[p] != u'%')
            continue;
        bool ok = false;
        const int c = QStringView(str).mid(p + 1, 2).toInt(&ok, 16);
        if (ok && c > 31 && str[p + 1].isLetterOrNumber() && str[p + 2].isLetterOrNumber()) {
            str[p] = QChar(c);
            str.remove(p + 1, 2);
        }
    }
}

void cleanPart(QString& part, const SimpleCleanupOptions& opt)
{
    const int f = opt.flags;
    if (f & DotToSpace)
        replaceAll(part, QStringLiteral("."), QStringLiteral(" "), false);
    if (f & UnderlineToSpace)
        replaceAll(part, QStringLiteral("_"), QStringLiteral(" "), false);
    if (f & ReplaceApostrophe) {
        replaceAll(part, QStringLiteral("´"), QStringLiteral("'"), false);
        replaceAll(part, QStringLiteral("`"), QStringLiteral("'"), false);
    }
    if (f & ReplaceHex)
        reconstructHex(part);
    if (f & SearchAndReplace)
        replaceAll(part, opt.search, opt.replace, (f & SearchCaseSensitive) != 0);
    if ((f & ReplaceChars) && !opt.searchChars.isEmpty()
        && opt.searchChars.size() == opt.replaceChars.size()) {
        for (qsizetype i = 0; i < opt.searchChars.size(); ++i)
            replaceAll(part, opt.searchChars[i], opt.replaceChars[i], true);
    }
}

} // namespace

bool isDeletion(const MaskEdit& e)
{
    return e.start1 == e.end1 && e.start2 == e.end2 && e.start2 <= e.start1
        && e.after.size() < e.before.size();
}

QStringList applyMaskEdit(QStringList lines, const MaskEdit& e, bool right)
{
    const qsizetype beforeLen = e.before.size();
    if (isDeletion(e)) {
        // Backspace/Del, possibly a whole word (Morph removed just one char)
        const qsizetype count = beforeLen - e.after.size();
        const qsizetype fromEnd = beforeLen - e.start2;
        for (QString& line : lines) {
            const qsizetype pos = right ? line.size() - fromEnd : e.start2;
            if (pos < 0 || pos >= line.size())
                continue;
            line.remove(pos, count);
        }
        return lines;
    }

    // Selection [start1,end1) was replaced by the typed/pasted chars
    const qsizetype newLen = std::max<qsizetype>(e.end2 - e.start1, 0);
    const QString newChars = e.after.mid(e.start1, newLen);
    const qsizetype startR = beforeLen - e.start1;
    const qsizetype endR = beforeLen - e.end1;
    for (QString& line : lines) {
        const qsizetype llen = line.size();
        qsizetype from = 0;
        qsizetype to = 0;
        if (!right) {
            if (e.start1 > llen)
                continue;
            from = e.start1;
            to = std::min(e.end1, llen);
        } else {
            if (llen - startR < 0)
                continue;
            from = llen - startR;
            to = std::max(llen - endR, from);
        }
        line.replace(from, to - from, newChars);
    }
    return lines;
}

QStringList insertColumn(QStringList lines, const QString& clipboard, int startLine,
                         qsizetype startX, qsizetype endX, qsizetype refLength, bool right)
{
    QString clip = clipboard;
    clip.remove(u'\r');
    if (clip.isEmpty())
        return lines;
    const QStringList chunks = clip.split(u'\n');
    if (endX < startX)
        std::swap(startX, endX);
    if (right) {
        // Columns count from the line end from here on
        startX = refLength - startX;
        endX = refLength - endX;
    }

    qsizetype next = 0;
    for (qsizetype i = std::max(startLine, 0); i < lines.size() && next < chunks.size(); ++i) {
        QString& name = lines[i];
        const QString& ins = chunks[next++];
        if (!right) {
            const qsizetype at = std::min(startX, name.size());
            name.remove(at, std::max<qsizetype>(endX - startX, 0));
            name.insert(at, ins);
        } else {
            const qsizetype at = std::clamp<qsizetype>(name.size() - startX, 0, name.size());
            name.insert(at, ins);
            const qsizetype delAt = name.size() - startX;
            if (delAt >= 0)
                name.remove(delAt, std::max<qsizetype>(startX - endX, 0));
        }
    }
    return lines;
}

void replaceAll(QString& str, const QString& source, const QString& dest, bool caseSensitive)
{
    if (str.isEmpty() || source.isEmpty())
        return;
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    qsizetype p = 0;
    while ((p = str.indexOf(source, p, cs)) >= 0) {
        str.replace(p, source.size(), dest);
        p += dest.size();
    }
}

QString simpleCleanupFilename(const QString& filename, const SimpleCleanupOptions& opt)
{
    if (filename.isEmpty())
        return {};
    const qsizetype lastDot = filename.lastIndexOf(u'.');
    QString name = lastDot < 0 ? filename : filename.left(lastDot);
    QString ext = lastDot < 0 ? QString() : filename.mid(lastDot + 1);
    cleanPart(name, opt);
    cleanPart(ext, opt);
    return lastDot < 0 ? name : name + u'.' + ext;
}

bool isInvalidFileName(const QString& name)
{
    if (name.isEmpty() || name == u"." || name == u"..")
        return true;
    static constexpr QStringView bad = u"\\/:*?\"<>|";
    return std::ranges::any_of(name, [](QChar c) { return c < QChar(u' ') || bad.contains(c); });
}

std::expected<QStringList, NameError> validateNewNames(const QStringList& lines, const QStringList& dirs)
{
    if (lines.size() < dirs.size())
        return std::unexpected(NameError{NameError::NotEnough, 0});

    QStringList names;
    names.reserve(dirs.size());
    QSet<QString> seen;
    for (qsizetype i = 0; i < dirs.size(); ++i) {
        const QString name = lines[i].trimmed();
        const int line = static_cast<int>(i) + 1;
        if (isInvalidFileName(name))
            return std::unexpected(NameError{NameError::Invalid, line});
        QString key = dirs[i];
        if (!key.endsWith(u'/') && !key.endsWith(u'\\'))
            key += u'/';
        key = QString(key + name).toCaseFolded();
        if (seen.contains(key))
            return std::unexpected(NameError{NameError::Duplicate, line});
        seen.insert(key);
        names.append(name);
    }
    return names;
}

} // namespace eMule::massrename
