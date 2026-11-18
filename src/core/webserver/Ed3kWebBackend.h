#pragma once

/// @file Ed3kWebBackend.h
/// @brief What the REST API may ask of the plain ed3k downloads.
///
/// Core never links the ed3k module, so the /api/v1/ed3k routes talk to this
/// interface and DaemonApp installs the implementation — the HosterWebBackend seam.
/// Rows are GetEd3kDownloads maps, so the GUI and the API read one vocabulary.

#include <QCborArray>
#include <QString>

namespace eMule {

class Ed3kWebBackend {
public:
    virtual ~Ed3kWebBackend() = default;

    /// False without an ed3k node (switched off, or not built).
    [[nodiscard]] virtual bool available() const = 0;
    /// The GetEd3kDownloads rows.
    [[nodiscard]] virtual QCborArray downloads() const = 0;

    struct AddResult {
        QString root;    ///< 64 hex; empty on failure
        QString error;   ///< English
    };
    /// A download of @p hexRoot (64 hex), as an ed3k search row adds it.
    virtual AddResult add(const QString& hexRoot, qint64 size, const QString& name, int category,
                          bool paused) = 0;
    /// An ed3k:// link.
    virtual AddResult addLink(const QString& link, int category, bool paused) = 0;

    /// Ed3kDownloadControl actions; @p arg is the priority or category.
    enum class Control : int { Pause = 0, Resume = 1, Cancel = 2, Priority = 3, Category = 4,
                               ClearCompleted = 5, PreviewFirst = 6, CancelDelete = 7 };
    /// False: unknown download, or refused.
    virtual bool control(const QString& hexRoot, Control action, qint64 arg) = 0;
    /// "Move to" of an unfinished download; empty: started, else why not.
    virtual QString moveTo(const QString& hexRoot, const QString& dir) = 0;
    /// Rename; empty: done, else why not.
    virtual QString rename(const QString& hexRoot, const QString& name) = 0;
};

} // namespace eMule
