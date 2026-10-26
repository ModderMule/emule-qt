#include "pch.h"
/// @file ClientDetailDialog.cpp
/// @brief Client detail dialog implementation.

#include "ClientDetailDialog.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

#include "net/Address.h"
#include "utils/CountryFlags.h"
#include "utils/DialogSizing.h"
#include "utils/OtherFunctions.h"
#include "utils/StringUtils.h"

namespace eMule {

// ── helpers ────────────────────────────────────────────────────────────

namespace {

QString str(const QCborMap& m, QLatin1StringView key)
{
    return m.value(key).toString();
}

qint64 num(const QCborMap& m, QLatin1StringView key)
{
    return m.value(key).toInteger();
}

double dbl(const QCborMap& m, QLatin1StringView key)
{
    return m.value(key).toDouble();
}

struct ValueRow {
    QString   caption;
    QLabel*   value = nullptr;   // null for a widget row (country)
};

/// Bold "label:" caption, right-aligned as in a QFormLayout.
QLabel* gridCaption(const QString& label)
{
    auto* caption = new QLabel(QStringLiteral("<b>%1:</b>").arg(label));
    caption->setAlignment(Qt::AlignRight | Qt::AlignTop);
    return caption;
}

/// Caption at (@p row, @p col), one-line value right of it spanning @p span columns.
void addGridRow(QGridLayout* grid, int row, int col, const QString& label, const QString& value,
                int span = 1)
{
    // One line, no word wrap: a wrapping label in a grid has its height measured at the
    // wrong width, which left a scrollbar and a gap under the groups
    auto* valueLabel = new QLabel(value);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    grid->addWidget(gridCaption(label), row, col);
    grid->addWidget(valueLabel, row, col + 1, 1, span, Qt::AlignTop);
}

/// Every caption/value pair of @p page in order, forms and grids alike.
QList<ValueRow> valueRows(const QWidget* page)
{
    const auto pair = [](const QLayoutItem* labelItem, const QLayoutItem* fieldItem) {
        const auto* caption = labelItem ? qobject_cast<QLabel*>(labelItem->widget()) : nullptr;
        return ValueRow{caption ? caption->text() : QString{},
                        fieldItem ? qobject_cast<QLabel*>(fieldItem->widget()) : nullptr};
    };
    QList<ValueRow> rows;
    for (const auto* grid : page->findChildren<QGridLayout*>()) {
        for (int r = 0; r < grid->rowCount(); ++r) {
            for (int c = 0; c + 1 < grid->columnCount(); c += 2) {
                const auto* labelItem = grid->itemAtPosition(r, c);
                // a spanned value covers this cell too: not a caption
                if (labelItem && (c == 0 || labelItem != grid->itemAtPosition(r, c - 1)))
                    rows.append(pair(labelItem, grid->itemAtPosition(r, c + 1)));
            }
        }
    }
    for (const auto* form : page->findChildren<QFormLayout*>()) {
        for (int i = 0; i < form->rowCount(); ++i)
            rows.append(pair(form->itemAt(i, QFormLayout::LabelRole),
                             form->itemAt(i, QFormLayout::FieldRole)));
    }
    return rows;
}

} // anonymous namespace

// ── constructor ────────────────────────────────────────────────────────

ClientDetailDialog::ClientDetailDialog(const QCborMap& d, QWidget* parent)
    : DetailDialog(parent, ContentScroll::On)
{
    setAttribute(Qt::WA_DeleteOnClose);

    // Wider than MFC's 322 DLU: General pairs two value columns.
    // Height is the content's to decide: the row count varies with the client (a friend
    // slot adds a row) and so does the wrapped height of the file-name values.
    setDesignedSize(QSize(640, 0), QSize(720, 0));

    ClientDetailDialog::setDetails(d);
}

// ── re-target ──────────────────────────────────────────────────────────

void ClientDetailDialog::setDetails(const QCborMap& d)
{
    delete m_content;
    m_content = buildContent(d);
    contentLayout()->addWidget(m_content);

    m_cc = str(d, QLatin1StringView("cc"));

    setSubjectKey(str(d, QLatin1StringView("userHash")));
    setWindowTitle(tr("Client Details: %1")
        .arg(str(d, QLatin1StringView("userName"))));

    fitToContent();
}

void ClientDetailDialog::applyDetails(const QCborMap& d)
{
    if (!m_content || str(d, QLatin1StringView("userHash")) != subjectKey()
        || str(d, QLatin1StringView("cc")) != m_cc) {
        setDetails(d);
        return;
    }

    // Build off-screen and copy the texts over when the rows line up
    const std::unique_ptr<QWidget> fresh(buildContent(d));
    const QList<ValueRow> now  = valueRows(m_content);
    const QList<ValueRow> next = valueRows(fresh.get());
    const bool sameRows = std::ranges::equal(now, next, [](const ValueRow& a, const ValueRow& b) {
        return a.caption == b.caption && (a.value == nullptr) == (b.value == nullptr);
    });
    if (!sameRows) {
        setDetails(d);
        return;
    }

    for (qsizetype i = 0; i < now.size(); ++i) {
        if (now[i].value && now[i].value->text() != next[i].value->text())
            now[i].value->setText(next[i].value->text());
    }
    setWindowTitle(tr("Client Details: %1")
        .arg(str(d, QLatin1StringView("userName"))));
}

// ── private helpers ────────────────────────────────────────────────────

QWidget* ClientDetailDialog::buildContent(const QCborMap& d)
{
    auto* page = new QWidget;
    auto* mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    DialogSizing::enableHeightForWidth(page);

    // ── General group ──────────────────────────────────────────────────
    {
        // One grid with a shared label column, paired like MFC's IDD_SOURCEDETAILWND:
        // ID beside the name, the hash full width, then client | connection
        auto* group = new QGroupBox(tr("General"));
        auto* grid  = new QGridLayout(group);
        grid->setColumnStretch(1, 3);   // software names are the longest values
        grid->setColumnStretch(3, 2);
        grid->setHorizontalSpacing(12);
        DialogSizing::enableHeightForWidth(group);

        // Labels and values as MFC CClientDetailPage (srchybrid/ClientDetailDialog.cpp:86-175)
        const QString unknown = QStringLiteral("?");
        const QString userName = str(d, QLatin1StringView("userName"));
        const bool lowID = d.value(QLatin1StringView("hasLowID")).toBool();
        addGridRow(grid, 0, 0, tr("Name"), userName.isEmpty() ? unknown : userName);
        addGridRow(grid, 0, 2, tr("ID"), lowID ? tr("Low ID") : tr("High ID"));
        addGridRow(grid, 1, 0, tr("Hash"), d.value(QLatin1StringView("hasValidHash")).toBool(true)
                                           ? str(d, QLatin1StringView("userHash")) : unknown, 3);

        addGridRow(grid, 2, 0, tr("Software"), str(d, QLatin1StringView("software")));

        // Country (MorphXT IP2Country): flag + long name
        const QString cc = str(d, QLatin1StringView("cc"));
        auto* country = new QWidget;
        auto* rowLayout = new QHBoxLayout(country);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        if (!cc.isEmpty()) {
            auto* flagLabel = new QLabel(country);
            flagLabel->setPixmap(CountryFlags::flag(cc).pixmap(QSize(18, 16)));
            rowLayout->addWidget(flagLabel);
        }
        rowLayout->addWidget(new QLabel(cc.isEmpty() ? unknown : CountryFlags::tooltip(cc), country), 1);
        grid->addWidget(gridCaption(tr("Country")), 3, 0);
        grid->addWidget(country, 3, 1, Qt::AlignTop);

        const bool kadConn = d.value(QLatin1StringView("kadConnected")).toBool();
        addGridRow(grid, 4, 0, tr("Kad"), kadConn ? tr("Connected") : tr("Disconnected"));

        const QString obfuscation = str(d, QLatin1StringView("obfuscation"));
        addGridRow(grid, 5, 0, tr("Obfuscation"),
                   obfuscation == QLatin1String("enabled") ? tr("Enabled")
                   : obfuscation == QLatin1String("supported") ? tr("Supported")
                   : tr("Not supported or disabled"));

        // Server. Prefer the string form: "serverIP" is 0 for an IPv6 server, which would
        // otherwise render the row as "\u2014" even though the address is known.
        const auto srvIP   = static_cast<uint32>(num(d, QLatin1StringView("serverIP")));
        const auto srvPort = static_cast<uint16>(num(d, QLatin1StringView("serverPort")));
        const QString srvName = str(d, QLatin1StringView("serverName"));
        const QString srvAddr = str(d, QLatin1StringView("serverAddr"));
        QString srvEndpoint;
        if (!srvAddr.isEmpty())
            srvEndpoint = Endpoint(Address::fromString(srvAddr), srvPort).toString();
        else if (srvIP != 0)
            srvEndpoint = ipstr(srvIP, srvPort);

        addGridRow(grid, 2, 2, tr("Server IP"), srvEndpoint.isEmpty() ? unknown : srvEndpoint);
        addGridRow(grid, 3, 2, tr("Server Name"),
                   srvEndpoint.isEmpty() || srvName.isEmpty() ? unknown : srvName);

        const bool creditsKnown = d.value(QLatin1StringView("creditsKnown")).toBool();
        const QString ident = str(d, QLatin1StringView("identification"));
        addGridRow(grid, 4, 2, tr("Identification"),
                   !creditsKnown ? unknown
                   : ident == QLatin1String("ok") ? tr("Successful")
                   : ident == QLatin1String("failed") ? tr("Invalid")
                   : tr("Not supported or disabled"));

        // Not in MFC: NAT-T for LowID peers — the route in use, else what the peer supports
        const QString natRoute = str(d, QLatin1StringView("natRoute"));
        QString natt;
        if (natRoute == QLatin1String("quic")) {
            natt = tr("Connected (%1)").arg(tr("QUIC"));
        } else if (natRoute == QLatin1String("ipv6")) {
            natt = tr("Connected (%1)").arg(tr("IPv6 fallback"));
        } else {
            QStringList ways;
            if (d.value(QLatin1StringView("supportsIPv6")).toBool())
                ways << tr("IPv6 fallback");
            if (d.value(QLatin1StringView("supportsNatTQuic")).toBool())
                ways << tr("QUIC");
            natt = ways.isEmpty() ? tr("Not supported")
                                  : tr("Supported (%1)").arg(ways.join(QStringLiteral(", ")));
        }
        addGridRow(grid, 5, 2, tr("NAT-T"), natt);

        mainLayout->addWidget(group);
    }

    // ── Transfer group ─────────────────────────────────────────────────
    {
        auto* group = new QGroupBox(tr("Transfer"));
        auto* form  = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        DialogSizing::enableHeightForWidth(group);

        // MFC words this group from the remote client's side: "downloaded" is what
        // they got from us, "uploaded" what they sent us.
        const QString none = QStringLiteral("-");
        const bool creditsKnown = d.value(QLatin1StringView("creditsKnown")).toBool();

        const QString upFileName = str(d, QLatin1StringView("uploadFileName"));
        addDetailRow(form, tr("Currently downloading"), upFileName.isEmpty() ? none : upFileName);
        const QString reqFileName = str(d, QLatin1StringView("reqFileName"));
        addDetailRow(form, tr("Requested to upload"), reqFileName.isEmpty() ? none : reqFileName);

        // Lifetime of the client object, not the current slot session (IDC_DDOWN / IDC_DDUP)
        addDetailRow(form, tr("Downloaded (this session)"),
                     formatByteSize(static_cast<uint64>(num(d, QLatin1StringView("transferredUp")))));
        addDetailRow(form, tr("Uploaded (this session)"),
                     formatByteSize(static_cast<uint64>(num(d, QLatin1StringView("transferredDown")))));

        // Averages over the slot time, ours up is theirs down (MorphXT; MFC shows the
        // current rate under the same labels, IDC_DAVDR / IDC_DAVUR)
        addDetailRow(form, tr("Average Downloadrate"),
                     formatByteRate(static_cast<uint64>(num(d, QLatin1StringView("avgUpDatarate")))));
        addDetailRow(form, tr("Average Upload rate"),
                     formatByteRate(static_cast<uint64>(num(d, QLatin1StringView("avgDownDatarate")))));

        // Credits (IDC_DDOWNTOTAL / IDC_DUPTOTAL)
        addDetailRow(form, tr("Downloaded total"), creditsKnown
            ? formatByteSize(static_cast<uint64>(num(d, QLatin1StringView("uploadedTotal")))) : QStringLiteral("?"));
        addDetailRow(form, tr("Uploaded total"), creditsKnown
            ? formatByteSize(static_cast<uint64>(num(d, QLatin1StringView("downloadedTotal")))) : QStringLiteral("?"));

        mainLayout->addWidget(group);
    }

    // ── Scores group ───────────────────────────────────────────────────
    {
        auto* group = new QGroupBox(tr("Scores"));
        auto* form  = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        DialogSizing::enableHeightForWidth(group);

        const QString unknown = QStringLiteral("?");
        const bool creditsKnown = d.value(QLatin1StringView("creditsKnown")).toBool();

        // Theirs, then in brackets what this peer grants us (MorphXT ownCredits)
        addDetailRow(form, tr("DL/UP Modifier"), creditsKnown
            ? QStringLiteral("%1  [%2]").arg(QString::number(dbl(d, QLatin1StringView("scoreRatio")), 'f', 1),
                                             QString::number(dbl(d, QLatin1StringView("myScoreRatio")), 'f', 1))
            : unknown);

        // MFC formats IDC_DRATING as %.1f from an integer score (ClientDetailDialog.cpp:159),
        // so it always renders one trailing zero. Match it.
        const auto rating = num(d, QLatin1StringView("rating"));
        addDetailRow(form, tr("Rating (total)"),
                     creditsKnown && !str(d, QLatin1StringView("userName")).isEmpty()
                         ? QString::number(static_cast<double>(rating), 'f', 1) : unknown);

        // A number, "(Friend Slot)", or "-" outside our upload path (ClientDetailDialog.cpp:164-170)
        QString score = QStringLiteral("-");
        if (creditsKnown && !d.value(QLatin1StringView("uploadIdle")).toBool()) {
            score = d.value(QLatin1StringView("friendSlot")).toBool()
                ? tr("(Friend Slot)") : QString::number(num(d, QLatin1StringView("score")));
        }
        addDetailRow(form, tr("Uploadqueue Score"), score);

        mainLayout->addWidget(group);
    }

    mainLayout->addStretch();
    return page;
}

} // namespace eMule
