#pragma once

/// @file HosterPuzzleDialog.h
/// @brief An image puzzle (captcha) of a hoster the resolver asks the user to solve
/// (hoster.md §3). Not modal: downloads go on meanwhile. Closes itself when the resolver's
/// time runs out; closing it unanswered skips the puzzle. "Skip hoster" skips this and every
/// later puzzle of the hoster until the user retries a download there.

#include <QDialog>
#include <QTimer>

class QLabel;
class QLineEdit;

namespace eMule {

class HosterPuzzleDialog : public QDialog {
    Q_OBJECT

public:
    /// @p image: PNG, JPEG or GIF bytes; @p explain: the hoster's hint, may be empty.
    HosterPuzzleDialog(const QByteArray& image, const QString& host, const QString& fileName, const QString& explain,
                       int timeoutS, QWidget* parent = nullptr);

    [[nodiscard]] QString host() const { return m_host; }

signals:
    /// The user's text; empty: skipped, @p skipHoster: the hoster's later puzzles too. Emitted once.
    void answered(const QString& text, bool skipHoster);

protected:
    void done(int result) override;

private:
    void tick();

    QLabel* m_countdown = nullptr;
    QLineEdit* m_edit = nullptr;
    QTimer m_timer;
    QString m_host;
    int m_left = 0;
    bool m_sent = false;
    bool m_skipHoster = false;
};

} // namespace eMule
