#include "pch.h"
/// @file SingleInstance.cpp
/// @brief One GUI per install dir and profile when the GUI starts its own core.

#include "app/SingleInstance.h"

#include "utils/Log.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace eMule {

namespace {

constexpr char kAck = 1;

/// Same dir however it was spelled: symlinks, trailing slash, case on Windows.
QString canonicalDir(const QString& dir)
{
    const QFileInfo info(dir);
    QString path = info.canonicalFilePath();
    if (path.isEmpty())
        path = QDir::cleanPath(info.absoluteFilePath());
#ifdef Q_OS_WIN
    path = path.toLower();
#endif
    return path;
}

QString makeServerName(const QString& appDir, const QString& configDir)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(canonicalDir(appDir).toUtf8());
    hash.addData(QByteArrayView("\n"));
    hash.addData(canonicalDir(configDir).toUtf8());
    // Short: a Unix socket path is limited to ~104 bytes and lives in the temp dir
    return QStringLiteral("emuleqt-gui-")
           + QString::fromLatin1(hash.result().toHex().left(16));
}

} // namespace

SingleInstance::SingleInstance(const QString& appDir, const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_serverName(makeServerName(appDir, configDir))
    , m_lock(QDir::tempPath() + QLatin1Char('/') + m_serverName + QStringLiteral(".lock"))
{
    // Never stale while the owner lives; QLockFile checks the pid of a dead one
    m_lock.setStaleLockTime(0);
}

SingleInstance::~SingleInstance()
{
    if (m_server)
        m_server->close();
}

bool SingleInstance::tryBecomePrimary()
{
    // The lock decides, not the socket: a crashed GUI leaves a socket file behind on
    // Unix, and two starts at once would both find no server to talk to.
    if (!m_lock.tryLock(0))
        return false;

    m_server = std::make_unique<QLocalServer>();
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(m_serverName);   // leftover of a crash
    if (!m_server->listen(m_serverName)) {
        // Still the primary: the lock is ours. Later starts just cannot reach us.
        logWarning(QStringLiteral("Single instance: cannot listen on %1: %2")
                       .arg(m_serverName, m_server->errorString()));
        return true;
    }
    connect(m_server.get(), &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
    return true;
}

bool SingleInstance::forwardToPrimary(const QStringList& args, int timeoutMs) const
{
#ifdef Q_OS_WIN
    // We were started by the user and may take the foreground; let the primary
    // have it, or Windows only flashes its taskbar button.
    ::AllowSetForegroundWindow(ASFW_ANY);
#endif

    QByteArray payload;
    {
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << args;
    }

    // The primary takes the lock a moment before it listens: retry briefly
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QLocalSocket socket;
        socket.connectToServer(m_serverName);
        if (!socket.waitForConnected(500)) {
            QThread::msleep(100);
            continue;
        }
        QDataStream out(&socket);
        out.setVersion(QDataStream::Qt_6_0);
        out << payload;
        const int left = std::max(100, timeoutMs - static_cast<int>(timer.elapsed()));
        if (!socket.waitForBytesWritten(left))
            return false;
        // The ack says the primary read it, so the links are not lost with us
        while (socket.bytesAvailable() < 1) {
            if (!socket.waitForReadyRead(std::max(100, timeoutMs - static_cast<int>(timer.elapsed()))))
                return false;
        }
        char ack = 0;
        return socket.getChar(&ack) && ack == kAck;
    }
    return false;
}

QStringList SingleInstance::absoluteArgs(const QStringList& args)
{
    QStringList result;
    result.reserve(args.size());
    for (const QString& arg : args) {
        // A relative .nzb path means nothing in the other process's working dir
        const QFileInfo info(arg);
        if (!arg.contains(QStringLiteral("://")) && info.isRelative() && info.exists())
            result << info.absoluteFilePath();
        else
            result << arg;
    }
    return result;
}

void SingleInstance::onNewConnection()
{
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            QDataStream in(socket);
            in.setVersion(QDataStream::Qt_6_0);
            in.startTransaction();
            QByteArray payload;
            in >> payload;
            if (!in.commitTransaction())
                return;   // more to come

            QStringList args;
            QDataStream inner(payload);
            inner.setVersion(QDataStream::Qt_6_0);
            inner >> args;
            socket->putChar(kAck);
            socket->flush();
            socket->disconnectFromServer();
            logInfo(QStringLiteral("Another start of this profile handed over %1 argument(s)")
                        .arg(args.size()));
            emit activationRequested(args);
        });
    }
}

} // namespace eMule
