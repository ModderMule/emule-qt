#pragma once

/// @file RenameFileDialog.h
/// @brief One-line prompt for a new file name, wide enough to show one.
///
/// QInputDialog::getText() is as narrow as its label. Shared by the transfer and
/// the shared files list.

#include <QCoreApplication>
#include <QInputDialog>
#include <QString>

#include <optional>

namespace eMule {

class RenameFileDialog : public QInputDialog {
public:
    RenameFileDialog(QWidget* parent, const QString& currentName)
        : QInputDialog(parent), m_currentName(currentName)
    {
        // MFC InputBox labels: IDS_RENAME, IDS_DL_FILENAME
        setWindowTitle(QCoreApplication::translate("RenameFileDialog", "Rename"));
        setLabelText(QCoreApplication::translate("RenameFileDialog", "File name:"));
        setInputMode(QInputDialog::TextInput);
        setTextValue(currentName);
        resize(kWidth, sizeHint().height());
    }

    /// The trimmed name the user entered; empty when it is blank or unchanged.
    [[nodiscard]] QString newName() const
    {
        const QString name = textValue().trimmed();
        return name == m_currentName ? QString() : name;
    }

    /// Runs the prompt. No value: cancelled, blank, or the name it already has.
    [[nodiscard]] static std::optional<QString> ask(QWidget* parent, const QString& currentName)
    {
        RenameFileDialog dlg(parent, currentName);
        if (dlg.exec() != QDialog::Accepted || dlg.newName().isEmpty())
            return std::nullopt;
        return dlg.newName();
    }

private:
    static constexpr int kWidth = 640;
    QString m_currentName;
};

} // namespace eMule
