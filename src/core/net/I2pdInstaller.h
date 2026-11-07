#pragma once

/// @file I2pdInstaller.h
/// @brief Downloads the pinned I2P router (emuleqt-i2pd) into <config>/I2P/bin.
///
/// Not shipped with the app: fetched the first time the own router is wanted (Options or
/// YAML), and again when a new app version pins a newer release (I2pdRelease.h). The archive
/// is SHA-256 checked, unpacked in-process into <dir>.tmp, then swapped in; `.release` in the
/// folder names the installed pin.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QThread;

namespace eMule {

class HttpStreamDownload;

class I2pdInstaller : public QObject {
    Q_OBJECT

public:
    /// cap on the archive (the release is ~10 MB)
    static constexpr qint64 kMaxArchive = 64ll * 1024 * 1024;

    struct Asset {
        QString name;     ///< e.g. i2pd-macos-universal.tar.gz
        QString sha256;   ///< lowercase hex
        QUrl url;
        QString release;  ///< "<tag> <sha256>", the .release stamp
    };

    explicit I2pdInstaller(QObject* parent = nullptr);
    ~I2pdInstaller() override;

    /// The pinned asset of this OS/CPU; empty name when there is none.
    [[nodiscard]] static Asset pinnedAsset();
    /// A release exists for this OS/CPU.
    [[nodiscard]] static bool supported() { return !pinnedAsset().name.isEmpty(); }
    /// The program's file name: emuleqt-i2pd[.exe]
    [[nodiscard]] static QString binaryName();

    /// The install folder (<config>/I2P/bin). Not while installing.
    void setDir(const QString& dir);
    [[nodiscard]] QString dir() const { return m_dir; }
    /// The installed program; empty when none.
    [[nodiscard]] QString binary() const;
    /// The installed one is the pinned release.
    [[nodiscard]] bool current() const;

    /// Fetches the pinned asset. False when one runs, nothing is pinned, or no folder.
    bool install();
    /// Same for any asset (tests: a file:// URL).
    bool install(const Asset& asset);
    /// Stops; finished() is not emitted. The old install stays.
    void abort();

    [[nodiscard]] bool running() const;
    /// 0..100 while running
    [[nodiscard]] int progress() const { return m_progress; }
    /// Why the last install failed; English.
    [[nodiscard]] QString error() const { return m_error; }

signals:
    void progressChanged(int percent);
    void finished(bool ok);

private:
    void onDownloaded(bool ok);
    void unpack(const QString& archive);
    void unpackDone(const QString& archive, const QString& error);
    void done(bool ok, const QString& error = {});
    [[nodiscard]] QString tmpDir() const { return m_dir + QStringLiteral(".tmp"); }

    QString m_dir;
    Asset m_asset;
    std::unique_ptr<QNetworkAccessManager> m_nam;
    std::unique_ptr<HttpStreamDownload> m_dl;
    QPointer<QThread> m_unpackThread;
    int m_progress = 0;
    QString m_error;
};

} // namespace eMule
