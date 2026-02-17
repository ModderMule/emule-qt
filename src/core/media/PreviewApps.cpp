// This file is part of eMule
// Copyright (C) 2002-2024 Merkur
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from MFC CPreviewApps — config parser + preview-ability checker.

#include "media/PreviewApps.h"
#include "files/PartFile.h"
#include "utils/DebugUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace eMule {

// ===================================================================
// Public API
// ===================================================================

int PreviewApps::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcEmuleFile) << "PreviewApps: cannot open config file:" << filePath;
        return 0;
    }

    m_apps.clear();

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (auto app = parseLine(line))
            m_apps.push_back(std::move(*app));
    }

    return static_cast<int>(m_apps.size());
}

bool PreviewApps::canPreview(const PartFile* file) const
{
    if (!file)
        return false;

    for (const auto& app : m_apps) {
        if (matchesFile(app, file))
            return true;
    }
    return false;
}

std::optional<PreviewApp> PreviewApps::findPreviewApp(const PartFile* file) const
{
    if (!file)
        return std::nullopt;

    for (const auto& app : m_apps) {
        if (matchesFile(app, file))
            return app;
    }
    return std::nullopt;
}

std::vector<std::pair<QString, int>> PreviewApps::menuEntries(const PartFile* file) const
{
    std::vector<std::pair<QString, int>> entries;
    if (!file)
        return entries;

    for (size_t i = 0; i < m_apps.size(); ++i) {
        if (matchesFile(m_apps[i], file))
            entries.emplace_back(m_apps[i].title, static_cast<int>(i));
    }
    return entries;
}

std::optional<PreviewApp> PreviewApps::parseLine(const QString& line)
{
    const QString trimmed = line.trimmed();

    // Skip empty lines and comments
    if (trimmed.isEmpty() || trimmed.startsWith(u'#'))
        return std::nullopt;

    // Tab-separated: Title<TAB>ext1,ext2<TAB>minStartHex<TAB>minCompletedHex<TAB>command<TAB>args
    const QStringList fields = trimmed.split(u'\t');
    if (fields.size() < 5) {
        qCDebug(lcEmuleFile) << "PreviewApps: skipping malformed line (too few fields):" << trimmed;
        return std::nullopt;
    }

    PreviewApp app;
    app.title = fields[0].trimmed();
    if (app.title.isEmpty())
        return std::nullopt;

    // Parse extensions (comma-separated, case-insensitive)
    const QStringList extList = fields[1].split(u',', Qt::SkipEmptyParts);
    for (const auto& ext : extList)
        app.extensions.append(ext.trimmed().toLower());

    if (app.extensions.isEmpty())
        return std::nullopt;

    // Parse hex sizes
    bool ok = false;
    app.minStartOfFile = fields[2].trimmed().toULongLong(&ok, 16);
    if (!ok)
        app.minStartOfFile = 0;

    ok = false;
    app.minCompletedSize = fields[3].trimmed().toULongLong(&ok, 16);
    if (!ok)
        app.minCompletedSize = 0;

    // Command
    app.command = fields[4].trimmed();
    if (app.command.isEmpty())
        return std::nullopt;

    // Optional args (may be absent)
    if (fields.size() > 5)
        app.commandArgs = fields[5].trimmed();

    return app;
}

// ===================================================================
// Private helpers
// ===================================================================

bool PreviewApps::matchesFile(const PreviewApp& app, const PartFile* file) const
{
    // Skip files in error state
    if (file->status() == PartFileStatus::Error)
        return false;

    // Check file extension
    const QString fileName = file->fileName();
    const auto dotPos = fileName.lastIndexOf(u'.');
    if (dotPos < 0)
        return false;

    const QString ext = fileName.mid(dotPos + 1).toLower();
    bool extMatch = false;
    for (const auto& appExt : app.extensions) {
        if (ext == appExt) {
            extMatch = true;
            break;
        }
    }
    if (!extMatch)
        return false;

    // Check minimum start-of-file bytes are complete
    if (app.minStartOfFile > 0) {
        if (!file->isComplete(0, app.minStartOfFile - 1))
            return false;
    }

    // Check minimum total completed size
    if (app.minCompletedSize > 0) {
        if (static_cast<uint64>(file->completedSize()) < app.minCompletedSize)
            return false;
    }

    return true;
}

} // namespace eMule
