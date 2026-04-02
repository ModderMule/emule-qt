#pragma once

/// @file WebServices.h
/// @brief Web services menu integration — parses webservices.dat config file.
///
/// Populates context menus with external web lookup links for files,
/// replacing macros (#hashid, #filesize, etc.) with file metadata.
/// Port of MFC CWebServices (srchybrid/WebServices.h).

#include <QString>
#include <QDateTime>

#include <vector>

class QMenu;

namespace eMule {

struct WebServiceEntry {
    QString label;
    QString urlTemplate;
    bool    hasFileMacros = false;
};

class WebServices {
public:
    static WebServices& instance();

    /// Re-read webservices.dat if the file has been modified since last load.
    void reload();

    /// Load and parse a webservices.dat file at the given path.
    /// Returns true if the file was opened and parsed successfully.
    bool loadFromFile(const QString& path);

    /// Populate a QMenu with web service entries that use file macros.
    /// Each action opens the URL in the default browser after macro expansion.
    /// @param menu     Target menu to add actions to.
    /// @param hash     File hash as hex string (for #hashid).
    /// @param fileName File name (for #filename, #name, #cleanfilename, #cleanname).
    /// @param fileSize File size in bytes (for #filesize).
    void populateFileMenu(QMenu* menu, const QString& hash,
                          const QString& fileName, uint64_t fileSize);

    [[nodiscard]] const std::vector<WebServiceEntry>& services() const { return m_services; }

    WebServices() = default;

private:

    [[nodiscard]] QString servicesFilePath() const;
    static QString expandMacros(const QString& urlTemplate, const QString& hash,
                                const QString& fileName, uint64_t fileSize);
    static QString cleanupFilename(const QString& name, bool keepExtension = true);

    std::vector<WebServiceEntry> m_services;
    QDateTime m_lastModified;
};

} // namespace eMule
