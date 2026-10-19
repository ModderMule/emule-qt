/// @file tst_TaskbarProgress.cpp
/// @brief Taskbar / Dock download progress -- eMule::TaskbarProgress.
///
/// Port of MFC CemuleDlg::UpdateStatusBarProgress (srchybrid/EmuleDlg.cpp:3688).
/// Covers the platform-free parts: the state rules, the cache that keeps unchanged
/// values from reaching the native side, and the painted Dock icon.

#include "utils/TaskbarProgress.h"

#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QTest>

#include <vector>

using namespace eMule;

namespace {

class FakeBackend final : public TaskbarProgressBackend {
public:
    explicit FakeBackend(std::vector<TaskbarProgressValue>* log, int* reinits = nullptr)
        : m_log(log), m_reinits(reinits) {}
    void apply(const TaskbarProgressValue& value) override { m_log->push_back(value); }
    void reinitialize() override { if (m_reinits) ++*m_reinits; }

private:
    std::vector<TaskbarProgressValue>* m_log;
    int* m_reinits;
};

QIcon blankIcon()
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    return QIcon(pm);
}

/// Row through the middle of the bar, see renderDockProgressIcon()
int barRow(int size) { return size - size / 10 - size / 16; }

} // anonymous namespace

class tst_TaskbarProgress : public QObject {
    Q_OBJECT

private slots:
    void compute_emptyQueueIsNone();
    void compute_errorBeatsTransferring();
    void compute_transferringIsNormal();
    void compute_idleIsPaused();
    void compute_valueRoundsDownAndClamps();

    void cache_unchangedValueSkipsBackend();
    void cache_disableHidesAndEnableRestores();
    void cache_reinitializePushesAgain();
    void cache_destructorHides();

    void dockIcon_noneIsPlainIcon();
    void dockIcon_barColourFollowsState();
    void dockIcon_fillIsProportional();
};

void tst_TaskbarProgress::compute_emptyQueueIsNone()
{
    QCOMPARE(computeTaskbarProgress(0, 0, 5, 1), TaskbarProgressValue{});
    QCOMPARE(computeTaskbarProgress(-1, 0, 0, 0).state, TaskbarState::None);
}

void tst_TaskbarProgress::compute_errorBeatsTransferring()
{
    QCOMPARE(computeTaskbarProgress(100, 50, 3, 1).state, TaskbarState::Error);
}

void tst_TaskbarProgress::compute_transferringIsNormal()
{
    QCOMPARE(computeTaskbarProgress(100, 50, 3, 0),
             (TaskbarProgressValue{TaskbarState::Normal, 500}));
}

void tst_TaskbarProgress::compute_idleIsPaused()
{
    QCOMPARE(computeTaskbarProgress(100, 50, 0, 0).state, TaskbarState::Paused);
}

void tst_TaskbarProgress::compute_valueRoundsDownAndClamps()
{
    QCOMPARE(computeTaskbarProgress(1000000, 999999, 1, 0).permille, 999);   // not full yet
    QCOMPARE(computeTaskbarProgress(100, 100, 1, 0).permille, 1000);
    QCOMPARE(computeTaskbarProgress(100, 250, 1, 0).permille, 1000);
    QCOMPARE(computeTaskbarProgress(100, -5, 1, 0).permille, 0);
    // Large queue: no overflow
    const qint64 tb = qint64{1} << 40;
    QCOMPARE(computeTaskbarProgress(4 * tb, tb, 1, 0).permille, 250);
}

void tst_TaskbarProgress::cache_unchangedValueSkipsBackend()
{
    std::vector<TaskbarProgressValue> log;
    {
        TaskbarProgress progress(std::make_unique<FakeBackend>(&log));
        progress.update({});                                   // already hidden
        QVERIFY(log.empty());
        progress.update({TaskbarState::Normal, 100});
        progress.update({TaskbarState::Normal, 100});
        QCOMPARE(log.size(), size_t{1});
        progress.update({TaskbarState::Paused, 100});
        progress.update({TaskbarState::Paused, 101});
        QCOMPARE(log.size(), size_t{3});
        QCOMPARE(log.back(), (TaskbarProgressValue{TaskbarState::Paused, 101}));
    }
}

void tst_TaskbarProgress::cache_disableHidesAndEnableRestores()
{
    std::vector<TaskbarProgressValue> log;
    TaskbarProgress progress(std::make_unique<FakeBackend>(&log));
    progress.update({TaskbarState::Normal, 300});
    progress.setEnabled(false);
    QCOMPARE(log.back(), TaskbarProgressValue{});
    const size_t before = log.size();
    progress.update({TaskbarState::Normal, 400});           // remembered, not shown
    QCOMPARE(log.size(), before);
    progress.setEnabled(true);
    QCOMPARE(log.back(), (TaskbarProgressValue{TaskbarState::Normal, 400}));
}

void tst_TaskbarProgress::cache_reinitializePushesAgain()
{
    std::vector<TaskbarProgressValue> log;
    int reinits = 0;
    TaskbarProgress progress(std::make_unique<FakeBackend>(&log, &reinits));
    progress.update({TaskbarState::Error, 10});
    progress.reinitialize();
    QCOMPARE(reinits, 1);
    QCOMPARE(log.size(), size_t{2});
    QCOMPARE(log.back(), (TaskbarProgressValue{TaskbarState::Error, 10}));
}

void tst_TaskbarProgress::cache_destructorHides()
{
    std::vector<TaskbarProgressValue> log;
    {
        TaskbarProgress progress(std::make_unique<FakeBackend>(&log));
        progress.update({TaskbarState::Normal, 10});
    }
    QCOMPARE(log.back(), TaskbarProgressValue{});
}

void tst_TaskbarProgress::dockIcon_noneIsPlainIcon()
{
    const QImage img = renderDockProgressIcon(blankIcon(), {}, 128).toImage();
    QCOMPARE(img.pixelColor(64, barRow(128)).alpha(), 0);
}

void tst_TaskbarProgress::dockIcon_barColourFollowsState()
{
    constexpr int size = 128;
    for (const auto state : {TaskbarState::Normal, TaskbarState::Paused, TaskbarState::Error}) {
        const QImage img = renderDockProgressIcon(blankIcon(), {state, 1000}, size).toImage();
        QCOMPARE(img.pixelColor(size / 2, barRow(size)).rgb(), dockProgressColor(state).rgb());
    }
}

void tst_TaskbarProgress::dockIcon_fillIsProportional()
{
    constexpr int size = 128;
    const QRgb normal = dockProgressColor(TaskbarState::Normal).rgb();
    const QImage img = renderDockProgressIcon(blankIcon(), {TaskbarState::Normal, 500}, size).toImage();
    QCOMPARE(img.pixelColor(size / 4, barRow(size)).rgb(), normal);
    QVERIFY(img.pixelColor(3 * size / 4, barRow(size)).rgb() != normal);
    QVERIFY(img.pixelColor(3 * size / 4, barRow(size)).alpha() == 255);   // empty track
}

QTEST_MAIN(tst_TaskbarProgress)
#include "tst_TaskbarProgress.moc"
