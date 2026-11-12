#pragma once

/// @file AtomicFile.h
/// @brief QSaveFile without the forced disk flush.
///
/// QSaveFile::commit() always syncs (FlushFileBuffers on Windows) before the rename: on a
/// busy or slow drive that held the core's main thread for many seconds, until the GUI
/// gave up on it (report 43). AtomicFile writes a temporary file next to the target and
/// renames it over: a crash still leaves the old file or the new one, the data reaches
/// the disk when the OS writes its cache (as MFC eMule's saves).
///
/// Only for a main-thread save whose caller needs the result now (Preferences::save for
/// an IPC reply, a stats backup). Timed or frequent saves go to BackgroundFileWriter:
/// off the main thread, synced there.

#include <QFile>
#include <QString>

namespace eMule {

class AtomicFile : public QFile {
public:
    explicit AtomicFile(const QString& path);
    ~AtomicFile() override;   ///< not committed: the temporary file goes
    AtomicFile(const AtomicFile&) = delete;
    AtomicFile& operator=(const AtomicFile&) = delete;

    /// Opens a new temporary file beside the target, the target's permissions when it exists.
    bool open(OpenMode mode) override;
    /// Closes and renames the temporary file over the target; false: a write or the rename
    /// failed, the target is untouched.
    bool commit();
    /// Drops what was written; the target is untouched.
    void cancelWriting();

    [[nodiscard]] const QString& targetName() const { return m_target; }

protected:
    qint64 writeData(const char* data, qint64 len) override;

private:
    bool replaceTarget();

    QString m_target;
    bool m_tempMade = false;
    bool m_writeError = false;   ///< as QSaveFile: only a failed write fails the commit
};

} // namespace eMule
