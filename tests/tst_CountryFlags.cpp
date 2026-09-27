/// @file tst_CountryFlags.cpp
/// @brief GUI half of IP2Country: flag icons, the Country column and its visibility
///        rule, the models' "cc" plumbing, and header layouts surviving the extra column.

#include "controls/AbstractListView.h"
#include "controls/ClientListModel.h"
#include "controls/KadContactsModel.h"
#include "controls/ServerListModel.h"
#include "utils/CountryFlags.h"

#include <QCborArray>
#include <QCborMap>
#include <QDirIterator>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QTest>

using namespace eMule;

class tst_CountryFlags : public QObject {
    Q_OBJECT

private slots:
    void init();

    void flag_knownAndUnknownCodes();
    void flag_everyIsoTerritoryHasOne();
    void withFlag_widthAndOffSwitch();
    void columnText_followsMode();
    void clientModel_countryColumnAndDecoration();
    void serverModel_parsesCc();
    void kadModel_countryColumn();
    void bindCountryColumn_followsSettingOverSavedLayout();
    void restoreHeader_keepsLayoutWhenColumnAppended();
    void delegate_widensComposedDecoration();
};

void tst_CountryFlags::init()
{
    CountryFlags::setSettings(true, CountryFlags::NameMode::Hidden);
}

void tst_CountryFlags::flag_knownAndUnknownCodes()
{
    QVERIFY(!CountryFlags::flag(QStringLiteral("DE")).isNull());
    QVERIFY(!CountryFlags::flag(QStringLiteral("de")).isNull());
    QVERIFY(!CountryFlags::flag(QStringLiteral("GB")).isNull());
    QVERIFY(CountryFlags::flag(QStringLiteral("QQ")).isNull());
    QVERIFY(CountryFlags::flag(QString()).isNull());
    QVERIFY(CountryFlags::flag(QStringLiteral("DEU")).isNull());
}

void tst_CountryFlags::flag_everyIsoTerritoryHasOne()
{
    // Every territory Qt knows by a two-letter code; a few Qt-only pseudo codes
    // and territories MaxMind never returns are allowed to lack a flag.
    const QSet<QString> allowedMissing = {
        QStringLiteral("AC"), QStringLiteral("CP"), QStringLiteral("DG"), QStringLiteral("EA"),
        QStringLiteral("IC"), QStringLiteral("TA"), QStringLiteral("XK"), QStringLiteral("QO"),
        QStringLiteral("EZ"), QStringLiteral("UN"), QStringLiteral("CQ"), QStringLiteral("419"),
        QStringLiteral("150"), QStringLiteral("001")};
    QStringList missing;
    for (int t = QLocale::Afghanistan; t <= QLocale::LastTerritory; ++t) {
        const QString code = QLocale::territoryToCode(static_cast<QLocale::Territory>(t));
        if (code.size() != 2 || allowedMissing.contains(code))
            continue;
        if (CountryFlags::flag(code).isNull())
            missing << code;
    }
    QVERIFY2(missing.size() <= 3, qPrintable(missing.join(u' ')));
}

void tst_CountryFlags::withFlag_widthAndOffSwitch()
{
    const QIcon base(QStringLiteral(":/icons/ClientDefault.ico"));
    QVERIFY(!base.isNull());

    const QIcon composed = CountryFlags::withFlag(base, QStringLiteral("SE"));
    const QSize s = composed.availableSizes().value(0);
    QVERIFY(s.isValid());
    QCOMPARE(s.width() * 16, 36 * s.height());   // 16 + 2 + 18 wide, 16 high

    // Unknown country still reserves the slot, so names stay aligned
    const QSize blank = CountryFlags::withFlag(base, QString()).availableSizes().value(0);
    QCOMPARE(blank.width() * 16, 36 * blank.height());

    // No base icon: just the flag slot
    const QSize alone = CountryFlags::withFlag(QIcon(), QStringLiteral("SE")).availableSizes().value(0);
    QCOMPARE(alone.width() * 16, 18 * alone.height());

    CountryFlags::setSettings(false, CountryFlags::NameMode::Hidden);
    QCOMPARE(CountryFlags::withFlag(base, QStringLiteral("SE")).cacheKey(), base.cacheKey());
}

void tst_CountryFlags::columnText_followsMode()
{
    CountryFlags::setSettings(true, CountryFlags::NameMode::Short);
    QCOMPARE(CountryFlags::columnText(QStringLiteral("se")), QStringLiteral("SE"));
    CountryFlags::setSettings(true, CountryFlags::NameMode::Long);
    QCOMPARE(CountryFlags::columnText(QStringLiteral("SE")), QStringLiteral("Sweden"));
    QCOMPARE(CountryFlags::columnText(QString()), QString());
    CountryFlags::setSettings(true, CountryFlags::NameMode::Hidden);
    QCOMPARE(CountryFlags::columnText(QStringLiteral("SE")), QString());
    QCOMPARE(CountryFlags::tooltip(QStringLiteral("SE")), QStringLiteral("Sweden (SE)"));
    QCOMPARE(CountryFlags::tooltip(QString()), QString());
}

void tst_CountryFlags::clientModel_countryColumnAndDecoration()
{
    CountryFlags::setSettings(true, CountryFlags::NameMode::Long);
    for (const auto mode : {ClientListMode::Uploading, ClientListMode::Downloading,
                            ClientListMode::OnQueue, ClientListMode::KnownClients}) {
        ClientListModel model(mode);
        ClientRow row;
        row.userName = QStringLiteral("peer");
        row.cc = QStringLiteral("JP");
        model.setClients({row});

        const int col = model.countryColumn();
        QCOMPARE(col, model.columnCount() - 1);
        QCOMPARE(model.headerData(col, Qt::Horizontal).toString(), QStringLiteral("Country"));
        QCOMPARE(model.data(model.index(0, col)).toString(), QStringLiteral("Japan"));
        QCOMPARE(model.data(model.index(0, col), Qt::UserRole).toString(), QStringLiteral("Japan"));
        const auto icon = model.data(model.index(0, 0), Qt::DecorationRole).value<QIcon>();
        const QSize s = icon.availableSizes().value(0);
        QVERIFY(s.width() > s.height());
        QCOMPARE(model.data(model.index(0, 0), Qt::ToolTipRole).toString(),
                 QStringLiteral("Japan (JP)"));
    }
}

void tst_CountryFlags::serverModel_parsesCc()
{
    CountryFlags::setSettings(true, CountryFlags::NameMode::Short);
    ServerListModel model;
    QCborMap m;
    m.insert(QStringLiteral("name"), QStringLiteral("srv"));
    m.insert(QStringLiteral("address"), QStringLiteral("81.2.69.160"));
    m.insert(QStringLiteral("cc"), QStringLiteral("GB"));
    model.refreshFromCborArray(QCborArray{m});
    QCOMPARE(model.data(model.index(0, ServerListModel::ColCountry)).toString(), QStringLiteral("GB"));
    QVERIFY(!model.data(model.index(0, ServerListModel::ColName), Qt::DecorationRole)
                 .value<QIcon>().isNull());

    CountryFlags::setSettings(false, CountryFlags::NameMode::Short);
    QVERIFY(!model.data(model.index(0, ServerListModel::ColName), Qt::DecorationRole).isValid());
}

void tst_CountryFlags::kadModel_countryColumn()
{
    CountryFlags::setSettings(true, CountryFlags::NameMode::Short);
    KadContactsModel model;
    KadContactRow row;
    row.cc = QStringLiteral("SE");
    model.setContacts({row});
    QCOMPARE(model.columnCount(), int(KadContactsModel::ColCount));
    QCOMPARE(model.data(model.index(0, KadContactsModel::ColCountry)).toString(), QStringLiteral("SE"));
}

void tst_CountryFlags::bindCountryColumn_followsSettingOverSavedLayout()
{
    ClientListModel model(ClientListMode::KnownClients);
    ListTreeView view;
    view.setModel(&model);
    view.bindColumns(QStringLiteral("tst_countryFlags_known"));
    CountryFlags::bindCountryColumn(&view, model.countryColumn());

    QVERIFY(view.header()->isSectionHidden(model.countryColumn()));

    CountryFlags::setSettings(true, CountryFlags::NameMode::Long);
    QVERIFY(!view.header()->isSectionHidden(model.countryColumn()));

    // A saved layout with the column visible must not win over "Hidden"
    const QByteArray shown = view.header()->saveState();
    CountryFlags::setSettings(true, CountryFlags::NameMode::Hidden);
    view.header()->restoreState(shown);
    QVERIFY(!view.header()->isSectionHidden(model.countryColumn()));
    view.applyColumnPolicy();
    QVERIFY(view.header()->isSectionHidden(model.countryColumn()));
}

void tst_CountryFlags::restoreHeader_keepsLayoutWhenColumnAppended()
{
    // Every list gained a Country column. Qt restores a layout saved with fewer
    // columns (widths, order, sort), so nobody's saved layout resets — pin that.
    QStandardItemModel oldModel(0, 3);
    QHeaderView oldHeader(Qt::Horizontal);
    oldHeader.setModel(&oldModel);
    oldHeader.setSortIndicatorShown(true);
    oldHeader.resizeSection(0, 211);
    oldHeader.resizeSection(1, 73);
    oldHeader.resizeSection(2, 97);
    oldHeader.moveSection(2, 0);
    oldHeader.setSortIndicator(1, Qt::DescendingOrder);
    const QByteArray state = oldHeader.saveState();

    QStandardItemModel newModel(0, 4);
    QHeaderView header(Qt::Horizontal);
    header.setModel(&newModel);
    QVERIFY(header.restoreState(state));
    QCOMPARE(header.sectionSize(0), 211);
    QCOMPARE(header.sectionSize(1), 73);
    QCOMPARE(header.sectionSize(2), 97);
    QCOMPARE(header.logicalIndex(0), 2);
    QCOMPARE(header.visualIndex(3), 3);
    QCOMPARE(header.sortIndicatorSection(), 1);
    QCOMPARE(header.sortIndicatorOrder(), Qt::DescendingOrder);
}

void tst_CountryFlags::delegate_widensComposedDecoration()
{
    CountryFlags::setSettings(true, CountryFlags::NameMode::Hidden);
    ClientListModel model(ClientListMode::Uploading);
    ClientRow row;
    row.userName = QStringLiteral("peer");
    row.cc = QStringLiteral("DE");
    model.setClients({row});

    ListTreeView view;
    view.setModel(&model);
    CountryFlags::bindFlagColumn(&view);
    auto* delegate = qobject_cast<FlagDecorationDelegate*>(view.itemDelegateForColumn(0));
    QVERIFY(delegate);

    QStyleOptionViewItem opt;
    opt.decorationSize = QSize(16, 16);
    struct Probe : FlagDecorationDelegate {
        using FlagDecorationDelegate::FlagDecorationDelegate;
        using FlagDecorationDelegate::initStyleOption;
    } probe;
    probe.initStyleOption(&opt, model.index(0, 0));
    QCOMPARE(opt.decorationSize, QSize(36, 16));
}

QTEST_MAIN(tst_CountryFlags)
#include "tst_CountryFlags.moc"
