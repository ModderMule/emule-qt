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
#include "dialogs/FirstStartWizard.h"
#include "dialogs/OptionsDialog.h"
#include "dialogs/PortChangeNotice.h"
#include "dialogs/PortMapStatusText.h"
#include "prefs/Preferences.h"
#include "utils/DialogSizing.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCborMap>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QTest>
#include <QTimer>
#include <QDoubleSpinBox>
#include <QTreeWidget>

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
    void defaultSizeFitsTheScreen();
    void storedSizeIsRestored();
    void oversizedStoredSizeIsClampedToTheScreen();
    void undersizedStoredSizeGrowsToTheMinimum();
    void sizeIsRememberedOnClose();
    void breadcrumbNamesGroupAndPage();
    void usenetPageHasAccountAndAdvancedTabs();

    // --- values loaded into the pages (offline: IpcClient nullptr) ---------
    void anUnlimitedLimitParksAtTheCapacity();
    void theIpcLogBoxShowsTheStoredValue();
    void aLargeQueueSizeSurvivesTheSlider();
    void aLargeConnectionLimitSurvivesTheSpin();
    void extendedValuesComeFromThePrefsNotFallbacks();
    void generalPageControlsEnableApply();
    void ussShowsTheLowestAllowedUploadSpeed();
    void schedulerActionsFollowMfc();

    // First start wizard: compiled into this binary anyway, and just as daemon-less.
    void theWizardShowsAllOfItsText();
    void theWizardOffersTheNewDefaultsToAnUntunedInstall();
    void theWizardLeavesTunedLimitsAndADisabledUdpPortAlone();
    void aWizardLineTypeBecomesCapacityAndLimits();
    void theWizardRateFieldsFollowTheLineAndSwitchToCustom();
    void theWizardLimitFieldsFollowTheLineAndTakeAnEdit();
    void theWizardNeedsANetwork();
    void theWizardHasTwoPages();
    void theWizardBringsKadBackWithUdp();
    void theWizardReportsTheRealPortMappingResult();
    void theWizardDoesNotWaitWithoutACore();
    void aPortChangeIsReportedAsItWasApplied();
    void onlyAPendingPortChangeOffersARestart();
    void defaultWidthShowsEveryTabTitle();
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

/// Issue #10: the designed 800x700 was applied whatever the screen, so on a short one the
/// button row opened under the taskbar. The default has to fit like the minimum does.
void TestOptionsDialogSizing::defaultSizeFitsTheScreen()
{
    theUiState.setOptionsDialogSize({});
    OptionsDialog dlg(nullptr, nullptr);

    const QSize available = dlg.screen()->availableGeometry().size();
    QVERIFY2(dlg.width() < available.width() && dlg.height() < available.height(),
             qPrintable(QStringLiteral("opens at %1x%2 on a %3x%4 screen")
                            .arg(dlg.width()).arg(dlg.height())
                            .arg(available.width()).arg(available.height())));
    QVERIFY(dlg.width() >= dlg.minimumWidth());
    QVERIFY(dlg.height() >= dlg.minimumHeight());
}

void TestOptionsDialogSizing::storedSizeIsRestored()
{
    OptionsDialog probe(nullptr, nullptr);
    const QSize wanted = probe.minimumSize() + QSize(8, 12);
    QVERIFY2(wanted.width() < probe.screen()->availableGeometry().width() - 40
                 && wanted.height() < probe.screen()->availableGeometry().height() - 60,
             "test screen too small to tell a restored size from a clamped one");

    theUiState.setOptionsDialogSize(wanted);
    OptionsDialog dlg(nullptr, nullptr);
    QCOMPARE(dlg.size(), wanted);
    theUiState.setOptionsDialogSize({});
}

/// A size remembered on a big monitor must not follow the user to a small one.
void TestOptionsDialogSizing::oversizedStoredSizeIsClampedToTheScreen()
{
    theUiState.setOptionsDialogSize(QSize(5000, 5000));
    OptionsDialog dlg(nullptr, nullptr);

    const QSize available = dlg.screen()->availableGeometry().size();
    QVERIFY2(dlg.width() < available.width() && dlg.height() < available.height(),
             qPrintable(QStringLiteral("opens at %1x%2").arg(dlg.width()).arg(dlg.height())));
    theUiState.setOptionsDialogSize({});
}

void TestOptionsDialogSizing::undersizedStoredSizeGrowsToTheMinimum()
{
    theUiState.setOptionsDialogSize(QSize(100, 100));
    OptionsDialog dlg(nullptr, nullptr);
    QCOMPARE(dlg.size(), dlg.minimumSize());
    theUiState.setOptionsDialogSize({});
}

void TestOptionsDialogSizing::sizeIsRememberedOnClose()
{
    theUiState.setOptionsDialogSize({});
    QVERIFY(!theUiState.optionsDialogSize().isValid());

    OptionsDialog dlg(nullptr, nullptr);
    const QSize wanted = dlg.minimumSize() + QSize(8, 12);
    dlg.resize(wanted);
    dlg.reject();   // Cancel: the size is the user's even if the edits are not

    QCOMPARE(theUiState.optionsDialogSize(), wanted);
    theUiState.setOptionsDialogSize({});
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
/// MFC IDS_DYNUP_MINUPLOAD: the core has honoured the value all along, the page had
/// no control for it.
void TestOptionsDialogSizing::ussShowsTheLowestAllowedUploadSpeed()
{
    const uint32 saved = thePrefs.minUpload();
    thePrefs.setMinUpload(17);
    OptionsDialog dlg(nullptr, nullptr);
    auto* spin = dlg.findChild<QSpinBox*>(QStringLiteral("dynUpMinUpload"));
    QVERIFY(spin);
    QCOMPARE(spin->value(), 17);
    QCOMPARE(spin->minimum(), 1);
    thePrefs.setMinUpload(saved);
}

/// MFC PPgScheduler.cpp:340-415: sixteen actions at most, and a category action is
/// given its category from a submenu instead of a typed number.
void TestOptionsDialogSizing::schedulerActionsFollowMfc()
{
    OptionsDialog dlg(nullptr, nullptr);
    auto* table = dlg.findChild<QTreeWidget*>(QStringLiteral("schedActions"));
    QVERIFY(table);

    QStringList entries;
    bool addEnabled = false;
    QAction* allCategories = nullptr;
    dlg.m_schedMenuHook = [&](QMenu* menu) {
        entries.clear();
        allCategories = nullptr;
        for (QAction* a : menu->actions()) {
            entries << a->text();
            if (a->text() == QStringLiteral("Add"))
                addEnabled = a->menu()->isEnabled();
            if (a->text() == QStringLiteral("Select category")) {
                for (QAction* c : a->menu()->actions())
                    if (c->text() == QStringLiteral("All"))
                        allCategories = c;
            }
        }
        if (allCategories)
            allCategories->trigger();
    };
    const auto openMenu = [&] { emit table->customContextMenuRequested(QPoint(1, 1)); };

    // A rate action: edited through the prompt.
    auto* limit = new QTreeWidgetItem(table, {QStringLiteral("Upload Limit"), QStringLiteral("10")});
    limit->setData(0, Qt::UserRole, 1);
    table->setCurrentItem(limit);
    openMenu();
    QVERIFY(addEnabled);
    QVERIFY(entries.contains(QStringLiteral("Edit Value")));
    QVERIFY(!entries.contains(QStringLiteral("Select category")));

    // A category action: a submenu, no free-text edit; "All" is -1.
    auto* stop = new QTreeWidgetItem(table, {QStringLiteral("Stop Category"), QString()});
    stop->setData(0, Qt::UserRole, 6);
    table->setCurrentItem(stop);
    openMenu();
    QVERIFY(entries.contains(QStringLiteral("Select category")));
    QVERIFY(!entries.contains(QStringLiteral("Edit Value")));
    QCOMPARE(stop->text(1), QStringLiteral("-1"));

    // The sixteenth is the last.
    while (table->topLevelItemCount() < 16) {
        auto* filler = new QTreeWidgetItem(table, {QStringLiteral("Source Limit"), QStringLiteral("1")});
        filler->setData(0, Qt::UserRole, 3);
    }
    openMenu();
    QVERIFY(!addEnabled);
}

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

/// The spin stopped at 10 000, so a larger stored connection limit was cut down by
/// the next OK -- the same failure the queue-size slider had.
void TestOptionsDialogSizing::aLargeConnectionLimitSurvivesTheSpin()
{
    // The two port spins on this page also stop at 65535, so match on the value too.
    thePrefs.setPort(4662);
    thePrefs.setUdpPort(4672);
    thePrefs.setMaxConnections(20'000);
    OptionsDialog dlg(nullptr, nullptr);

    QSpinBox* conns = nullptr;
    for (QSpinBox* s : stackOf(dlg)->widget(OptionsDialog::PageConnection)->findChildren<QSpinBox*>())
        if (s->maximum() == 65535 && s->value() == 20'000)
            conns = s;
    QVERIFY(conns);
    QCOMPARE(conns->minimum(), 1);
    thePrefs.setMaxConnections(500);
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

// ---------------------------------------------------------------------------
// First start wizard
// ---------------------------------------------------------------------------

namespace {

/// The settings the wizard reads and writes, put back when a test is done.
struct WizardPrefsGuard {
    uint32 capDown = thePrefs.maxGraphDownloadRate();
    uint32 capUp = thePrefs.maxGraphUploadRate();
    uint32 maxDown = thePrefs.maxDownload();
    uint32 maxUp = thePrefs.maxUpload();
    uint16 port = thePrefs.port();
    uint16 udpPort = thePrefs.udpPort();
    bool kad = thePrefs.kadEnabled();
    bool ed2k = thePrefs.networkED2K();

    ~WizardPrefsGuard()
    {
        thePrefs.setMaxGraphDownloadRate(capDown);
        thePrefs.setMaxGraphUploadRate(capUp);
        thePrefs.setMaxDownload(maxDown);
        thePrefs.setMaxUpload(maxUp);
        thePrefs.setPort(port);
        thePrefs.setUdpPort(udpPort);
        thePrefs.setKadEnabled(kad);
        thePrefs.setNetworkED2K(ed2k);
    }
};

void setBandwidth(uint32 capDown, uint32 capUp, uint32 maxDown, uint32 maxUp)
{
    // capacity first: the upload limit is clamped to it
    thePrefs.setMaxGraphDownloadRate(capDown);
    thePrefs.setMaxGraphUploadRate(capUp);
    thePrefs.setMaxDownload(maxDown);
    thePrefs.setMaxUpload(maxUp);
}

QPushButton* wizardButton(const FirstStartWizard& wizard, const QString& text)
{
    for (QPushButton* b : wizard.findChildren<QPushButton*>())
        if (b->text() == text)
            return b;
    return nullptr;
}

/// Click Next until the last page; returns how many pages that took.
int walkToLastPage(const FirstStartWizard& wizard)
{
    int clicks = 0;
    while (QPushButton* next = wizardButton(wizard, QStringLiteral("Next >"))) {
        if (++clicks > 20)
            break;   // a page refused to let go
        next->click();
    }
    return clicks;
}

QString selectedLine(const FirstStartWizard& wizard)
{
    const auto* list = wizard.findChild<QTreeWidget*>();
    return list && list->currentItem() ? list->currentItem()->text(0) : QString();
}

bool selectLine(const FirstStartWizard& wizard, const QString& name)
{
    auto* list = wizard.findChild<QTreeWidget*>();
    const auto found = list->findItems(name, Qt::MatchExactly);
    if (found.size() != 1)
        return false;
    list->setCurrentItem(found.front());
    return true;
}

} // namespace

/// A fixed-size dialog clips rather than grows, so a wrapped paragraph that does not fit
/// is simply cut — on either page, and the stack is as tall as its taller one.
void TestOptionsDialogSizing::theWizardShowsAllOfItsText()
{
    for (const auto start : {FirstStartWizard::StartPage::Ports, FirstStartWizard::StartPage::Speed}) {
        FirstStartWizard wizard(nullptr, nullptr, start);
        wizard.show();
        QVERIFY(QTest::qWaitForWindowExposed(&wizard));

        for (const QLabel* label : wizard.findChildren<QLabel*>()) {
            if (!label->isVisible() || !label->wordWrap())
                continue;
            QVERIFY2(label->height() >= label->heightForWidth(label->width()),
                     qPrintable(label->text().left(40)));
        }
        const auto* list = wizard.findChild<QTreeWidget*>();
        QCOMPARE(list->isVisible(), start == FirstStartWizard::StartPage::Speed);
    }
}

/// 250/500 were the shipped limits before the speed page existed. An install still on
/// exactly those gets the new defaults by clicking through.
void TestOptionsDialogSizing::theWizardOffersTheNewDefaultsToAnUntunedInstall()
{
    const WizardPrefsGuard guard;
    setBandwidth(500, 250, 500, 250);

    FirstStartWizard wizard(nullptr);
    QCOMPARE(selectedLine(wizard), QStringLiteral("Unknown (recommended defaults)"));

    // Page one: nothing to go back to, and no Finish yet.
    QVERIFY(!wizardButton(wizard, QStringLiteral("< Back"))->isEnabled());
    QVERIFY(!wizardButton(wizard, QStringLiteral("Finish")));
    wizardButton(wizard, QStringLiteral("Next >"))->click();
    QVERIFY(wizardButton(wizard, QStringLiteral("< Back"))->isEnabled());
    QCOMPARE(wizard.result(), int(QDialog::Rejected));   // still open
    walkToLastPage(wizard);

    wizardButton(wizard, QStringLiteral("Finish"))->click();
    QCOMPARE(wizard.result(), int(QDialog::Accepted));

    const QCborMap applied = wizard.appliedSettings();
    QCOMPARE(applied.value(QStringLiteral("maxDownload")).toInteger(-1), 0);   // unlimited
    QCOMPARE(applied.value(QStringLiteral("maxUpload")).toInteger(), 500);
    QCOMPARE(applied.value(QStringLiteral("maxGraphDownloadRate")).toInteger(), 12500);
    QCOMPARE(applied.value(QStringLiteral("maxGraphUploadRate")).toInteger(), 1250);
    QCOMPARE(thePrefs.maxDownload(), 0u);
    QCOMPARE(thePrefs.maxUpload(), 500u);
}

/// The wizard also runs once after an upgrade, on installs that were set up years ago.
/// Clicking through must not move their limits, nor switch a disabled UDP port back on.
void TestOptionsDialogSizing::theWizardLeavesTunedLimitsAndADisabledUdpPortAlone()
{
    const WizardPrefsGuard guard;
    setBandwidth(9000, 900, 8000, 700);
    thePrefs.setPort(4662);
    thePrefs.setUdpPort(0);

    thePrefs.setKadEnabled(true);
    thePrefs.setNetworkED2K(true);

    FirstStartWizard wizard(nullptr);
    QCOMPARE(selectedLine(wizard), QStringLiteral("Keep current settings"));

    // G47: Kad runs over UDP. With UDP off its box is unticked and greyed
    // (MFC PShtWiz1.cpp:662-668); it used to stay ticked.
    QCheckBox* kad = nullptr;
    for (QCheckBox* box : wizard.findChildren<QCheckBox*>())
        if (box->text() == QStringLiteral("Kad"))
            kad = box;
    QVERIFY(kad);
    QVERIFY(!kad->isEnabled());
    QVERIFY(!kad->isChecked());

    walkToLastPage(wizard);
    wizardButton(wizard, QStringLiteral("Finish"))->click();
    QCOMPARE(wizard.result(), int(QDialog::Accepted));

    const QCborMap applied = wizard.appliedSettings();
    QCOMPARE(applied.value(QStringLiteral("kadEnabled")).toBool(true), false);
    QVERIFY(!applied.contains(QStringLiteral("maxDownload")));
    QVERIFY(!applied.contains(QStringLiteral("maxGraphUploadRate")));
    QCOMPARE(applied.value(QStringLiteral("port")).toInteger(), 4662);
    QCOMPARE(applied.value(QStringLiteral("udpPort")).toInteger(-1), 0);
    QCOMPARE(thePrefs.maxDownload(), 8000u);
    QCOMPARE(thePrefs.maxUpload(), 700u);
    QCOMPARE(thePrefs.udpPort(), uint16(0));
}

void TestOptionsDialogSizing::aWizardLineTypeBecomesCapacityAndLimits()
{
    const WizardPrefsGuard guard;
    setBandwidth(9000, 900, 8000, 700);

    // Opened the way Options > Connection > Wizard does: straight on the speed page.
    FirstStartWizard wizard(nullptr, nullptr, FirstStartWizard::StartPage::Speed);
    QVERIFY(selectLine(wizard, QStringLiteral("VDSL 100")));
    wizardButton(wizard, QStringLiteral("Finish"))->click();
    QCOMPARE(wizard.result(), int(QDialog::Accepted));

    const QCborMap applied = wizard.appliedSettings();
    QCOMPARE(applied.value(QStringLiteral("maxGraphDownloadRate")).toInteger(), 12207);
    QCOMPARE(applied.value(QStringLiteral("maxGraphUploadRate")).toInteger(), 4883);
    QCOMPARE(applied.value(QStringLiteral("maxDownload")).toInteger(), 10986);
    QCOMPARE(applied.value(QStringLiteral("maxUpload")).toInteger(), 3906);
    // The core clamps the upload limit to the capacity; set in the wrong order it would
    // have been cut to the old 900.
    QCOMPARE(thePrefs.maxUpload(), 3906u);
    QCOMPARE(thePrefs.maxGraphUploadRate(), 4883u);
}

void TestOptionsDialogSizing::theWizardRateFieldsFollowTheLineAndSwitchToCustom()
{
    const WizardPrefsGuard guard;
    FirstStartWizard wizard(nullptr, nullptr, FirstStartWizard::StartPage::Speed);

    const auto spins = wizard.findChildren<QDoubleSpinBox*>();
    QCOMPARE(spins.size(), 2);
    QVERIFY(selectLine(wizard, QStringLiteral("Starlink 200")));
    // Never greyed out: they show the line and take an edit at once.
    QVERIFY(spins[0]->isEnabled() && spins[1]->isEnabled());
    QCOMPARE(spins[0]->value(), 200.0);
    QCOMPARE(spins[1]->value(), 20.0);
    QCOMPARE(selectedLine(wizard), QStringLiteral("Starlink 200"));

    spins[1]->setValue(25.0);
    QCOMPARE(selectedLine(wizard), QStringLiteral("Custom"));
    QCOMPARE(spins[0]->value(), 200.0);   // the other rate carries over

    wizardButton(wizard, QStringLiteral("Finish"))->click();
    const QCborMap applied = wizard.appliedSettings();
    QCOMPARE(applied.value(QStringLiteral("maxGraphDownloadRate")).toInteger(), 24414);
    QCOMPARE(applied.value(QStringLiteral("maxGraphUploadRate")).toInteger(), 3052);
}

void TestOptionsDialogSizing::theWizardLimitFieldsFollowTheLineAndTakeAnEdit()
{
    const WizardPrefsGuard guard;
    setBandwidth(9000, 900, 8000, 700);
    FirstStartWizard wizard(nullptr, nullptr, FirstStartWizard::StartPage::Speed);

    auto* down = wizard.findChild<QSpinBox*>(QStringLiteral("limitDown"));
    auto* up = wizard.findChild<QSpinBox*>(QStringLiteral("limitUp"));
    QVERIFY(down && up);
    QCOMPARE(selectedLine(wizard), QStringLiteral("Keep current settings"));
    QCOMPARE(down->value(), 8000);
    QCOMPARE(up->value(), 700);

    QVERIFY(selectLine(wizard, QStringLiteral("VDSL 100")));
    QCOMPARE(down->value(), 10986);
    QCOMPARE(up->value(), 3906);

    // A typed limit keeps the line, and 0 is unlimited.
    down->setValue(0);
    up->setValue(2000);
    QCOMPARE(selectedLine(wizard), QStringLiteral("VDSL 100"));
    QCOMPARE(down->text(), QStringLiteral("Unlimited"));

    wizardButton(wizard, QStringLiteral("Finish"))->click();
    const QCborMap applied = wizard.appliedSettings();
    QCOMPARE(applied.value(QStringLiteral("maxGraphDownloadRate")).toInteger(), 12207);
    QCOMPARE(applied.value(QStringLiteral("maxGraphUploadRate")).toInteger(), 4883);
    QCOMPARE(applied.value(QStringLiteral("maxDownload")).toInteger(), 0);
    QCOMPARE(applied.value(QStringLiteral("maxUpload")).toInteger(), 2000);

    // On "Keep current settings" a typed limit is written; above the capacity raises it.
    FirstStartWizard keep(nullptr, nullptr, FirstStartWizard::StartPage::Speed);
    QCOMPARE(selectedLine(keep), QStringLiteral("Keep current settings"));
    keep.findChild<QSpinBox*>(QStringLiteral("limitUp"))->setValue(6000);
    wizardButton(keep, QStringLiteral("Finish"))->click();
    QCOMPARE(keep.appliedSettings().value(QStringLiteral("maxUpload")).toInteger(), 6000);
    QCOMPARE(keep.appliedSettings().value(QStringLiteral("maxGraphUploadRate")).toInteger(), 6000);
    QCOMPARE(thePrefs.maxUpload(), 6000u);
}

/// G32, deliberate: two pages. MFC's seven (PShtWiz1.cpp:775-797) were tried and taken
/// back — nick, autoconnect, priorities and obfuscation stay in Options.
void TestOptionsDialogSizing::theWizardHasTwoPages()
{
    const WizardPrefsGuard guard;
    thePrefs.setKadEnabled(true);
    thePrefs.setNetworkED2K(true);
    thePrefs.setUdpPort(4672);

    FirstStartWizard wizard(nullptr);
    auto* pages = wizard.findChild<QStackedWidget*>();
    QVERIFY(pages);
    QCOMPARE(pages->count(), 2);
    QCOMPARE(pages->currentIndex(), 0);
    QCOMPARE(walkToLastPage(wizard), 1);
    QVERIFY(wizardButton(wizard, QStringLiteral("Finish")));

    // Finish writes nothing the two pages do not show
    wizardButton(wizard, QStringLiteral("Finish"))->click();
    const QCborMap applied = wizard.appliedSettings();
    for (const char* key : {"nick", "autoConnect", "autoDownloadPriority", "autoSharedFilesPriority",
                            "safeServerConnect", "cryptLayerRequested", "startWithOS"})
        QVERIFY2(!applied.contains(QString::fromLatin1(key)), key);

    FirstStartWizard speed(nullptr, nullptr, FirstStartWizard::StartPage::Speed);
    QCOMPARE(speed.findChild<QStackedWidget*>()->currentIndex(), 1);
}

/// G47, the other direction: re-enabling UDP gives Kad its tick back.
void TestOptionsDialogSizing::theWizardBringsKadBackWithUdp()
{
    const WizardPrefsGuard guard;
    thePrefs.setKadEnabled(true);
    thePrefs.setUdpPort(4672);

    FirstStartWizard wizard(nullptr);
    QCheckBox* kad = nullptr;
    QCheckBox* udpOff = nullptr;
    for (QCheckBox* b : wizard.findChildren<QCheckBox*>()) {
        if (b->text() == QStringLiteral("Kad"))
            kad = b;
        if (b->text() == QStringLiteral("Disable"))
            udpOff = b;
    }
    QVERIFY(kad && udpOff);
    QVERIFY(kad->isChecked() && kad->isEnabled());

    udpOff->setChecked(true);
    QVERIFY(!kad->isChecked() && !kad->isEnabled());
    udpOff->setChecked(false);
    QVERIFY(kad->isChecked() && kad->isEnabled());
}

void TestOptionsDialogSizing::theWizardNeedsANetwork()
{
    const WizardPrefsGuard guard;
    thePrefs.setKadEnabled(false);
    thePrefs.setNetworkED2K(false);

    FirstStartWizard wizard(nullptr, nullptr, FirstStartWizard::StartPage::Speed);

    // The refusal is a modal box; answer it or the click never returns.
    bool warned = false;
    QTimer::singleShot(0, &wizard, [&warned] {
        if (QWidget* box = QApplication::activeModalWidget()) {
            warned = true;
            box->close();
        }
    });
    wizardButton(wizard, QStringLiteral("Finish"))->click();

    QVERIFY(warned);
    QCOMPARE(wizard.result(), int(QDialog::Rejected));
    QVERIFY(wizard.appliedSettings().isEmpty());
    // ...and it is back on the page where that can be fixed.
    QVERIFY(wizardButton(wizard, QStringLiteral("Next >")));
}

void TestOptionsDialogSizing::theWizardReportsTheRealPortMappingResult()
{
    const WizardPrefsGuard guard;
    FirstStartWizard wizard(nullptr, nullptr, FirstStartWizard::StartPage::Ports);   // where the status is shown
    wizard.show();
    QVERIFY(QTest::qWaitForWindowExposed(&wizard));
    auto* status = wizard.findChild<QLabel*>(QStringLiteral("upnpStatus"));
    auto* progress = wizard.findChild<QProgressBar*>();
    QVERIFY(status && progress);
    const QSize before = wizard.size();

    const auto info = [](PortMapStatus s, const QString& method = {}, const QString& address = {}) {
        QCborMap map;
        map.insert(QStringLiteral("status"), static_cast<int>(s));
        map.insert(QStringLiteral("methodText"), method);
        map.insert(QStringLiteral("externalAddress"), address);
        return map;
    };

    // Still probing: no verdict yet.
    wizard.showPortMapStatus(info(PortMapStatus::Probing));
    QCOMPARE(portMapStatusSummary(info(PortMapStatus::Probing)).outcome, PortMapOutcome::Pending);
    QVERIFY(!status->text().isEmpty());

    wizard.showPortMapStatus(info(PortMapStatus::Mapped, QStringLiteral("PCP"), QStringLiteral("203.0.113.7")));
    QVERIFY(status->text().contains(QStringLiteral("PCP")));
    QVERIFY(status->text().contains(QStringLiteral("203.0.113.7")));
    QVERIFY(!progress->isVisible());
    QCOMPARE(portMapStatusSummary(info(PortMapStatus::Mapped)).outcome, PortMapOutcome::Ok);

    // Granted behind carrier-grade NAT is not a success; the longest text must fit.
    wizard.showPortMapStatus(info(PortMapStatus::Degraded, QStringLiteral("UPnP"), QStringLiteral("100.83.250.167")));
    QCOMPARE(portMapStatusSummary(info(PortMapStatus::Degraded)).outcome, PortMapOutcome::Warning);
    QVERIFY(status->text().contains(QStringLiteral("100.83.250.167")));
    QCoreApplication::processEvents();
    QVERIFY2(status->height() >= status->heightForWidth(status->width()),
             qPrintable(QStringLiteral("status label is %1 px high, text needs %2")
                            .arg(status->height()).arg(status->heightForWidth(status->width()))));
    QCOMPARE(wizard.size(), before);

    for (const PortMapStatus failed : {PortMapStatus::NotMapped, PortMapStatus::Failed, PortMapStatus::Disabled})
        QCOMPARE(portMapStatusSummary(info(failed)).outcome, PortMapOutcome::Failed);
}

void TestOptionsDialogSizing::theWizardDoesNotWaitWithoutACore()
{
    const WizardPrefsGuard guard;
    FirstStartWizard wizard(nullptr);
    wizard.show();
    QVERIFY(QTest::qWaitForWindowExposed(&wizard));

    wizardButton(wizard, QStringLiteral("Use UPnP to Setup Ports"))->click();

    QVERIFY(!wizard.findChild<QProgressBar*>()->isVisible());
    QVERIFY(wizardButton(wizard, QStringLiteral("Use UPnP to Setup Ports"))->isEnabled());
    QVERIFY(!wizard.findChild<QLabel*>(QStringLiteral("upnpStatus"))->text().isEmpty());
}

/// A live rebind is a status bar line; anything that leaves the old ports up needs a box
/// naming the ports still in use.
void TestOptionsDialogSizing::aPortChangeIsReportedAsItWasApplied()
{
    const auto reply = [](PortApplyResult result) {
        return QCborMap{{QStringLiteral("ports"), int(result)},
                        {QStringLiteral("tcpPort"), 5662},
                        {QStringLiteral("udpPort"), 5672}};
    };

    QVERIFY(portChangeSummary(reply(PortApplyResult::Unchanged)).text.isEmpty());
    QVERIFY(portChangeSummary(QCborMap{}).text.isEmpty());   // older core: no field

    const PortChangeSummary applied = portChangeSummary(reply(PortApplyResult::Applied));
    QVERIFY(!applied.needsAttention);
    QVERIFY(applied.text.contains(QStringLiteral("5662")));
    QVERIFY(applied.text.contains(QStringLiteral("5672")));

    for (const PortApplyResult kept : {PortApplyResult::RestartRequired, PortApplyResult::BindFailed}) {
        const PortChangeSummary summary = portChangeSummary(reply(kept));
        QVERIFY(summary.needsAttention);
        QVERIFY(summary.text.contains(QStringLiteral("5662")));
        QVERIFY(summary.text.contains(QStringLiteral("5672")));
    }
    QVERIFY(portChangeSummary(reply(PortApplyResult::RestartRequired))
                .text.contains(QStringLiteral("restarting")));
}

/// The button restarts the core, so it belongs on the one notice a restart resolves.
void TestOptionsDialogSizing::onlyAPendingPortChangeOffersARestart()
{
    const auto show = [](PortApplyResult result, std::function<void()> restart) {
        const QCborMap map{{QStringLiteral("ports"), int(result)},
                           {QStringLiteral("tcpPort"), 5662},
                           {QStringLiteral("udpPort"), 5672}};
        QWidget owner;
        showPortChangeResult(&owner, Ipc::IpcMessage::makeResult(1, true, QCborValue(map)),
                             std::move(restart));
        auto* box = owner.findChild<QMessageBox*>();
        int buttons = -1;
        if (box) {
            buttons = int(box->buttons().size());
            box->buttons().constFirst()->click();
        }
        return buttons;
    };

    int restarts = 0;
    QCOMPARE(show(PortApplyResult::RestartRequired, [&restarts] { ++restarts; }), 2);
    QCOMPARE(restarts, 1);   // first button is Restart Now

    QCOMPARE(show(PortApplyResult::BindFailed, [&restarts] { ++restarts; }), 1);
    QCOMPARE(show(PortApplyResult::RestartRequired, {}), 1);   // nothing to restart with
    QCOMPARE(restarts, 1);
}

// A tab bar without scroll buttons (macOS) elides its titles when squeezed; the default
// width has to leave them readable, while the minimum stays the designed one.
void TestOptionsDialogSizing::defaultWidthShowsEveryTabTitle()
{
    const auto build = [](QDialog& dialog, bool scrollButtons) {
        auto* layout = new QVBoxLayout(&dialog);
        auto* tabs = new QTabWidget;
        tabs->setUsesScrollButtons(scrollButtons);
        for (int i = 0; i < 8; ++i)
            tabs->addTab(new QWidget, QStringLiteral("A rather long tab title %1").arg(i));
        layout->addWidget(tabs);
        eMule::DialogSizing::applySize(&dialog, QSize(300, 200), QSize(320, 240));
        return tabs;
    };

    QDialog squeezing;
    const QTabWidget* tabs = build(squeezing, false);
    const int titles = tabs->tabBar()->sizeHint().width();
    QVERIFY(titles > 320);
    if (titles + 80 > squeezing.screen()->availableGeometry().width())
        QSKIP("screen too narrow for the tab row");
    QVERIFY2(squeezing.width() >= titles, qPrintable(QStringLiteral("%1 < %2")
                                              .arg(squeezing.width()).arg(titles)));
    QVERIFY(squeezing.minimumWidth() < titles);
    QCOMPARE(squeezing.height(), 240);
    squeezing.show();
    QVERIFY(QTest::qWaitForWindowExposed(&squeezing));
    QVERIFY2(tabs->tabBar()->width() >= titles, qPrintable(QStringLiteral("%1 < %2")
                                                    .arg(tabs->tabBar()->width()).arg(titles)));

    // One more tab while the window is up (a walker step onto an archive): it follows
    auto* live = squeezing.findChild<QTabWidget*>();
    live->addTab(new QWidget, QStringLiteral("One more long tab title"));
    const int wider = live->tabBar()->sizeHint().width();
    QVERIFY(wider > titles);
    if (wider + 80 <= squeezing.screen()->availableGeometry().width()) {
        eMule::DialogSizing::applySize(&squeezing, QSize(300, 200), QSize(320, 240));
        QVERIFY2(squeezing.width() >= wider, qPrintable(QStringLiteral("%1 < %2")
                                                 .arg(squeezing.width()).arg(wider)));

        // ...but a narrower size the user picks afterwards survives the next re-fit
        const int chosen = squeezing.minimumWidth() + 10;   // the squeezed tabs set the floor
        QVERIFY(chosen < wider);
        squeezing.resize(chosen, squeezing.height());
        eMule::DialogSizing::applySize(&squeezing, QSize(300, 200), QSize(320, 240));
        QCOMPARE(squeezing.width(), chosen);
    }

    QDialog scrolling;   // arrows keep the titles whole; nothing to widen
    build(scrolling, true);
    QCOMPARE(scrolling.width(), 320);
}

QTEST_MAIN(TestOptionsDialogSizing)
#include "tst_OptionsDialogSizing.moc"
