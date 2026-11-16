#pragma once

/// @file VpnPromo.h
/// @brief AirVPN referral link: a VPN with port forwarding, so High ID + anonymity.
///        Shown near the ports (Connection options, first-start wizard, port test,
///        Network Info, My Info, Low ID box). Header-only. Build with NO_PROMOTIONS
///        to drop every link.

#include <QCoreApplication>
#include <QLabel>
#include <QMessageBox>
#include <QString>

namespace eMule::VpnPromo {

#ifdef NO_PROMOTIONS
inline constexpr bool kEnabled = false;
#else
inline constexpr bool kEnabled = true;
#endif

inline constexpr auto kUrl = "https://airvpn.org/?referred_by=859544";

/// What the link promises: forwarding while ports are closed (or unknown), else anonymity only.
enum class Pitch {
    HighId,
    Anonymity,
};

/// A class only for tr() (context "VpnPromo").
class Text {
    Q_DECLARE_TR_FUNCTIONS(VpnPromo)

public:
    /// Rich text with the link; empty with NO_PROMOTIONS.
    [[nodiscard]] static QString html(Pitch pitch)
    {
        if constexpr (!kEnabled)
            return {};
        const QString link = QStringLiteral("<a href=\"%1\">AirVPN</a>").arg(QLatin1StringView(kUrl));
        return pitch == Pitch::HighId
                   ? tr("No open ports? %1 forwards a port through the VPN: High ID and anonymity in one.").arg(link)
                   : tr("Hide your IP from peers: %1 keeps a port forwarded, so you stay reachable.").arg(link);
    }

    /// Plain text ending in the URL, for panes that linkify (Server Info log).
    [[nodiscard]] static QString plain(Pitch pitch)
    {
        if constexpr (!kEnabled)
            return {};
        const QString name = QStringLiteral("AirVPN");
        const QString text = pitch == Pitch::HighId
            ? tr("No open ports? %1 forwards a port through the VPN: High ID and anonymity in one.").arg(name)
            : tr("Hide your IP from peers: %1 keeps a port forwarded, so you stay reachable.").arg(name);
        return text + u' ' + QLatin1StringView(kUrl);
    }

    /// Short status bar link.
    [[nodiscard]] static QString statusHtml(Pitch pitch)
    {
        if constexpr (!kEnabled)
            return {};
        const QString link = QStringLiteral("<a href=\"%1\">AirVPN</a>").arg(QLatin1StringView(kUrl));
        return pitch == Pitch::HighId ? tr("High ID via %1").arg(link) : tr("Anonymous via %1").arg(link);
    }

    [[nodiscard]] static QString toolTip() { return tr("Referral link, supports eMule Qt"); }
};

/// A wrapped link label, or nullptr with NO_PROMOTIONS.
[[nodiscard]] inline QLabel* label(Pitch pitch, QWidget* parent)
{
    if constexpr (!kEnabled)
        return nullptr;
    auto* l = new QLabel(Text::html(pitch), parent);
    l->setObjectName(QStringLiteral("vpnPromo"));
    l->setTextFormat(Qt::RichText);
    l->setOpenExternalLinks(true);
    l->setWordWrap(true);
    l->setToolTip(Text::toolTip());
    return l;
}

/// Adds the link under a message box's text. Qt box only: a native alert shows no links.
inline void addTo(QMessageBox& box, Pitch pitch)
{
    if constexpr (!kEnabled)
        return;
    box.setOption(QMessageBox::Option::DontUseNativeDialog);
    box.setInformativeText(Text::html(pitch));
    if (auto* info = box.findChild<QLabel*>(QStringLiteral("qt_msgbox_informativelabel"))) {
        info->setTextFormat(Qt::RichText);
        info->setOpenExternalLinks(true);
        info->setTextInteractionFlags(Qt::TextBrowserInteraction);
        info->setToolTip(Text::toolTip());
    }
}

} // namespace eMule::VpnPromo
