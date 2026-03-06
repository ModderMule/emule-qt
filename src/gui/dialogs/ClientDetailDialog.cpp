#include "pch.h"
/// @file ClientDetailDialog.cpp
/// @brief Client detail dialog implementation.

#include "ClientDetailDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

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

/// Create a selectable, word-wrapping label.
QLabel* valueLabel(const QString& text)
{
    auto* lbl = new QLabel(text);
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lbl->setWordWrap(true);
    lbl->setMinimumWidth(240);
    lbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    return lbl;
}

/// Add a bold-label : value row to a form layout.
void addRow(QFormLayout* form, const QString& label, const QString& value)
{
    form->addRow(QStringLiteral("<b>%1:</b>").arg(label), valueLabel(value));
}

} // anonymous namespace

// ── constructor ────────────────────────────────────────────────────────

ClientDetailDialog::ClientDetailDialog(const QCborMap& d, QWidget* parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Client Details: %1")
        .arg(str(d, QLatin1StringView("userName"))));
    setMinimumSize(580, 580);
    resize(620, 620);

    auto* mainLayout = new QVBoxLayout(this);

    // ── General group ──────────────────────────────────────────────────
    {
        auto* group = new QGroupBox(tr("General"));
        auto* form  = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        addRow(form, tr("User Name"), str(d, QLatin1StringView("userName")));
        addRow(form, tr("User Hash"), str(d, QLatin1StringView("userHash")));

        // ID (Low/High)
        const bool lowID = d.value(QLatin1StringView("hasLowID")).toBool();
        addRow(form, tr("ID"), lowID ? tr("Low ID") : tr("High ID"));

        addRow(form, tr("Client Software"), str(d, QLatin1StringView("software")));

        // Server
        const auto srvIP   = static_cast<uint32>(num(d, QLatin1StringView("serverIP")));
        const auto srvPort = static_cast<uint16>(num(d, QLatin1StringView("serverPort")));
        const QString srvName = str(d, QLatin1StringView("serverName"));
        if (srvIP != 0) {
            const QString srvStr = srvName.isEmpty()
                ? ipstr(srvIP, srvPort)
                : QStringLiteral("%1 - %2").arg(srvName, ipstr(srvIP, srvPort));
            addRow(form, tr("Server"), srvStr);
        } else {
            addRow(form, tr("Server"), QStringLiteral("\u2014"));
        }

        addRow(form, tr("Identification"), str(d, QLatin1StringView("identification")));
        addRow(form, tr("Obfuscation"), str(d, QLatin1StringView("obfuscation")));

        const bool kadConn = d.value(QLatin1StringView("kadConnected")).toBool();
        addRow(form, tr("Kad"), kadConn ? tr("Connected") : tr("Not connected"));

        mainLayout->addWidget(group);
    }

    // ── Transfer group ─────────────────────────────────────────────────
    {
        auto* group = new QGroupBox(tr("Transfer"));
        auto* form  = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        // Currently downloading from us (what we upload to them)
        const QString upFileName = str(d, QLatin1StringView("uploadFileName"));
        addRow(form, tr("Currently Downloading"),
               upFileName.isEmpty() ? QStringLiteral("\u2014") : upFileName);

        // Currently uploading to us (what they upload to us)
        const QString reqFileName = str(d, QLatin1StringView("reqFileName"));
        addRow(form, tr("Currently Uploading"),
               reqFileName.isEmpty() ? QStringLiteral("\u2014") : reqFileName);

        // Session transferred
        const auto sessionDown = static_cast<uint64>(num(d, QLatin1StringView("sessionDown")));
        const auto sessionUp   = static_cast<uint64>(num(d, QLatin1StringView("sessionUp")));
        addRow(form, tr("Downloaded (Session)"), formatByteSize(sessionDown));
        addRow(form, tr("Uploaded (Session)"),   formatByteSize(sessionUp));

        // Data rate
        const auto datarate = static_cast<uint64>(num(d, QLatin1StringView("datarate")));
        if (datarate > 0)
            addRow(form, tr("Download Rate"), formatByteSize(datarate) + QStringLiteral("/s"));
        else
            addRow(form, tr("Download Rate"), QStringLiteral("\u2014"));

        // Total credits
        const auto downTotal = static_cast<uint64>(num(d, QLatin1StringView("downloadedTotal")));
        const auto upTotal   = static_cast<uint64>(num(d, QLatin1StringView("uploadedTotal")));
        addRow(form, tr("Downloaded (Total)"), formatByteSize(downTotal));
        addRow(form, tr("Uploaded (Total)"),   formatByteSize(upTotal));

        mainLayout->addWidget(group);
    }

    // ── Scores group ───────────────────────────────────────────────────
    {
        auto* group = new QGroupBox(tr("Scores"));
        auto* form  = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        const double scoreRatio = dbl(d, QLatin1StringView("scoreRatio"));
        addRow(form, tr("DL/UP Modifier"), QString::number(scoreRatio, 'f', 1));

        const auto rating = num(d, QLatin1StringView("rating"));
        addRow(form, tr("Rating (Total)"), QString::number(rating));

        const auto score = num(d, QLatin1StringView("score"));
        addRow(form, tr("Upload Queue Score"), QString::number(score));

        const bool friendSlot = d.value(QLatin1StringView("friendSlot")).toBool();
        if (friendSlot)
            addRow(form, tr("Friend Slot"), tr("Yes"));

        mainLayout->addWidget(group);
    }

    mainLayout->addStretch();

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    mainLayout->addWidget(btnBox);
}

} // namespace eMule
