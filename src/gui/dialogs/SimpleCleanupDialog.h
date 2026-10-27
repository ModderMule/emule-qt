#pragma once

/// @file SimpleCleanupDialog.h
/// @brief Mass Rename "Simple cleanup" options (MorphXT CSimpleCleanupDialog, IDD_SIMPLECLEANUP).

#include "utils/MassRename.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace eMule {

class IpcClient;

class SimpleCleanupDialog : public QDialog {
    Q_OBJECT

public:
    explicit SimpleCleanupDialog(const massrename::SimpleCleanupOptions& opt, QWidget* parent = nullptr);

    [[nodiscard]] massrename::SimpleCleanupOptions options() const;

    /// The options kept in prefs.
    [[nodiscard]] static massrename::SimpleCleanupOptions storedOptions();
    /// Keeps @p opt in the local prefs and in the daemon's (it owns preferences.yml).
    static void storeOptions(const massrename::SimpleCleanupOptions& opt, IpcClient* ipc);

private:
    void addPair();
    void editPair();
    void deletePair();
    void onPairSelected();
    void updateEnabled();

    QCheckBox* m_dotCheck = nullptr;
    QCheckBox* m_underlineCheck = nullptr;
    QCheckBox* m_apostropheCheck = nullptr;
    QCheckBox* m_hexCheck = nullptr;
    QCheckBox* m_searchCheck = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLineEdit* m_replaceEdit = nullptr;
    QCheckBox* m_caseCheck = nullptr;
    QCheckBox* m_charsCheck = nullptr;
    QLineEdit* m_charSource = nullptr;
    QLineEdit* m_charDest = nullptr;
    QTreeWidget* m_charList = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_editBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
};

} // namespace eMule
