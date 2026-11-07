/// @file tst_BtTrackerList.cpp
/// @brief One tracker edit for many torrents (#143): the list rules, no libtorrent.

#include "BtTrackerList.h"

#include <QTest>

using namespace eMule::bt;

namespace {
const QString kA = QStringLiteral("udp://a.example:80/announce");
const QString kB = QStringLiteral("udp://b.example:80/announce");
const QString kC = QStringLiteral("http://c.example/announce");
const QString kD = QStringLiteral("http://d.example/announce");
} // namespace

class tst_BtTrackerList : public QObject {
    Q_OBJECT

private slots:
    void addGoesToLastTier();
    void addSkipsKnownAndEmpty();
    void removeKeepsTiers();
    void exactReplaces();
    void exactEmptyDropsAll();
    void noChange();
};

void tst_BtTrackerList::addGoesToLastTier()
{
    const QList<BtTrackerEntry> cur{{kA, 0}, {kB, 2}};
    const auto out = editTrackerList(cur, {{kC}, {}, false});
    QCOMPARE(out, (QList<BtTrackerEntry>{{kA, 0}, {kB, 2}, {kC, 2}}));
    QCOMPARE(editTrackerList({}, {{kC}, {}, false}), (QList<BtTrackerEntry>{{kC, 0}}));
}

void tst_BtTrackerList::addSkipsKnownAndEmpty()
{
    const QList<BtTrackerEntry> cur{{kA, 0}};
    const auto out = editTrackerList(cur, {{QStringLiteral("  ") + kA, QString(), kC, kC + u' '}, {}, false});
    QCOMPARE(out, (QList<BtTrackerEntry>{{kA, 0}, {kC, 0}}));
}

void tst_BtTrackerList::removeKeepsTiers()
{
    const QList<BtTrackerEntry> cur{{kA, 0}, {kB, 1}, {kC, 1}};
    const auto out = editTrackerList(cur, {{kD}, {kB, kD}, false});
    // removal before adding: kD is added although also listed for removal
    QCOMPARE(out, (QList<BtTrackerEntry>{{kA, 0}, {kC, 1}, {kD, 1}}));
}

void tst_BtTrackerList::exactReplaces()
{
    const QList<BtTrackerEntry> cur{{kA, 0}, {kB, 1}, {kC, 2}};
    // kept ones keep their tier, remove is ignored
    const auto out = editTrackerList(cur, {{kD, kB}, {kB}, true});
    QCOMPARE(out, (QList<BtTrackerEntry>{{kB, 1}, {kD, 1}}));
}

void tst_BtTrackerList::exactEmptyDropsAll()
{
    QVERIFY(editTrackerList({{kA, 0}}, {{}, {}, true}).isEmpty());
}

void tst_BtTrackerList::noChange()
{
    const QList<BtTrackerEntry> cur{{kA, 0}, {kB, 1}};
    QCOMPARE(editTrackerList(cur, {{kA}, {kC}, false}), cur);
    QCOMPARE(editTrackerList(cur, {{kA, kB}, {}, true}), cur);
}

QTEST_GUILESS_MAIN(tst_BtTrackerList)
#include "tst_BtTrackerList.moc"
