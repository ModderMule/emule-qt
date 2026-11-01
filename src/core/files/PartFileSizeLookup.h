#pragma once

/// @file PartFileSizeLookup.h
/// @brief Finds the real size of a download whose hashset says its size is wrong.
///
/// A part-aligned size (n × PARTSIZE) whose last part hash isn't MD4("") belongs to a
/// bigger file: the real size lies in (size, size + PARTSIZE). Asks the eD2K server
/// (`ed2k::<hash>` search) and Kad (source records carry FT_FILESIZE) at once, each only
/// when its network is enabled. The first answer in range fixes the download; when both
/// fail it is paused and the reason logged.

#include "utils/Types.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

#include <deque>
#include <functional>
#include <optional>

namespace eMule {

class PartFileSizeLookup : public QObject {
    Q_OBJECT

public:
    /// Where a network stands for a lookup
    enum class Net : uint8 { Disabled, Waiting, Ready };

    /// One half's answer: the size (0 = none) or why there is none
    using Done = std::function<void(uint64 size, const QString& reason)>;
    /// Starts a half; gives back a cancel, or nullptr after calling @p done itself
    using Start = std::function<std::function<void()>(const QByteArray& hash, uint64 low,
                                                      uint64 high, Done done)>;

    /// Everything outside this class. The defaults are the real core; tests replace them.
    struct Backend {
        std::function<Net()> serverState;
        std::function<Net()> kadState;
        Start startServer;
        Start startKad;
        /// The download is still there and still waiting for a size
        std::function<bool(const QByteArray& hash)> wanted;
        std::function<void(const QByteArray& hash, uint64 size, const QString& from)> apply;
        std::function<void(const QByteArray& hash, const QString& serverReason,
                           const QString& kadReason)> fail;
    };

    explicit PartFileSizeLookup(QObject* parent = nullptr);
    PartFileSizeLookup(Backend backend, QObject* parent = nullptr);
    ~PartFileSizeLookup() override;

    /// Queue a lookup; the real size lies in (@p low, @p high). Repeats are ignored.
    void request(const QByteArray& hash, uint64 low, uint64 high);
    /// Drop a queued or running lookup (download removed or fixed)
    void cancel(const QByteArray& hash);
    [[nodiscard]] bool isPending(const QByteArray& hash) const;

    /// How long a half waits for its network to come up
    static constexpr int kNetWaitMs = 120'000;
    static constexpr int kTickMs = 2'000;

private:
    enum class HalfState : uint8 { Idle, Waiting, Running, Failed, Done };
    struct Half {
        HalfState state = HalfState::Idle;
        QString reason;
        std::function<void()> cancel;
    };
    struct Job {
        QByteArray hash;
        uint64 low = 0;
        uint64 high = 0;
        qint64 waitDeadline = 0;
    };

    static Backend realBackend();
    void startNext();
    void tick();
    void advance(Half& half, Net net, const Start& start, bool server);
    void halfDone(uint32 generation, bool server, uint64 size, const QString& reason);
    void finish(std::optional<std::pair<uint64, QString>> found);
    void stopHalves();

    Backend m_backend;
    std::deque<Job> m_queue;
    std::optional<Job> m_current;
    Half m_server;
    Half m_kad;
    uint32 m_generation = 0;   // tells answers of a finished lookup apart
    QTimer m_tick;
};

} // namespace eMule
