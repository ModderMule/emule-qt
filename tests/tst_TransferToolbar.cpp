/// @file tst_TransferToolbar.cpp
/// @brief Transfer header click cycles lists like MFC's IDC_DOWNLOAD_ICO button (#171).

#include "controls/TransferToolbar.h"

#include <QAbstractButton>
#include <QApplication>
#include <QIcon>
#include <QLabel>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace Qt::StringLiterals;

class tst_TransferToolbar : public QObject {
    Q_OBJECT

private slots:
    void labelClickCyclesWithButtons();
    void labelClickCyclesWithoutButtons();
    void emptyAreaCyclesOnlyWithoutButtons();
    void buttonClickDoesNotCycle();

private:
    static QLabel* boldLabel(TransferToolbar& bar);
};

void tst_TransferToolbar::labelClickCyclesWithButtons()
{
    TransferToolbar bar;
    bar.addButton(QIcon(), u"A"_s);
    bar.addButton(QIcon(), u"B"_s);
    bar.setLabelText(u"Downloads (3)"_s);
    bar.resize(600, 22);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));

    QSignalSpy cycle(&bar, &TransferToolbar::cycleRequested);
    QTest::mouseClick(boldLabel(bar), Qt::LeftButton);
    QCOMPARE(cycle.count(), 1);
}

void tst_TransferToolbar::labelClickCyclesWithoutButtons()
{
    TransferToolbar bar;
    bar.addButton(QIcon(), u"A"_s);
    bar.setButtonsVisible(false);
    bar.setLabelText(u"Uploading (1)"_s);
    bar.resize(600, 22);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));

    QSignalSpy cycle(&bar, &TransferToolbar::cycleRequested);
    QTest::mouseClick(boldLabel(bar), Qt::LeftButton);
    QCOMPARE(cycle.count(), 1);
    QTest::mouseClick(boldLabel(bar), Qt::RightButton);
    QCOMPARE(cycle.count(), 1);
}

void tst_TransferToolbar::emptyAreaCyclesOnlyWithoutButtons()
{
    TransferToolbar bar;
    bar.addButton(QIcon(), u"A"_s);
    bar.setLabelText(u"Downloads"_s);
    bar.resize(600, 22);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));

    QSignalSpy cycle(&bar, &TransferToolbar::cycleRequested);
    const QPoint emptyArea(bar.width() - 5, bar.height() / 2);
    QTest::mouseClick(&bar, Qt::LeftButton, {}, emptyArea);
    QCOMPARE(cycle.count(), 0);

    bar.setButtonsVisible(false);
    QTest::mouseClick(&bar, Qt::LeftButton, {}, emptyArea);
    QCOMPARE(cycle.count(), 1);
}

void tst_TransferToolbar::buttonClickDoesNotCycle()
{
    TransferToolbar bar;
    bar.addButton(QIcon(), u"A"_s);
    const int b = bar.addButton(QIcon(), u"B"_s);
    bar.resize(600, 22);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));

    QSignalSpy cycle(&bar, &TransferToolbar::cycleRequested);
    QSignalSpy clicked(&bar, &TransferToolbar::buttonClicked);
    const auto buttons = bar.findChildren<QAbstractButton*>();
    QCOMPARE(buttons.size(), 2);
    QTest::mouseClick(buttons.at(b), Qt::LeftButton);
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(cycle.count(), 0);
}

QLabel* tst_TransferToolbar::boldLabel(TransferToolbar& bar)
{
    for (auto* label : bar.findChildren<QLabel*>()) {
        if (label->font().bold())
            return label;
    }
    return nullptr;
}

QTEST_MAIN(tst_TransferToolbar)
#include "tst_TransferToolbar.moc"
