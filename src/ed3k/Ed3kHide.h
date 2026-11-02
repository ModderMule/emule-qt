#pragma once

/// @file Ed3kHide.h
/// @brief Hiding (hops.md §1 "One machine, two nodes"): the anonymous node of the session,
/// started and stopped live from the prefs, and which key each file is held under.
///
/// - Global switch: while hiding, new ed3k searches, downloads and new shares go under the
///   anonymous key; files held before keep their key (keys.json, Config/ed3k). A file is
///   never under both keys (packmule refuses it) and moves only after the user was asked (§1).
/// - One hop count for every connection and lookup type (HideCfg::uniform); gates at the
///   core default. packmule takes per-type counts too (HideCfg), unused here.
/// - The seed is Config/ed3k/anon.key (owner-only, a secret, never logged).
/// - Files of the anonymous key are not served while hiding is off: nothing of them goes
///   under the node key behind the user's back.
/// - HTTP, hoster, eD2K and LAN never go anonymous (ffi.md MUST): only the joins that say
///   so (Ed3kJoinBase::anonCapable) use the anonymous key.
/// - ToDo: topics (sets) and live under the anonymous key: the FFI has no anonymous routing
///   for them, so they stay on the node key; rooms (ed3k_room_anon_*) and paid
///   (paidAnon*) under the anonymous key are not switched by this either.

#include "Ed3kFfi.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <map>
#include <optional>

namespace eMule::ed3k {

class Ed3kSession;

/// The prefs of hiding (ed3kHide, ed3kHops, ed3kHopping, ed3kHopShare).
struct Ed3kHideSettings {
    bool on = false;
    int hops = 1;             ///< 1 to 3, every use (HideCfg::uniform)
    bool hopping = true;      ///< forward for others while hiding (§1 "Recommended", §5)
    int sharePercent = 25;    ///< of the upload limit for others' paths (§5)

    friend bool operator==(const Ed3kHideSettings&, const Ed3kHideSettings&) = default;
};

/// What GetEd3kStatus "hide" shows.
struct Ed3kHideStatus {
    bool on = false;            ///< the pref
    bool running = false;       ///< the anonymous node runs
    QString nodeId;             ///< the anonymous node ID, 8 hex (public)
    int hops = 0;
    int paths = 0;              ///< paths this machine owns
    int built = 0;              ///< of them built
    bool waitingForHops = false;   ///< hiding, no path built yet (20 hops needed first)
    int anonFiles = 0;          ///< files recorded under the anonymous key
};

class Ed3kHide : public QObject {
    Q_OBJECT

public:
    static inline const QString kSeedFile = QStringLiteral("anon.key");
    static inline const QString kKeysFile = QStringLiteral("keys.json");

    explicit Ed3kHide(Ed3kSession& session, QObject* parent = nullptr);
    ~Ed3kHide() override;

    /// The prefs; applied at once while the node runs (no restart).
    void setSettings(const Ed3kHideSettings& settings);
    [[nodiscard]] const Ed3kHideSettings& settings() const { return m_settings; }

    /// Starts or stops the anonymous node as the settings say; hopping and share. After the
    /// session's start (hops.dat imported) and on a change.
    void apply();
    /// The node started: keys.json of its state dir is read.
    void attach();
    /// The node stopped (or died): saved, nothing runs.
    void detach();

    /// The anonymous node runs.
    [[nodiscard]] bool hiding() const;
    [[nodiscard]] std::optional<::ed3k::NodeId> anonId() const;

    // -- which key a file is under -------------------------------------------

    /// The key a file of an anon-capable join goes under: its recorded one; a new one under
    /// the anonymous key while hiding, else the node key.
    [[nodiscard]] ::ed3k::KeyKind keyFor(const ::ed3k::Root& root) const;
    /// The key recorded for @p root; nullopt: never held.
    [[nodiscard]] std::optional<::ed3k::KeyKind> recorded(const ::ed3k::Root& root) const;
    /// Records @p root under @p key (saved, debounced).
    void record(const ::ed3k::Root& root, ::ed3k::KeyKind key);
    /// The key a file is held under now (ed3k_file_key), else the recorded one.
    [[nodiscard]] std::optional<::ed3k::KeyKind> keyOf(const ::ed3k::Root& root) const;

    /// A new anonymous key (hops.md §1: queue places and what uploaders remember are lost;
    /// the GUI asked). The anonymous node restarts under it while hiding. false: not written.
    bool newAnonKey(QString* error = nullptr);

    [[nodiscard]] Ed3kHideStatus status() const;

    /// Path events (HopsPath*): the status moved.
    void onEvent(const ::ed3k::Event& ev);

    /// "Node" / "Anonymous" for the GUI (not localized: an IPC value).
    [[nodiscard]] static QString keyName(::ed3k::KeyKind key);

signals:
    /// The anonymous node started (also again under a new key): Ed3kCredit::applyAnonKey.
    void started();
    /// The anonymous node stopped: its downloads and shares are gone.
    void stopped();
    /// Running, paths or the key of a file changed (PushEd3kHideChanged).
    void changed();

private:
    void load();
    void save();
    void scheduleSave();

    Ed3kSession& m_session;
    Ed3kHideSettings m_settings;
    /// what the engine was given: running with these hops; nullopt: not hiding
    std::optional<int> m_appliedHops;
    std::optional<bool> m_appliedHopping;
    int m_appliedShare = -1;
    std::map<::ed3k::Root, ::ed3k::KeyKind> m_keys;
    QString m_dir;
    bool m_loaded = false;
    QTimer m_saveTimer;
};

} // namespace eMule::ed3k
