#include "app/CoreAttachment.h"

#include <QHostAddress>
#include <QPainter>
#include <QPixmap>

namespace eMule {

CoreAttachment& CoreAttachment::instance()
{
    static CoreAttachment link;
    return link;
}

void CoreAttachment::setTarget(const QString& host, uint16_t port, bool remote)
{
    const QString h = host.isEmpty() ? QStringLiteral("127.0.0.1") : host;
    if (h == m_host && port == m_port && remote == m_remote)
        return;
    m_host = h;
    m_port = port;
    m_remote = remote;
    emit changed();
}

void CoreAttachment::noteLaunched()
{
    if (m_launched)
        return;
    m_launched = true;
    emit changed();
}

void CoreAttachment::setStopsOnExit(bool stop)
{
    if (!canToggle())
        return;
    if (stop == !m_userDetached)
        return;
    m_userDetached = !stop;
    emit changed();
}

QString CoreAttachment::address() const
{
    if (m_host.isEmpty())
        return {};
    const bool v6 = QHostAddress(m_host).protocol() == QAbstractSocket::IPv6Protocol;
    return (v6 ? u'[' + m_host + u']' : m_host) + u':' + QString::number(m_port);
}

QString CoreAttachment::locationText() const
{
    if (m_remote)
        return tr("Remote core at %1").arg(address());
    return m_launched ? tr("Local core at %1, started by this GUI").arg(address())
                      : tr("Local core at %1").arg(address());
}

QString CoreAttachment::exitText() const
{
    return stopsOnExit() ? tr("Attached: stops when the GUI closes")
                         : tr("Detached: keeps running when the GUI closes");
}

QString CoreAttachment::toolTip() const
{
    QString tip = locationText() + u'\n' + exitText();
    if (canToggle())
        tip += u'\n' + (stopsOnExit() ? tr("Double-click to detach") : tr("Double-click to attach"));
    return tip;
}

QIcon CoreAttachment::icon(const QColor& color) const
{
    QIcon result;
    for (const int size : {16, 32}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 16.0, size / 16.0);
        p.translate(8, 8);
        p.rotate(-45);
        p.setPen(QPen(color, 1.6));
        p.setBrush(Qt::NoBrush);
        // Two chain links; apart with a gap when detached
        const qreal shift = stopsOnExit() ? 2.4 : 4.2;
        p.drawRoundedRect(QRectF(-shift - 3.5, -2.2, 7, 4.4), 2.2, 2.2);
        p.drawRoundedRect(QRectF(shift - 3.5, -2.2, 7, 4.4), 2.2, 2.2);
        if (!stopsOnExit()) {
            p.setPen(QPen(color, 1.0));
            p.drawLine(QPointF(-0.6, -5.0), QPointF(-0.6, -3.4));
            p.drawLine(QPointF(0.6, 3.4), QPointF(0.6, 5.0));
        }
        result.addPixmap(pm);
    }
    return result;
}

} // namespace eMule
