#pragma once

/// @file BugReportDialog.h
/// @brief "Submit Bug Report" dialog for posting reports to the eMule Qt website.

#include "dialogs/RememberedSizeDialog.h"

#include <functional>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QProgressBar;
class QPushButton;
class QTextEdit;

namespace eMule {

class LogWidget;

class BugReportDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    /// @p coreVersion: the connected core's app version, empty if not known.
    /// @p statsText: the full statistics tree as text, read at submit time
    explicit BugReportDialog(LogWidget* logWidget, const QString& coreVersion = {},
                             std::function<QString()> statsText = {}, QWidget* parent = nullptr);

private slots:
    void onSubmitClicked();
    void onAddScreenshots();
    void onRemoveScreenshot();
    void onBrowseCrashDump();
    void onRemoveCrashDump();

private:
    void prefillCrashDump();
    [[nodiscard]] QString versionLine() const;

    QString m_coreVersion;
    std::function<QString()> m_statsText;

    QComboBox* m_typeCombo = nullptr;
    QLineEdit* m_titleEdit = nullptr;
    QTextEdit* m_descriptionEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_emailEdit = nullptr;
    QListWidget* m_screenshotList = nullptr;
    QLineEdit* m_crashDumpEdit = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_submitBtn = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    LogWidget* m_logWidget = nullptr;
};

} // namespace eMule
