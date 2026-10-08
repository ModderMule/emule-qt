#include "pch.h"
/// @file ClientDetailDialog.cpp
/// @brief Client detail dialog implementation.

#include "ClientDetailDialog.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

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

} // anonymous namespace

// ── constructor ────────────────────────────────────────────────────────

ClientDetailDialog::ClientDetailDialog(const QCborMap& d, QWidget* parent)
    : DetailDialog(parent, ContentScroll::On)
{
    setAttribute(Qt::WA_DeleteOnClose);

    // Height is the content's to decide: the row count varies with the client (a friend
    // slot adds a row) and so does the wrapped height of the file-name values.
    setDesignedSize(QSize(580, 0), QSize(620, 0));

    ClientDetailDialog::setDetails(d);
}

// ── re-target ──────────────────────────────────────────────────────────

void ClientDetailDialog::setDetails(const QCborMap& d)
{
    delete m_content;
    m_content = buildContent(d);
    contentLayout()->addWidget(m_content);

    setSubjectKey(str(d, QLatin1StringView("userHash")));
    setWindowTitle(tr("Client Details: %1")
        .arg(str(d, QLatin1StringView("userName"))));

    fitToContent();
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
        auto* group = new QGroupBox(tr("General"));
        auto* form  = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        DialogSizing::enableHeightForWidth(group);

        // Labels and values as MFC CClientDetailPage (srchybrid/ClientDetailDialog.cpp:86-175)
        const QString unknown = QStringLiteral("?");
        const QString userName = str(d, QLatin1StringView("userName"));
        addDetailRow(form, tr("Name"), userName.isEmpty() ? unknown : userName);
        addDetailRow(form, tr("Hash"), d.value(QLatin1StringView("hasValidHash")).toBool(true)
                                       ? str(d, QLatin1StringView("userHash")) : unknown);

        // ID (Low/High)
        const bool lowID = d.value(QLatin1StringView("hasLowID")).toBool();
        addDetailRow(form, tr("ID"), lowID ? tr("Low ID") : tr("High ID"));

        addDetailRow(form, tr("Software"), str(d, QLatin1StringView("software")));

        // Country (MorphXT IP2Country): flag + long name
        if (const QString cc = str(d, QLatin1StringView("cc")); !cc.isEmpty()) {
            auto* row = new QWidget;
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            auto* flagLabel = new QLabel(row);
            flagLabel->setPixmap(CountryFlags::flag(cc).pixmap(QSize(18, 16)));
            rowLayout->addWidget(flagLabel);
            rowLayout->addWidget(new QLabel(CountryFlags::tooltip(cc), row), 1);
            form->addRow(QStringLiteral("<b>%1:</b>").arg(tr("Country")), row);
        }

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

        addDetailRow(form, tr("Server IP"), srvEndpoint.isEmpty() ? unknown : srvEndpoint);
        addDetailRow(form, tr("Server Name"),
                     srvEndpoint.isEmpty() || srvName.isEmpty() ? unknown : srvName);

        const bool creditsKnown = d.value(QLatin1StringView("creditsKnown")).toBool();
        const QString ident = str(d, QLatin1StringView("identification"));
        addDetailRow(form, tr("Identification"),
                     !creditsKnown ? unknown
                     : ident == QLatin1String("ok") ? tr("Successful")
                     : ident == QLatin1String("failed") ? tr("Invalid")
                     : tr("Not supported or disabled"));
        const QString obfuscation = str(d, QLatin1StringView("obfuscation"));
        addDetailRow(form, tr("Obfuscation"),
                     obfuscation == QLatin1String("enabled") ? tr("Enabled")
                     : obfuscation == QLatin1String("supported") ? tr("Supported")
                     : tr("Not supported or disabled"));

        const bool kadConn = d.value(QLatin1StringView("kadConnected")).toBool();
        addDetailRow(form, tr("Kad"), kadConn ? tr("Connected") : tr("Disconnected"));

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

        // The live rates: ours up is theirs down (IDC_DAVDR / IDC_DAVUR)
        addDetailRow(form, tr("Average Downloadrate"),
                     formatByteRate(static_cast<uint64>(num(d, QLatin1StringView("upDatarate")))));
        addDetailRow(form, tr("Average Upload rate"),
                     formatByteRate(static_cast<uint64>(num(d, QLatin1StringView("datarate")))));

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

        const double scoreRatio = dbl(d, QLatin1StringView("scoreRatio"));
        addDetailRow(form, tr("DL/UP Modifier"),
                     creditsKnown ? QString::number(scoreRatio, 'f', 1) : unknown);

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
