#pragma once

/// @file PreviewApps.h
/// @brief Preview application configuration — port of MFC CPreviewApps.
///
/// Parses PreviewApps.dat and determines whether a PartFile can be previewed
/// based on extension, completed bytes, and start-of-file availability.
///
/// A line is MFC's `Title=command args;ext=avi;ext=mkv;minsize=N;minstart=N`
/// (srchybrid/Preview.cpp:147-222, sizes decimal); the tab-separated form this
/// port started with is still read.

#include "utils/Types.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <utility>
#include <vector>

namespace eMule {

class PartFile;

// ---------------------------------------------------------------------------
// PreviewApp — single preview application entry
// ---------------------------------------------------------------------------

struct PreviewApp {
    QString title;
    QStringList extensions;       // {"avi", "mkv", "mp4"}
    uint64 minStartOfFile = 0;    // min bytes from start that must be complete
    uint64 minCompletedSize = 0;  // min total completed bytes
    QString command;              // path to executable
    QString commandArgs;          // arg template (%1 = file path)
};

// ---------------------------------------------------------------------------
// PreviewApps — config parser + preview-ability checker
// ---------------------------------------------------------------------------

class PreviewApps {
public:
    /// Load preview app definitions from a tab-separated config file.
    /// Returns the number of valid entries loaded.
    [[nodiscard]] int loadFromFile(const QString& filePath);

    /// All loaded preview apps.
    [[nodiscard]] const std::vector<PreviewApp>& apps() const { return m_apps; }

    /// Number of loaded preview apps.
    [[nodiscard]] int count() const { return static_cast<int>(m_apps.size()); }

    /// Check whether any loaded app can preview the given file.
    [[nodiscard]] bool canPreview(const PartFile* file) const;

    /// Find the first matching preview app for the given file.
    [[nodiscard]] std::optional<PreviewApp> findPreviewApp(const PartFile* file) const;

    /// Return (title, index) pairs for all apps that can handle the file.
    /// Suitable for populating a context menu.
    [[nodiscard]] std::vector<std::pair<QString, int>> menuEntries(const PartFile* file) const;

    /// The app MFC's plain Preview hands a file to: the last entry listing the file's
    /// extension, or -1 (MFC GetPreviewApp).
    [[nodiscard]] int appForFileName(const QString& fileName) const;

    /// Parse a single config line into a PreviewApp.
    /// Returns std::nullopt for comments, blank lines, or malformed entries.
    [[nodiscard]] static std::optional<PreviewApp> parseLine(const QString& line);

private:
    bool matchesFile(const PreviewApp& app, const PartFile* file) const;
    [[nodiscard]] static std::optional<PreviewApp> parseMfcLine(const QString& line);

    std::vector<PreviewApp> m_apps;
};

} // namespace eMule
