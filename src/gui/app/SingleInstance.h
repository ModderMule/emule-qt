#pragma once

/// @file SingleInstance.h
/// @brief One GUI per install dir and profile when the GUI starts its own core.
///
/// A second default start (desktop icon clicked twice, an ed2k link opened on
/// Windows/Linux) used to open a second window on the same core (issue #29).
/// Now it hands its links and .nzb files to the running GUI, which comes to the
/// front, and exits.
///
/// The key is the executable's dir plus the config dir: another install or another
/// --config profile is a separate instance on purpose (one core per interface/port).
/// --new-instance skips the guard altogether.

#include <QLockFile>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QLocalServer;

namespace eMule {

class SingleInstance : public QObject {
    Q_OBJECT

public:
    /// @param appDir dir of the GUI executable
    /// @param configDir the profile's config dir
    SingleInstance(const QString& appDir, const QString& configDir, QObject* parent = nullptr);
    ~SingleInstance() override;

    /// Try to become the instance for this key. False when another one runs.
    [[nodiscard]] bool tryBecomePrimary();

    /// Hand @p args to the running instance. False when it did not answer.
    [[nodiscard]] bool forwardToPrimary(const QStringList& args, int timeoutMs = 3000) const;

    /// Positional args made independent of this process's working dir.
    [[nodiscard]] static QStringList absoluteArgs(const QStringList& args);

    /// Local socket name, also the lock file's base name.
    [[nodiscard]] const QString& serverName() const { return m_serverName; }

signals:
    /// Another start forwarded its positional args (may be empty: just come to front).
    void activationRequested(const QStringList& args);

private:
    void onNewConnection();

    QString m_serverName;
    QLockFile m_lock;
    std::unique_ptr<QLocalServer> m_server;
};

} // namespace eMule
