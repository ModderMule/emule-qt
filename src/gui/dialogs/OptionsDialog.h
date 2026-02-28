#pragma once

/// @file OptionsDialog.h
/// @brief Options/Preferences dialog matching the MFC eMule Options window.
///
/// Left sidebar with category icons, right stacked widget for page content,
/// and OK/Cancel/Apply buttons at the bottom.

#include <QDialog>
#include <QIcon>

class QLabel;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QListWidget;
class QStackedWidget;
class QPushButton;

namespace eMule {

class IpcClient;

class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit OptionsDialog(IpcClient* ipc, QWidget* parent = nullptr);
    ~OptionsDialog() override;

    /// Switch to a specific page by index.
    void selectPage(int page);

    /// Page indices matching sidebar order.
    enum Page {
        PageGeneral = 0,
        PageDisplay,
        PageConnection,
        PageProxy,
        PageServer,
        PageDirectories,
        PageFiles,
        PageNotifications,
        PageStatistics,
        PageIRC,
        PageMessages,
        PageSecurity,
        PageScheduler,
        PageWebInterface,
        PageExtended,
        PageCount
    };

private slots:
    void onPageChanged(int row);
    void onOk();
    void onApply();

private:
    void setupSidebar();
    void setupPages();
    void setupButtons();

    QWidget* createGeneralPage();
    QWidget* createPlaceholderPage(const QString& title);

    void loadSettings();
    void saveSettings();
    static QIcon makePadlockIcon();

    IpcClient* m_ipc = nullptr;
    QListWidget* m_sidebar = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_pageHeader = nullptr;
    QPushButton* m_applyBtn = nullptr;

    // General page controls
    QLineEdit* m_nickEdit = nullptr;
    QComboBox* m_langCombo = nullptr;
    QCheckBox* m_promptOnExitCheck = nullptr;
    QCheckBox* m_startMinimizedCheck = nullptr;
};

} // namespace eMule
