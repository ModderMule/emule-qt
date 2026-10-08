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

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCborMap>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
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
    void breadcrumbNamesGroupAndPage();
    void usenetPageHasAccountAndAdvancedTabs();

    // --- values loaded into the pages (offline: IpcClient nullptr) ---------
    void anUnlimitedLimitParksAtTheCapacity();
    void theIpcLogBoxShowsTheStoredValue();
    void aLargeQueueSizeSurvivesTheSlider();
    void aLargeConnectionLimitSurvivesTheSpin();
    void extendedValuesComeFromThePrefsNotFallbacks();
    void generalPageControlsEnableApply();

    // First start wizard: compiled into this binary anyway, and just as daemon-less.
    void theWizardShowsAllOfItsText();
    void theWizardOffersTheNewDefaultsToAnUntunedInstall();
    void theWizardLeavesTunedLimitsAndADisabledUdpPortAlone();
    void aWizardLineTypeBecomesCapacityAndLimits();
    void theWizardNeedsANetwork();
    void theWizardReportsTheRealPortMappingResult();
    void theWizardDoesNotWaitWithoutACore();
    void aPortChangeIsReportedAsItWasApplied();
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

    FirstStartWizard wizard(nullptr);
    QCOMPARE(selectedLine(wizard), QStringLiteral("Keep current settings"));

    wizardButton(wizard, QStringLiteral("Next >"))->click();
    wizardButton(wizard, QStringLiteral("Finish"))->click();
    QCOMPARE(wizard.result(), int(QDialog::Accepted));

    const QCborMap applied = wizard.appliedSettings();
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
    FirstStartWizard wizard(nullptr);
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

QTEST_MAIN(TestOptionsDialogSizing)
#include "tst_OptionsDialogSizing.moc"
