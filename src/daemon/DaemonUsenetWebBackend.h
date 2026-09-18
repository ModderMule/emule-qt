#pragma once

/// @file DaemonUsenetWebBackend.h
/// @brief The web server's view of the Usenet queue, as the daemon provides it.
///
/// A thin adapter over UsenetBridge: WebServer lives in eMule::Core and may not
/// name the Usenet module, so DaemonApp owns one of these and hands the web
/// server the interface.

#include "webserver/UsenetWebBackend.h"

#include <QObject>

namespace eMule {

class DaemonUsenetWebBackend final : public UsenetWebBackend {
public:
    DaemonUsenetWebBackend() = default;

    [[nodiscard]] bool available() const override;
    [[nodiscard]] QCborArray queue() const override;
    [[nodiscard]] bool contains(const QString& id) const override;
    [[nodiscard]] QCborMap details(const QString& id) const override;
    [[nodiscard]] QCborMap archiveEntries(const QString& id, int fileIndex) override;
    [[nodiscard]] QCborMap downloadSplit() const override;
    [[nodiscard]] bool categoryExists(int category) const override;
    void setEnginePaused(bool paused) override;
    QString setFilesSkipped(const QString& id, const QList<int>& files, bool skipped) override;

    bool pause(const QString& id) override;
    bool resume(const QString& id) override;
    bool remove(const QString& id, bool deleteFiles) override;
    bool setPriority(const QString& id, int priority) override;
    bool setCategory(const QString& id, int category) override;
    bool setPassword(const QString& id, const QString& password) override;
    QString recheck(const QString& id) override;
    int applyCategoryAction(int category, UsenetWebCategoryAction action) override;

    UsenetWebAddResult addNzb(const QByteArray& data, const QString& name,
                              const UsenetWebAddOptions& options) override;
    void addNzbUrl(const QString& url, const UsenetWebAddOptions& options,
                   std::function<void(const UsenetWebAddResult&)> done) override;

private:
    /// Same bound as IpcClientHandler::kMaxNzbUrlFetches, for the same reason: a
    /// pasted list must not turn the daemon into a port scanner.
    static constexpr int kMaxNzbUrlFetches = 4;

    /// Lifetime anchor for fetches in flight; they are dropped with the backend.
    QObject m_fetchContext;
    int m_nzbUrlFetchesInFlight = 0;
};

} // namespace eMule
