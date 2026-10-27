#pragma once

/// @file MassRename.h
/// @brief Line-wise filename edits behind the Mass Rename dialog (MorphXT MassRename.cpp,
///        SimpleCleanup.cpp). Pure functions, no GUI.

#include <QString>
#include <QStringList>

#include <expected>

namespace eMule::massrename {

/// One edit the user made in the mask line, captured around the change.
struct MaskEdit {
    qsizetype start1 = 0;   ///< selection before the edit
    qsizetype end1 = 0;
    qsizetype start2 = 0;   ///< selection (cursor) after the edit
    qsizetype end2 = 0;
    QString before;         ///< mask text before the edit
    QString after;          ///< mask text after the edit
};

/// True when @p edit only removed characters at a collapsed cursor (Backspace / Del).
[[nodiscard]] bool isDeletion(const MaskEdit& edit);

/// Repeats the mask edit on every line (MorphXT OnEnChangeFilenamemaskedit). In
/// right-justified mode positions count from the line end. Lines too short are left alone.
[[nodiscard]] QStringList applyMaskEdit(QStringList lines, const MaskEdit& edit, bool rightJustified);

/// Inserts one clipboard line per filename, from @p startLine on, at column
/// [@p startX, @p endX) of a line @p refLength long; the span is replaced
/// (MorphXT OnBnClickedInserttextcolumn). Stops when the clipboard runs out.
[[nodiscard]] QStringList insertColumn(QStringList lines, const QString& clipboard,
                                       int startLine, qsizetype startX, qsizetype endX,
                                       qsizetype refLength, bool rightJustified);

/// Simple cleanup option bits (MorphXT SCO_*; same values, stored in prefs).
enum SimpleCleanupFlag : int {
    DotToSpace          = 1,
    UnderlineToSpace    = 2,
    ReplaceApostrophe   = 4,
    ReplaceHex          = 8,
    SearchAndReplace    = 16,
    SearchCaseSensitive = 32,
    ReplaceChars        = 64,
};

struct SimpleCleanupOptions {
    int flags = DotToSpace | UnderlineToSpace;
    QString search;
    QString replace;
    QStringList searchChars;    ///< pairs with replaceChars by index
    QStringList replaceChars;
};

/// Name and extension are cleaned apart, so the extension dot survives (MorphXT SimpleCleanupFilename).
[[nodiscard]] QString simpleCleanupFilename(const QString& filename, const SimpleCleanupOptions& opt);

/// Replaces every @p source by @p dest; replaced text is not searched again.
void replaceAll(QString& str, const QString& source, const QString& dest, bool caseSensitive);

struct NameError {
    enum Kind { NotEnough, Invalid, Duplicate };
    Kind kind;
    int line = 0;           ///< 1-based, 0 for NotEnough
};

/// Checks the edited lines before renaming: one name per file in @p dirs, trimmed, no
/// path characters, no two names alike in one directory (case-insensitive).
/// @return the trimmed names, one per file.
[[nodiscard]] std::expected<QStringList, NameError>
validateNewNames(const QStringList& lines, const QStringList& dirs);

/// True when @p name can't be a file name: empty, "." / "..", or holds \ / : * ? " < > | or a control char.
[[nodiscard]] bool isInvalidFileName(const QString& name);

} // namespace eMule::massrename
