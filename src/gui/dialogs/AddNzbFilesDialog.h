#pragma once

/// @file AddNzbFilesDialog.h
/// @brief Confirm the .nzb files picked from disk, and offer a password.
///
/// "Add NZB…" used to be a bare QFileDialog, which left nowhere to say that a
/// release is password-protected — and a passphrase supplied at add time is the
/// only way an encrypted set unpacks without failing first and being retried.
///
/// It shares PasteTextDialog with AddNzbUrlDialog rather than being another
/// hand-built form: the two now differ only in whether the box is typed into or
/// filled in, which is what Chrome::readOnlyText says.

#include "dialogs/PasteTextDialog.h"

namespace eMule {

class AddNzbFilesDialog : public PasteTextDialog {
    Q_OBJECT

public:
    /// @p paths are the files QFileDialog returned; the box lists them.
    /// @p categories is the user's category list in their own order, index 0
    /// first — passed in rather than fetched, because the panel already holds it
    /// for its own tab strip and a second GetCategories would race it.
    explicit AddNzbFilesDialog(const QStringList& paths, const QStringList& categories,
                               QWidget* parent = nullptr);

    /// The passphrase the user typed, empty when they typed none. Valid after
    /// exec() returns Accepted.
    [[nodiscard]] QString archivePassword() const { return m_password; }

    /// The queue-options row, read at the same moment and for the same reason:
    /// the base disables its fields on the way out.
    [[nodiscard]] int category() const { return m_category; }
    [[nodiscard]] int priority() const { return m_priority; }
    [[nodiscard]] bool paused() const { return m_paused; }

protected:
    void onAccepted() override;

private:
    QString m_password;
    int m_category = 0;
    int m_priority = 0;
    bool m_paused = false;
};

} // namespace eMule
