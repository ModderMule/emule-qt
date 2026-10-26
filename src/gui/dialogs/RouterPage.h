#pragma once

/// @file RouterPage.h
/// @brief "Open Router" of the Connection page and the first-start wizard. Header-only,
///        like PortTest.h, so GUI tests need no extra source.

#include "net/RouterLocator.h"
#include "utils/AsyncMessageBox.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QWidget>

namespace eMule::RouterPage {

/// Open the LAN router's web page in the default browser. Warns first when a VPN
/// carries the traffic, since router forwards then do not reach us. The lookup is
/// local: the browser runs here, and a core in Docker would only see its bridge.
inline void open(QWidget* parent)
{
    const auto tr = [](const char* text) {
        return QCoreApplication::translate("eMule::RouterPage", text);
    };

    const RouterInfo info = locateRouter();
    const QString title = tr("Open Router");
    const QString vpnNote = info.vpnInterface.isEmpty()
        ? QString()
        : tr("A VPN seems to be active (%1). Incoming connections then arrive through the VPN, "
             "so port forwarding on your router does not help. Forward the ports at your VPN "
             "provider instead, if it offers port forwarding.").arg(info.vpnInterface);

    if (info.router.isNull()) {
        QString text = tr("Could not find your router's address.\n\nLook up the \"default gateway\" "
                          "in this computer's network settings and open that address in a web browser.");
        if (!vpnNote.isEmpty())
            text += QStringLiteral("\n\n") + vpnNote;
        AsyncMessageBox::warning(parent, title, text);
        return;
    }

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(info.router.toString());
    url.setPath(QStringLiteral("/"));

    const auto openUrl = [parent, title, url, tr] {
        if (!QDesktopServices::openUrl(url))
            AsyncMessageBox::warning(parent, title,
                                     tr("Could not open %1 in the default browser.").arg(url.toString()));
    };

    if (vpnNote.isEmpty()) {
        openUrl();
        return;
    }

    auto* box = new QMessageBox(QMessageBox::Warning, title, vpnNote, QMessageBox::Cancel, parent);
    box->setInformativeText(tr("Your router is at %1.").arg(info.router.toString()));
    auto* openBtn = box->addButton(tr("Open Router Page"), QMessageBox::AcceptRole);
    box->setDefaultButton(openBtn);
    box->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(box, &QMessageBox::buttonClicked, box, [openBtn, openUrl](QAbstractButton* clicked) {
        if (clicked == openBtn)
            openUrl();
    });
    box->open();
}

} // namespace eMule::RouterPage
