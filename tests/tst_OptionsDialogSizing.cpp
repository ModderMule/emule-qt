/// @file tst_OptionsDialogSizing.cpp
/// @brief The Options window's height budget, and the sidebar↔page mapping under it.
///
/// The pages live in a QStackedWidget, and QStackedLayout::minimumSize() is the **maximum
/// over all of them** — including the seventeen that are not visible. So one page that
/// forgets its scroll area sets the minimum height of the whole window, on every
/// category. That is exactly what happened: the Usenet page grew a 19-row form and six
/// group boxes, ~1270 px of layout minimum, and the dialog started opening taller than
/// the screen. Nothing failed; it just got worse one row at a time.
///
/// The other half is the sidebar. It is grouped MorphXT-style now, so its display order
/// no longer matches the Page enum — and the enum is what `UiState::optionsLastPage()`
/// persists and what `--options N` accepts. Reordering it would silently repoint every
/// stored index, which is a bug that only shows up on somebody else's machine.
///
/// Being the one test that builds the real dialog, it also checks a few values the
/// offline load path puts on the pages.

#include "app/UiState.h"
#include "controls/AccordionSidebar.h"
#include "dialogs/OptionsDialog.h"
#include "prefs/Preferences.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTest>

using namespace eMule;

namespace {

/// What a page may cost. Generous on purpose: the point is to catch a page that never
/// got a scroll area at all, not to police layout by a few pixels.
constexpr int kPageBudget = 460;

/// The window has to open on a laptop. 700 is the designed default in OptionsDialog's
/// constructor; a minimum above it means the default could not be honoured, which is
/// precisely the failure this file was written for.
constexpr int kDialogBudget = 700;

const char* pageName(int page)
{
    static const char* names[] = {
        "General", "Display", "Connection", "Proxy", "Server", "Directories", "Files",
        "Notifications", "Statistics", "IRC", "Messages", "Security", "Scheduler",
        "WebInterface", "Usenet", "Indexers", "Feeds", "Extended"};
    return (page >= 0 && page < int(std::size(names))) ? names[page] : "?";
}

QStackedWidget* stackOf(OptionsDialog& dlg)
{
    return dlg.findChild<QStackedWidget*>();
}

} // namespace

class TestOptionsDialogSizing : public QObject {
    Q_OBJECT

private slots:
    void everyPageStaysUnderBudget();
    void dialogFitsOnALaptop();
    void selectPageLandsOnTheMatchingStackIndex();
    void everyPageAppearsOnceInTheSidebar();
    void storedPageIsRestored();
    void outOfRangeStoredPageFallsBackToGeneral();
    void breadcrumbNamesGroupAndPage();
    void usenetPageHasAccountAndAdvancedTabs();

    // --- values loaded into the pages (offline: IpcClient nullptr) ---------
    void anUnlimitedLimitParksAtTheCapacity();
    void theIpcLogBoxShowsTheStoredValue();
    void aLargeQueueSizeSurvivesTheSlider();
    void extendedValuesComeFromThePrefsNotFallbacks();
    void generalPageControlsEnableApply();
};

/// The regression this file exists for. Named per page, because "the dialog is too tall"
/// is not a finding anybody can act on.
void TestOptionsDialogSizing::everyPageStaysUnderBudget()
{
    OptionsDialog dlg(nullptr, nullptr);
    QStackedWidget* stack = stackOf(dlg);
    QVERIFY(stack);
    QCOMPARE(stack->count(), int(OptionsDialog::PageCount));

    for (int page = 0; page < stack->count(); ++page) {
        const int minimum = stack->widget(page)->minimumSizeHint().height();
        QVERIFY2(minimum <= kPageBudget,
                 qPrintable(QStringLiteral("page %1 needs %2 px, budget is %3 — it is "
                                           "probably missing its ContentScrollArea")
                                .arg(QLatin1String(pageName(page)))
                                .arg(minimum)
                                .arg(kPageBudget)));
    }

    QVERIFY(stack->minimumSizeHint().height() <= kPageBudget);
}

void TestOptionsDialogSizing::dialogFitsOnALaptop()
{
    OptionsDialog dlg(nullptr, nullptr);
    QVERIFY2(dlg.minimumHeight() <= kDialogBudget,
             qPrintable(QStringLiteral("minimum height is %1").arg(dlg.minimumHeight())));
    QVERIFY2(dlg.minimumWidth() <= 800,
             qPrintable(QStringLiteral("minimum width is %1").arg(dlg.minimumWidth())));
}

/// The sidebar carries Page values as opaque item ids, which is the whole reason the
/// grouping could change without touching the enum. If that identity breaks, every
/// caller of selectPage() silently opens the wrong page.
void TestOptionsDialogSizing::selectPageLandsOnTheMatchingStackIndex()
{
    OptionsDialog dlg(nullptr, nullptr);
    QStackedWidget* stack = stackOf(dlg);
    QVERIFY(stack);

    for (int page = 0; page < int(OptionsDialog::PageCount); ++page) {
        dlg.selectPage(page);
        QCOMPARE(stack->currentIndex(), page);
    }
}

/// A page added to the enum and to setupPages() but forgotten in the sidebar tables is
/// simply unreachable. There is a static_assert for it too; this catches the same thing
/// from the other side, after the tables have actually been walked.
void TestOptionsDialogSizing::everyPageAppearsOnceInTheSidebar()
{
    OptionsDialog dlg(nullptr, nullptr);
    auto* sidebar = dlg.findChild<AccordionSidebar*>();
    QVERIFY(sidebar);
    QCOMPARE(sidebar->itemCount(), int(OptionsDialog::PageCount));

    QSet<int> seen;
    for (int page = 0; page < int(OptionsDialog::PageCount); ++page) {
        QVERIFY2(sidebar->setCurrentItemId(page), pageName(page));
        QCOMPARE(sidebar->currentItemId(), page);
        QVERIFY2(!seen.contains(page), pageName(page));
        seen.insert(page);
    }
}

/// The migration guard. uistate.yml holds a Page value written by an older build, so
/// reordering the enum would repoint it — this fails the moment somebody does.
void TestOptionsDialogSizing::storedPageIsRestored()
{
    theUiState.setOptionsLastPage(OptionsDialog::PageFeeds);
    OptionsDialog dlg(nullptr, nullptr);
    QCOMPARE(stackOf(dlg)->currentIndex(), int(OptionsDialog::PageFeeds));
}

void TestOptionsDialogSizing::outOfRangeStoredPageFallsBackToGeneral()
{
    theUiState.setOptionsLastPage(999);
    OptionsDialog dlg(nullptr, nullptr);
    QCOMPARE(stackOf(dlg)->currentIndex(), int(OptionsDialog::PageGeneral));
    theUiState.setOptionsLastPage(OptionsDialog::PageGeneral);
}

/// MorphXT's "Options -> Advanced options -> IRC" (PreferencesDlg.cpp:582).
void TestOptionsDialogSizing::breadcrumbNamesGroupAndPage()
{
    OptionsDialog dlg(nullptr, nullptr);
    dlg.selectPage(OptionsDialog::PageIRC);
    QVERIFY2(dlg.windowTitle().contains(QStringLiteral("IRC")),
             qPrintable(dlg.windowTitle()));
    QVERIFY2(dlg.windowTitle().contains(QStringLiteral("Advanced")),
             qPrintable(dlg.windowTitle()));
}

/// The tabs split the page by scope, not by difficulty: the Account tab owns the server
/// table and *every* field belonging to the selected server, the Advanced tab owns only
/// settings that apply to the whole engine. Put a per-account field on the Advanced tab
/// and it sits on a tab with no account chooser, where it reads as global — which is a
/// wrong answer to "what does this apply to", not a layout nit.
void TestOptionsDialogSizing::usenetPageHasAccountAndAdvancedTabs()
{
    OptionsDialog dlg(nullptr, nullptr);
    dlg.selectPage(OptionsDialog::PageUsenet);

    auto* tabs = stackOf(dlg)->widget(OptionsDialog::PageUsenet)->findChild<QTabWidget*>();
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 2);

    // By object name, because the titles are tr()'d and a translated build would
    // otherwise pass this test by finding nothing at all.
    const auto perAccountGroups = [](QWidget* tab) {
        int found = 0;
        for (const QWidget* w : tab->findChildren<QWidget*>())
            found += w->objectName().startsWith(QStringLiteral("usenetAccount")) ? 1 : 0;
        return found;
    };
    QCOMPARE(perAccountGroups(tabs->widget(0)), 2);
    QCOMPARE(perAccountGroups(tabs->widget(1)), 0);

    // Both tabs are built up front, so a widget that lives on the hidden one is findable
    // and enabled-able from the moment the page exists. Building the Advanced tab lazily
    // would make the first commit write defaults over half the global settings.
    tabs->setCurrentIndex(0);
    QVERIFY(tabs->widget(1)->findChildren<QWidget*>().size() > 5);

    // Each tab scrolls on its own; an outer wrapper as well would mean two scrollbars.
    QVERIFY(qobject_cast<QScrollArea*>(tabs->widget(0)));
    QVERIFY(qobject_cast<QScrollArea*>(tabs->widget(1)));
}

/// An unlimited limit left the slider at its floor of 1, so ticking the box and pressing
/// OK throttled transfers to 1 KB/s. MFC parks it at the capacity (PPgConnection.cpp:177-184).
void TestOptionsDialogSizing::anUnlimitedLimitParksAtTheCapacity()
{
    thePrefs.setMaxGraphDownloadRate(300);
    thePrefs.setMaxDownload(0);
    OptionsDialog dlg(nullptr, nullptr);

    const auto sliders =
        stackOf(dlg)->widget(OptionsDialog::PageConnection)->findChildren<QSlider*>();
    QCOMPARE(sliders.size(), 2);   // download, then upload
    QSlider* down = sliders.front();
    QVERIFY(!down->isEnabled());
    QCOMPARE(down->value(), 300);

    bool labelShowsIt = false;
    for (const QLabel* l : down->parentWidget()->findChildren<QLabel*>())
        labelShowsIt |= l->text() == QStringLiteral("300 KB/s");
    QVERIFY(labelShowsIt);
}

/// The box was read from GetPreferences, which never carried the key, so it opened
/// unticked and the next OK switched the IPC log tab off.
void TestOptionsDialogSizing::theIpcLogBoxShowsTheStoredValue()
{
    thePrefs.setEnableIpcLog(true);
    OptionsDialog dlg(nullptr, nullptr);

    QCheckBox* box = nullptr;
    for (QCheckBox* c : dlg.findChildren<QCheckBox*>())
        if (c->text() == QStringLiteral("Enable IPC log tab"))
            box = c;
    QVERIFY(box);
    QVERIFY(box->isChecked());
    thePrefs.setEnableIpcLog(false);
}

/// The slider stopped at 30 000, so a larger stored queue was cut down by the next OK.
void TestOptionsDialogSizing::aLargeQueueSizeSurvivesTheSlider()
{
    thePrefs.setQueueSize(42'000);
    OptionsDialog dlg(nullptr, nullptr);

    QSlider* queue = nullptr;
    for (QSlider* s : stackOf(dlg)->widget(OptionsDialog::PageExtended)->findChildren<QSlider*>())
        if (s->maximum() == 500)   // ×100
            queue = s;
    QVERIFY(queue);
    QCOMPARE(queue->minimum(), 20);
    QCOMPARE(queue->value(), 420);
    thePrefs.setQueueSize(5000);
}

/// The offline map stopped after the Files page, so everything later loaded hardcoded
/// fallbacks — a 240 KB file buffer, 20 connections per 5 s — and the next OK saved them.
void TestOptionsDialogSizing::extendedValuesComeFromThePrefsNotFallbacks()
{
    thePrefs.setMaxConsPerFive(37);
    thePrefs.setFileBufferSize(64 * 16384);
    OptionsDialog dlg(nullptr, nullptr);
    const QWidget* page = stackOf(dlg)->widget(OptionsDialog::PageExtended);

    bool spinShowsIt = false;
    for (const QSpinBox* s : page->findChildren<QSpinBox*>())
        spinShowsIt |= s->maximum() == 50 && s->value() == 37;   // capped at the core's clamp
    QVERIFY(spinShowsIt);

    bool sliderShowsIt = false;
    for (const QSlider* s : page->findChildren<QSlider*>())
        sliderShowsIt |= s->maximum() == 4096 && s->value() == 64;
    QVERIFY(sliderShowsIt);
}

/// Six General-page controls never called markDirty, so changing only them left Apply grey.
void TestOptionsDialogSizing::generalPageControlsEnableApply()
{
    const auto applyOf = [](OptionsDialog& dlg) {
        QPushButton* apply = nullptr;
        for (QPushButton* b : dlg.findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Apply"))
                apply = b;
        return apply;
    };

    const QStringList boxes = {QStringLiteral("Show splash screen"),
                               QStringLiteral("Enable online signature"),
                               QStringLiteral("Prevent standby mode while running"),
                               QStringLiteral("Start with")};   // "…macOS" / "…Windows" / "…system"
    for (const QString& label : boxes) {
        OptionsDialog dlg(nullptr, nullptr);
        QPushButton* apply = applyOf(dlg);
        QVERIFY(apply);
        QCheckBox* box = nullptr;
        for (QCheckBox* c : dlg.findChildren<QCheckBox*>())
            if (c->text().startsWith(label))
                box = c;
        QVERIFY2(box, qPrintable(label));
        QVERIFY(!apply->isEnabled());
        box->toggle();
        QVERIFY2(apply->isEnabled(), qPrintable(label));
    }

    OptionsDialog dlg(nullptr, nullptr);
    QPushButton* apply = applyOf(dlg);
    QComboBox* lang = nullptr;
    for (QComboBox* c : dlg.findChildren<QComboBox*>())
        if (c->count() > 1 && c->itemText(0) == QStringLiteral("System Default"))
            lang = c;
    QVERIFY(lang);
    QVERIFY(!apply->isEnabled());
    lang->setCurrentIndex(lang->currentIndex() == 0 ? 1 : 0);
    QVERIFY(apply->isEnabled());
}

QTEST_MAIN(TestOptionsDialogSizing)
#include "tst_OptionsDialogSizing.moc"
