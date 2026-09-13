/// @file tst_CommentsPanel.cpp
/// @brief The Comments page and the editable one on top of it.
///
/// Two things are worth pinning here. The rating column says what MFC's
/// GetRateString says — the labels were shifted by one for a long time, so a file
/// somebody had flagged as fake read "Poor". And the editor must never write
/// behind the user: it posts only when Apply is pressed, and a details refresh
/// arriving mid-sentence must not overwrite what is being typed.

#include "dialogs/CommentEditPanel.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"
#include "utils/RatingIcons.h"

#include <QCborArray>
#include <QCborMap>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>

using namespace eMule;

class tst_CommentsPanel : public QObject {
    Q_OBJECT

private slots:
    void ratingLabelsMatchTheOriginal();
    void theListShowsOneRowPerComment();
    void theRatingColumnSortsByTheOrdinalNotByTheLabel();
    void theEditorAcceptsNoMoreThanTheWireCarries();
    void applyIsOfferedOnlyAfterAnEdit();
    void resetClearsBothFields();
    void anUncommentableFileStillReads();
    void arefreshDoesNotOverwriteTyping();

private:
    /// A details map of the shape GetSharedFileDetails answers with.
    [[nodiscard]] static QCborMap detailsFor(const QString& myComment, int myRating,
                                             bool canComment = true);

    /// The button captioned @p text. By caption rather than by position, so adding a
    /// button next to it cannot quietly point a test at the wrong one.
    [[nodiscard]] static QPushButton* buttonNamed(QWidget& panel, const QString& text);
};

QPushButton* tst_CommentsPanel::buttonNamed(QWidget& panel, const QString& text)
{
    for (auto* button : panel.findChildren<QPushButton*>())
        if (button->text() == text)
            return button;
    return nullptr;
}

QCborMap tst_CommentsPanel::detailsFor(const QString& myComment, int myRating,
                                       bool canComment)
{
    return QCborMap{
        {QLatin1StringView("hash"), QStringLiteral("0123456789ABCDEF0123456789ABCDEF")},
        {QLatin1StringView("fileName"), QStringLiteral("movie.avi")},
        {QLatin1StringView("canComment"), canComment},
        {QLatin1StringView("myComment"), myComment},
        {QLatin1StringView("myRating"), myRating},
        {QLatin1StringView("comments"), QCborArray{}},
    };
}

void tst_CommentsPanel::ratingLabelsMatchTheOriginal()
{
    // srchybrid/OtherFunctions.cpp:786-794. Note 1: eMule's rating 1 is the fake
    // flag, not "slightly poor" — mislabelling it is how a warning becomes a shrug.
    QCOMPARE(ratingLabel(0), QStringLiteral("Not rated"));
    QCOMPARE(ratingLabel(1), QStringLiteral("Invalid / Corrupt / Fake"));
    QCOMPARE(ratingLabel(2), QStringLiteral("Poor"));
    QCOMPARE(ratingLabel(3), QStringLiteral("Fair"));
    QCOMPARE(ratingLabel(4), QStringLiteral("Good"));
    QCOMPARE(ratingLabel(5), QStringLiteral("Excellent"));

    // Out of range falls back to "Not rated", as GetRateString's `rate > 5 ? 0` does.
    QCOMPARE(ratingLabel(6), QStringLiteral("Not rated"));
    QCOMPARE(ratingLabel(-1), QStringLiteral("Not rated"));
}

void tst_CommentsPanel::theListShowsOneRowPerComment()
{
    CommentsPanel panel(QStringLiteral("tstComments"));
    QCborMap details = detailsFor(QString{}, 0);
    details.insert(QLatin1StringView("comments"), QCborArray{
        QCborMap{{QLatin1StringView("userName"), QStringLiteral("someone")},
                 {QLatin1StringView("rating"), 1},
                 {QLatin1StringView("comment"), QStringLiteral("fake, do not bother")}},
        QCborMap{{QLatin1StringView("userName"), QStringLiteral("Kad")},
                 {QLatin1StringView("rating"), 0},
                 {QLatin1StringView("comment"), QStringLiteral("no rating, just words")}},
    });
    panel.setDetails(details);

    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 2);

    // Sorting is on, so find the rows rather than assuming an order.
    QStringList ratings;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        ratings << tree->topLevelItem(i)->text(CommentsPanel::ColRating);
    QVERIFY(ratings.contains(QStringLiteral("Invalid / Corrupt / Fake")));
    // A comment with no rating still fills the cell — CCommentListCtrl::AddComment
    // always passes GetRateString, so there is no blank case.
    QVERIFY(ratings.contains(QStringLiteral("Not rated")));
}

void tst_CommentsPanel::theRatingColumnSortsByTheOrdinalNotByTheLabel()
{
    // MFC's labels are not monotonic: alphabetically they run Excellent, Fair,
    // Good, Invalid / Corrupt / Fake, Not rated, Poor, which puts the best rating
    // at the top of an *ascending* sort and a fake between Good and Not rated.
    // Inserted in a third order again, so neither answer can come from the sort
    // simply leaving the rows where it found them.
    CommentsPanel panel(QStringLiteral("tstCommentsSort"));
    QCborMap details = detailsFor(QString{}, 0);
    details.insert(QLatin1StringView("comments"), QCborArray{
        QCborMap{{QLatin1StringView("userName"), QStringLiteral("c")},
                 {QLatin1StringView("rating"), 3},
                 {QLatin1StringView("comment"), QStringLiteral("fair")}},
        QCborMap{{QLatin1StringView("userName"), QStringLiteral("a")},
                 {QLatin1StringView("rating"), 5},
                 {QLatin1StringView("comment"), QStringLiteral("excellent")}},
        QCborMap{{QLatin1StringView("userName"), QStringLiteral("b")},
                 {QLatin1StringView("rating"), 0},
                 {QLatin1StringView("comment"), QStringLiteral("none")}},
    });
    panel.setDetails(details);

    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);
    tree->sortByColumn(CommentsPanel::ColRating, Qt::AscendingOrder);

    QStringList order;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        order << tree->topLevelItem(i)->text(CommentsPanel::ColComment);
    QCOMPARE(order, (QStringList{QStringLiteral("none"), QStringLiteral("fair"),
                                 QStringLiteral("excellent")}));
}

void tst_CommentsPanel::theEditorAcceptsNoMoreThanTheWireCarries()
{
    CommentEditPanel panel(QStringLiteral("tstCommentsEdit"));
    auto* edit = panel.findChild<QLineEdit*>();
    QVERIFY(edit);
    QCOMPARE(edit->maxLength(), int{MAXFILECOMMENTLEN});

    auto* combo = panel.findChild<QComboBox*>();
    QVERIFY(combo);
    QCOMPARE(combo->count(), 6);            // 0..5, no pseudo-values
    QCOMPARE(combo->itemText(1), QStringLiteral("Invalid / Corrupt / Fake"));
}

void tst_CommentsPanel::applyIsOfferedOnlyAfterAnEdit()
{
    CommentEditPanel panel(QStringLiteral("tstCommentsEdit"));
    panel.setDetails(detailsFor(QStringLiteral("existing"), 3));

    auto* applyBtn = buttonNamed(panel, QStringLiteral("Apply"));
    QVERIFY(applyBtn);

    auto* edit = panel.findChild<QLineEdit*>();
    auto* combo = panel.findChild<QComboBox*>();
    QCOMPARE(edit->text(), QStringLiteral("existing"));
    QCOMPARE(combo->currentIndex(), 3);

    // Nothing typed yet, so there is nothing to post. MFC likewise applies only
    // what the user actually changed.
    QVERIFY(!applyBtn->isEnabled());

    QSignalSpy spy(&panel, &CommentEditPanel::postComment);
    combo->setCurrentIndex(5);
    edit->setText(QStringLiteral("re-encoded, looks fine"));
    emit edit->textEdited(edit->text());

    QVERIFY(applyBtn->isEnabled());
    applyBtn->click();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("re-encoded, looks fine"));
    QCOMPARE(spy.at(0).at(2).toInt(), 5);

    // Until the daemon confirms, the change is still pending.
    QVERIFY(applyBtn->isEnabled());
    panel.commentApplied(true);
    QVERIFY(!applyBtn->isEnabled());
}

void tst_CommentsPanel::resetClearsBothFields()
{
    CommentEditPanel panel(QStringLiteral("tstCommentsEdit"));
    panel.setDetails(detailsFor(QStringLiteral("existing"), 4));

    auto* reset = buttonNamed(panel, QStringLiteral("Reset"));
    QVERIFY(reset);
    reset->click();

    QVERIFY(panel.findChild<QLineEdit*>()->text().isEmpty());
    QCOMPARE(panel.findChild<QComboBox*>()->currentIndex(), 0);

    // Reset does not post. Clearing a comment other people can already see still
    // takes a deliberate Apply.
    QSignalSpy spy(&panel, &CommentEditPanel::postComment);
    QCOMPARE(spy.count(), 0);
}

void tst_CommentsPanel::anUncommentableFileStillReads()
{
    // eMule will not publish a comment for a file it is not sharing, so the editor
    // greys out (CommentDialog.cpp:117-124). The list and the Kad lookup stay live,
    // though — that is the whole of the read-only page, and it is still useful.
    CommentEditPanel panel(QStringLiteral("tstCommentsEdit"));
    panel.setDetails(detailsFor(QString{}, 0, /*canComment*/ false));

    QVERIFY(!panel.findChild<QLineEdit*>()->isEnabled());
    QVERIFY(!panel.findChild<QComboBox*>()->isEnabled());
    QVERIFY(!buttonNamed(panel, QStringLiteral("Reset"))->isEnabled());
    QVERIFY(!buttonNamed(panel, QStringLiteral("Apply"))->isEnabled());

    QVERIFY(panel.findChild<QTreeWidget*>()->isEnabled());
    QVERIFY(buttonNamed(panel, QStringLiteral("Search Kad"))->isEnabled());
}

void tst_CommentsPanel::arefreshDoesNotOverwriteTyping()
{
    // A Kad notes lookup answers late and re-pushes the whole details map. Losing a
    // half-written sentence to that would be maddening, so a dirty editor keeps its
    // fields — MFC reloads only when its data-changed flag is set.
    CommentEditPanel panel(QStringLiteral("tstCommentsEdit"));
    panel.setDetails(detailsFor(QStringLiteral("old"), 2));

    auto* edit = panel.findChild<QLineEdit*>();
    edit->setText(QStringLiteral("half a thou"));
    emit edit->textEdited(edit->text());

    panel.setDetails(detailsFor(QStringLiteral("old"), 2));
    QCOMPARE(edit->text(), QStringLiteral("half a thou"));
}

QTEST_MAIN(tst_CommentsPanel)
#include "tst_CommentsPanel.moc"
