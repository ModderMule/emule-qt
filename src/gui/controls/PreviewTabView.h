#pragma once

/// @file PreviewTabView.h
/// @brief Base of the search Details tabs fetched on demand (Images, NFO): a
///        centered note (loading, none, error) or the content page.

#include <QString>
#include <QWidget>

class QLabel;
class QStackedWidget;

namespace eMule {

class PreviewTabView : public QWidget {
    Q_OBJECT

public:
    explicit PreviewTabView(QWidget* parent = nullptr);

    /// A centered note instead of the content.
    void showMessage(const QString& text);
    /// The note on show, empty while the content is.
    [[nodiscard]] QString message() const;

protected:
    /// Once, from the subclass constructor.
    void setContentPage(QWidget* page);
    void showContent();

private:
    QStackedWidget* m_stack = nullptr;
    QLabel* m_message = nullptr;
};

} // namespace eMule
