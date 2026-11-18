#pragma once

/// @file DaemonIndexerWebBackend.h
/// @brief The REST API's view of the indexer searches, as the daemon provides it.
///
/// Searches go to theIndexerSearchList, the one the IPC uses; grabs go through
/// grabIndexerResult(), the IPC grab's own code.

#include "webserver/IndexerWebBackend.h"

namespace eMule {

class DaemonIndexerWebBackend final : public IndexerWebBackend {
public:
    DaemonIndexerWebBackend() = default;

    [[nodiscard]] bool available() const override;
    StartResult start(const Query& query) override;
    [[nodiscard]] QCborArray searches() const override;
    [[nodiscard]] std::optional<QCborMap> search(quint32 id) const override;
    [[nodiscard]] std::optional<QCborArray> results(quint32 id) const override;
    bool stop(quint32 id) override;
    bool remove(quint32 id) override;
    void grab(quint32 searchId, const QString& resultId, const GrabOptions& options,
              std::function<void(const GrabResult&)> done) override;
};

} // namespace eMule
