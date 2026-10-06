/// @file tst_LogTextView.cpp
/// @brief Scrolling rules of the log panes (port of MFC CHTRichEditCtrl).
///
/// The regression these pin down: the Server Info pane stopped following new
/// connection messages. A plain QTextBrowser only follows while its scrollbar is
/// exactly at the maximum, and loses that for good once text arrives while it is
/// hidden, it is resized, or old lines are trimmed.

#include "controls/LogTextView.h"

#include <QScrollBar>
#include <QImage>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

using eMule::LogTextView;

namespace {

void fill(QTextBrowser& view, int lines, int from = 0)
{
    for (int i = 0; i < lines; ++i)
        view.append(QStringLiteral("<span>line %1</span>").arg(from + i));
}

void showAndFill(LogTextView& view, int lines = 200)
{
    view.resize(400, 200);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    fill(view, lines);
    QCoreApplication::processEvents();
    QVERIFY(view.verticalScrollBar()->maximum() > 4 * view.height());
}

bool atBottom(const QTextBrowser& view)
{
    const QScrollBar* bar = view.verticalScrollBar();
    return bar->value() == bar->maximum();
}

} // namespace

class tst_LogTextView : public QObject {
    Q_OBJECT

private slots:
    void followsWhenAtBottom();
    void keepsPositionWhenScrolledUp();
    void followsWithinSlack();
    void followsAgainAfterScrollingBack();
    void filledWhileHiddenOpensAtBottom();
    void trimWhileFollowingStaysAtBottom();
    void cursorInsertIsFollowed();
    void resizeKeepsBottom();
    void imageResourceSurvivesARebuild();
    void resizeKeepsScrolledUpPosition();
    void scrollToBottomRelatches();
    void restoreScrollValueSurvivesRebuild();
    void autoScrollFlag();
};

void tst_LogTextView::followsWhenAtBottom()
{
    LogTextView view;
    showAndFill(view);
    QVERIFY(atBottom(view));
    const int before = view.verticalScrollBar()->maximum();
    fill(view, 5, 1000);
    QCoreApplication::processEvents();
    QVERIFY(view.verticalScrollBar()->maximum() > before);
    QVERIFY(atBottom(view));
}

void tst_LogTextView::keepsPositionWhenScrolledUp()
{
    LogTextView view;
    showAndFill(view);
    view.verticalScrollBar()->setValue(100);
    QVERIFY(!view.isFollowing());
    fill(view, 20, 1000);
    QCoreApplication::processEvents();
    QCOMPARE(view.verticalScrollBar()->value(), 100);
}

void tst_LogTextView::followsWithinSlack()
{
    LogTextView view;
    showAndFill(view);
    QScrollBar* bar = view.verticalScrollBar();
    bar->setValue(bar->maximum() - LogTextView::kFollowSlackPx);
    QVERIFY(view.isFollowing());
    fill(view, 5, 1000);
    QCoreApplication::processEvents();
    QVERIFY(atBottom(view));

    bar->setValue(bar->maximum() - LogTextView::kFollowSlackPx - 1);
    QVERIFY(!view.isFollowing());
}

void tst_LogTextView::followsAgainAfterScrollingBack()
{
    LogTextView view;
    showAndFill(view);
    QScrollBar* bar = view.verticalScrollBar();
    bar->setValue(0);
    fill(view, 5, 1000);
    QCOMPARE(bar->value(), 0);
    bar->setValue(bar->maximum());
    fill(view, 5, 2000);
    QCoreApplication::processEvents();
    QVERIFY(atBottom(view));
}

void tst_LogTextView::filledWhileHiddenOpensAtBottom()
{
    // The startup banner and the server-message backlog land before the pane
    // was ever laid out at its real size.
    LogTextView view;
    fill(view, 200);
    view.resize(400, 200);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QCoreApplication::processEvents();
    QVERIFY(view.verticalScrollBar()->maximum() > 0);
    QVERIFY(atBottom(view));
    fill(view, 5, 1000);
    QCoreApplication::processEvents();
    QVERIFY(atBottom(view));
}

void tst_LogTextView::trimWhileFollowingStaysAtBottom()
{
    // LogWidget::trimToLimit: drop the oldest blocks after an append.
    LogTextView view;
    showAndFill(view);
    QTextCursor cursor(view.document());
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, 50);
    cursor.removeSelectedText();
    QCoreApplication::processEvents();
    QVERIFY(atBottom(view));
    fill(view, 5, 1000);
    QCoreApplication::processEvents();
    QVERIFY(atBottom(view));
}

void tst_LogTextView::cursorInsertIsFollowed()
{
    // LogWidget::insertSorted's slow path never goes through append().
    LogTextView view;
    showAndFill(view);
    const int before = view.verticalScrollBar()->maximum();
    QTextCursor cursor(view.document());
    cursor.movePosition(QTextCursor::End);
    for (int i = 0; i < 10; ++i) {
        cursor.insertBlock();
        cursor.insertHtml(QStringLiteral("<span>late %1</span>").arg(i));
    }
    QCoreApplication::processEvents();
    QVERIFY(view.verticalScrollBar()->maximum() > before);
    QVERIFY(atBottom(view));
}

void tst_LogTextView::resizeKeepsBottom()
{
    LogTextView view;
    showAndFill(view);
    view.resize(250, 120);   // narrower: lines wrap, the document grows
    QCoreApplication::processEvents();
    QVERIFY(atBottom(view));
    view.resize(500, 300);
    QCoreApplication::processEvents();
    QVERIFY(atBottom(view));
}

void tst_LogTextView::resizeKeepsScrolledUpPosition()
{
    LogTextView view;
    showAndFill(view);
    view.verticalScrollBar()->setValue(100);
    view.resize(400, 150);
    QCoreApplication::processEvents();
    QVERIFY(!view.isFollowing());
    QCOMPARE(view.verticalScrollBar()->value(), 100);
}

void tst_LogTextView::scrollToBottomRelatches()
{
    LogTextView view;
    showAndFill(view);
    view.verticalScrollBar()->setValue(0);
    view.scrollToBottom();
    QVERIFY(view.isFollowing());
    QVERIFY(atBottom(view));
    fill(view, 5, 1000);
    QCoreApplication::processEvents();
    QVERIFY(atBottom(view));
}

void tst_LogTextView::restoreScrollValueSurvivesRebuild()
{
    // MessagesPanel::updateChatDisplay: clear() + re-append on every message.
    LogTextView view;
    showAndFill(view);
    view.verticalScrollBar()->setValue(150);
    const int value = view.verticalScrollBar()->value();
    view.clear();
    fill(view, 201);
    view.restoreScrollValue(value);
    QCoreApplication::processEvents();
    QCOMPARE(view.verticalScrollBar()->value(), 150);
    QVERIFY(!view.isFollowing());
}

void tst_LogTextView::autoScrollFlag()
{
    LogTextView view;
    QVERIFY(view.autoScroll());   // MFC default (HTRichEditCtrl.cpp:60)
    view.setAutoScroll(false);
    QVERIFY(!view.autoScroll());
    // The flag does not gate following — only the view position does.
    showAndFill(view);
    fill(view, 5, 1000);
    QCoreApplication::processEvents();
    QVERIFY(atBottom(view));
}

// The chat pane is cleared and rebuilt on every message; a captcha picture is put back
// the way MessagesPanel::updateChatDisplay does it — resource first, then the <img>.
void tst_LogTextView::imageResourceSurvivesARebuild()
{
    LogTextView view;
    QImage captcha(104, 48, QImage::Format_Mono);
    captcha.fill(1);
    const QUrl url(QStringLiteral("captcha://0"));

    for (int rebuild = 0; rebuild < 2; ++rebuild) {
        view.clear();
        view.append(QStringLiteral("<font color='gray'>[12:00:00] *** solve this</font>"));
        view.document()->addResource(QTextDocument::ImageResource, url, captcha);
        view.append(QStringLiteral("<img src='%1'>").arg(url.toString()));

        int images = 0;
        for (QTextBlock block = view.document()->begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat format = it.fragment().charFormat();
                if (format.isImageFormat()
                    && format.toImageFormat().name() == url.toString())
                    ++images;
            }
        }
        QCOMPARE(images, 1);
        const QImage shown = view.document()->resource(QTextDocument::ImageResource, url)
                                 .value<QImage>();
        QCOMPARE(shown.size(), QSize(104, 48));
    }
}

QTEST_MAIN(tst_LogTextView)
#include "tst_LogTextView.moc"
