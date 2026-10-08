#pragma once

/// @file PortChangeNotice.h
/// @brief Tells the user what the core did with a changed listen port.
///
/// Reads the SetPreferences reply. The core rebinds at once when nothing is
/// connected; otherwise the old ports stay until a restart. Shared by the first
/// start wizard and the Options dialog.

#include "IpcMessage.h"
#include "portmap/PortMapTypes.h"
#include "utils/StatusBarNotifier.h"

#include <QCborMap>
#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QString>

#include <functional>

namespace eMule {

struct PortChangeSummary {
    QString text;           ///< empty: nothing to say
    bool needsAttention = false;   ///< message box rather than a status bar line
    bool offerRestart = false;     ///< a restart would apply the change
};

/// @param reply payload of the SetPreferences result: {ports, tcpPort, udpPort}
[[nodiscard]] inline PortChangeSummary portChangeSummary(const QCborMap& reply)
{
    const auto result = static_cast<PortApplyResult>(
        reply.value(QLatin1StringView("ports")).toInteger());
    const auto tcp = reply.value(QLatin1StringView("tcpPort")).toInteger();
    const auto udp = reply.value(QLatin1StringView("udpPort")).toInteger();

    // Literal translate() calls: lupdate cannot see a string passed through a helper
    using App = QCoreApplication;
    switch (result) {
    case PortApplyResult::Applied:
        return {App::translate("PortChange", "Now listening on TCP port %1 and UDP port %2.")
                    .arg(tcp).arg(udp), false};
    case PortApplyResult::RestartRequired:
        return {App::translate("PortChange",
                    "The new ports take effect after restarting eMule, because the core is "
                    "connected to a network or to other clients.\n\n"
                    "Until then it keeps listening on TCP port %1 and UDP port %2.")
                    .arg(tcp).arg(udp), true, true};
    case PortApplyResult::BindFailed:
        return {App::translate("PortChange",
                    "The new port could not be opened. It may be in use by another program.\n\n"
                    "The core keeps listening on TCP port %1 and UDP port %2.")
                    .arg(tcp).arg(udp), true};
    case PortApplyResult::Unchanged:
        break;
    }
    return {};
}

/// Safe from an IPC callback: the box is shown, not exec()'d.
/// @param restart restarts the core; without it the notice only informs
inline void showPortChangeResult(QWidget* parent, const Ipc::IpcMessage& reply,
                                 std::function<void()> restart = {})
{
    if (!reply.isValid())
        return;   // connection dropped
    const PortChangeSummary summary = portChangeSummary(reply.fieldMap(1));
    if (summary.text.isEmpty())
        return;
    if (!summary.needsAttention) {
        StatusBarNotifier::post(summary.text);
        return;
    }
    auto* box = new QMessageBox(QMessageBox::Information,
                                QCoreApplication::translate("PortChange", "Ports"),
                                summary.text, QMessageBox::Ok, parent);
    if (summary.offerRestart && restart) {
        box->setStandardButtons(QMessageBox::NoButton);
        auto* now = box->addButton(QCoreApplication::translate("PortChange", "Restart Now"),
                                   QMessageBox::AcceptRole);
        auto* later = box->addButton(QCoreApplication::translate("PortChange", "Later"),
                                     QMessageBox::RejectRole);
        box->setDefaultButton(later);
        box->setEscapeButton(later);
        QObject::connect(now, &QAbstractButton::clicked, box, std::move(restart));
    }
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(true);
    box->show();
}

} // namespace eMule
