#pragma once

/// @file RememberedSizeDialog.h
/// @brief Common parent of the resizable dialogs: reopens at the size the user
///        left it at (issue #107).
///
/// The size is stored in uistate.yml per class name, so the File and Search detail
/// dialogs each keep their own. It is restored on the first show only, after the
/// constructor's DialogSizing::applySize() set the minimum: the stored size is grown
/// to that minimum and cut to the screen, so a bigger font or a smaller monitor still
/// gets a window that fits. A fixed-size dialog is left alone.

#include <QDialog>
#include <QString>

namespace eMule {

class RememberedSizeDialog : public QDialog {
    Q_OBJECT

public:
    explicit RememberedSizeDialog(QWidget* parent = nullptr, Qt::WindowFlags flags = {});

    void setVisible(bool visible) override;

protected:
    /// Key in uistate.yml; the class name without namespace by default. Override
    /// when one class serves dialogs that should each keep their own size.
    [[nodiscard]] virtual QString sizeKey() const;

private:
    void restoreSize();
    void storeSize() const;

    bool m_restored = false;
};

} // namespace eMule
