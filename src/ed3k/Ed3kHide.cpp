/// @file Ed3kHide.cpp
/// @brief Hiding: the anonymous node and the key of each file — implementation.

#include "Ed3kHide.h"

#include "Ed3kKeyFile.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include <algorithm>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

QString hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toHex());
}

std::optional<ffi::Root> rootOfHex(const QString& hex)
{
    const QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    if (raw.size() != 32)
        return std::nullopt;
    ffi::Root r;
    std::copy_n(reinterpret_cast<const std::uint8_t*>(raw.constData()), 32, r.bytes.begin());
    return r;
}

} // namespace

Ed3kHide::Ed3kHide(Ed3kSession& session, QObject* parent)
    : QObject(parent)
    , m_session(session)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(2000);
    connect(&m_saveTimer, &QTimer::timeout, this, &Ed3kHide::save);
}

Ed3kHide::~Ed3kHide()
{
    if (m_saveTimer.isActive())
        save();
}

void Ed3kHide::setSettings(const Ed3kHideSettings& settings)
{
    const Ed3kHideSettings s = normalized(settings);
    if (s == m_settings)
        return;
    m_settings = s;
    if (!m_session.engine())
        return;
    apply();
    m_session.pump();
}

void Ed3kHide::apply()
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto t = m_session.now();
    // §5: forwarding for others while hiding (others' traffic covers its own, §1)
    const bool hopping = m_settings.on && m_settings.hopping;
    if (m_appliedHopping != hopping) {
        if (auto ok = e->hopsSetHopping(t, hopping); ok)
            m_appliedHopping = hopping;
        else if (ok.error() != ffi::Error::Unsupported)
            logEd3kWarning(QStringLiteral("ed3k: hopping not set (error %1)").arg(int(ok.error())));
    }
    if (m_appliedShare != m_settings.sharePercent) {
        if (auto ok = e->hopsSetShare(t, std::uint32_t(m_settings.sharePercent), 100); ok)
            m_appliedShare = m_settings.sharePercent;
    }

    if (m_settings.on) {
        if (m_appliedHops == m_settings.hops)
            return;
        QString err;
        auto seed = loadOrCreateSecret(m_dir, kSeedFile, nullptr, &err);
        if (!seed) {
            logEd3kError(QStringLiteral("ed3k: no anonymous key (%1); not hiding").arg(err));
            return;
        }
        (void)ownerOnly(QDir(m_dir).filePath(kSeedFile));
        // one hop count for every use; gates at the core default
        // was: auto ok = e->anonStart(t, *seed, ffi::HideCfg::uniform(std::uint8_t(m_settings.hops)));
        auto ok = e->anonStart(t, *seed, hideCfg(m_settings.hops));
        if (!ok && ok.error() == ffi::Error::Config && m_settings.hops == 0) {
            // a core without 0 hops (ED3K_HOPS_OFF): 1 hop, as §2.6 does without HOP
            logEd3kWarning(QStringLiteral("ed3k: 0 hops refused by the core; hiding with 1 hop"));
            ok = e->anonStart(t, *seed, hideCfg(1));
        }
        wipe(std::span<std::uint8_t>{*seed});
        if (!ok) {
            logEd3kWarning(QStringLiteral("ed3k: hiding not started (error %1)").arg(int(ok.error())));
            return;
        }
        const bool was = m_appliedHops.has_value();
        m_appliedHops = m_settings.hops;
        const auto id = anonId();
        logEd3k(QStringLiteral("ed3k: hiding with %1 hop(s), anonymous node %2")
                    .arg(m_settings.hops)
                    .arg(id ? hexOf(id->bytes).left(8) : QString()));
        if (!was)
            emit started();
        emit changed();
    } else if (m_appliedHops) {
        (void)e->anonStop(t);
        m_appliedHops.reset();
        logEd3k(QStringLiteral("ed3k: hiding stopped; files of the anonymous key are not shared until it is on again"));
        emit stopped();
        emit changed();
    }
}

void Ed3kHide::attach()
{
    m_dir = m_session.stateDir();
    m_appliedHops.reset();
    m_appliedHopping.reset();
    m_appliedShare = -1;
    if (!m_loaded)
        load();
}

void Ed3kHide::detach()
{
    const bool was = m_appliedHops.has_value();
    m_appliedHops.reset();
    m_appliedHopping.reset();
    m_appliedShare = -1;
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        save();
    }
    if (was)
        emit stopped();
}

bool Ed3kHide::hiding() const
{
    return anonId().has_value();
}

std::optional<ffi::NodeId> Ed3kHide::anonId() const
{
    auto* e = m_session.engine();
    if (!e)
        return std::nullopt;
    const auto id = e->anonId();
    return id ? *id : std::nullopt;
}

ffi::KeyKind Ed3kHide::keyForUse() const
{
    return hiding() ? ffi::KeyKind::Anon : ffi::KeyKind::Node;
}

ffi::KeyKind Ed3kHide::keyFor(const ffi::Root& root) const
{
    if (const auto k = recorded(root))
        return *k;
    return hiding() ? ffi::KeyKind::Anon : ffi::KeyKind::Node;
}

std::optional<ffi::KeyKind> Ed3kHide::recorded(const ffi::Root& root) const
{
    const auto it = m_keys.find(root);
    return it == m_keys.end() ? std::nullopt : std::optional(it->second);
}

void Ed3kHide::record(const ffi::Root& root, ffi::KeyKind key)
{
    auto [it, added] = m_keys.try_emplace(root, key);
    if (!added && it->second == key)
        return;
    const bool moved = !added;
    it->second = key;
    scheduleSave();
    if (moved || key == ffi::KeyKind::Anon)
        emit changed();
}

std::optional<ffi::KeyKind> Ed3kHide::keyOf(const ffi::Root& root) const
{
    if (auto* e = m_session.engine())
        if (const auto k = e->fileKey(root); k && *k)
            return **k;
    return recorded(root);
}

bool Ed3kHide::newAnonKey(QString* error)
{
    if (m_dir.isEmpty())
        m_dir = m_session.stateDir();
    if (m_dir.isEmpty()) {
        if (error)
            *error = QStringLiteral("ed3k has not run yet");
        return false;
    }
    Seed seed{};
    QRandomGenerator::system()->generate(seed.begin(), seed.end());
    const bool ok = replaceSecret(m_dir, kSeedFile, seed, error);
    wipe(std::span<std::uint8_t>{seed});
    if (!ok)
        return false;
    logEd3k(QStringLiteral("ed3k: a new anonymous key"));
    // the running anonymous node is replaced: it loses its downloads and shares (§1)
    if (m_appliedHops) {
        emit stopped();
        m_appliedHops.reset();
        apply();
        m_session.pump();
    }
    emit changed();
    return true;
}

Ed3kHideStatus Ed3kHide::status() const
{
    Ed3kHideStatus st;
    st.on = m_settings.on;
    st.hops = m_settings.hops;
    st.anonFiles = int(std::ranges::count(m_keys, ffi::KeyKind::Anon, &decltype(m_keys)::value_type::second));
    const auto id = anonId();
    if (!id)
        return st;
    st.running = true;
    st.nodeId = hexOf(id->bytes).left(8);
    if (auto* e = m_session.engine())
        if (auto paths = e->hopsPaths()) {
            st.paths = int(paths->size());
            st.built = int(std::ranges::count_if(*paths, [](const auto& p) { return p.built; }));
        }
    st.waitingForHops = st.built == 0;
    return st;
}

void Ed3kHide::onEvent(const ffi::Event& ev)
{
    namespace e = ffi::event;
    if (const auto* b = std::get_if<e::HopsPathBuilt>(&ev)) {
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: path %1 built (%2 hops)").arg(b->path).arg(b->hops); });
        emit changed();
    } else if (const auto* f = std::get_if<e::HopsPathFailed>(&ev)) {
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: path %1 failed at hop %2 (code %3)").arg(f->path).arg(f->hop).arg(f->code); });
        emit changed();
    } else if (std::holds_alternative<e::HopsPathClosed>(ev)) {
        emit changed();
    }
}

QString Ed3kHide::keyName(ffi::KeyKind key)
{
    return key == ffi::KeyKind::Anon ? QStringLiteral("Anonymous") : QStringLiteral("Node");
}

Ed3kHideSettings Ed3kHide::normalized(Ed3kHideSettings s)
{
    s.hops = std::clamp(s.hops, 0, 3);
    // §2.6: 0 only while forwarding for others; else its uses would go over 1 hop anyway
    if (s.hops == 0 && !s.hopping)
        s.hops = 1;
    s.sharePercent = std::clamp(s.sharePercent, 1, 100);
    return s;
}

ffi::HideCfg Ed3kHide::hideCfg(int hops)
{
    // out and in at @p hops (0: zero hops, §2.6); every per-use count 0 = takes hopsOut / hopsIn;
    // never kHopsOff (off = no anonStart); nodeKeyUses 0
    return ffi::HideCfg::uniform(std::uint8_t(std::clamp(hops, 0, 3)));
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kHide::load()
{
    if (m_dir.isEmpty())
        return;
    m_loaded = true;
    const QByteArray raw = readBlob(QDir(m_dir).filePath(kKeysFile));
    if (raw.isEmpty())
        return;
    const QJsonObject o = QJsonDocument::fromJson(raw).object();
    const auto take = [&](const QString& name, ffi::KeyKind key) {
        for (const auto& v : o.value(name).toArray())
            if (const auto r = rootOfHex(v.toString()))
                m_keys[*r] = key;
    };
    take(QStringLiteral("node"), ffi::KeyKind::Node);
    // anonymous last: a root in both (a broken file) stays hidden
    take(QStringLiteral("anon"), ffi::KeyKind::Anon);
}

void Ed3kHide::save()
{
    m_saveTimer.stop();
    if (m_dir.isEmpty())
        return;
    QJsonArray node;
    QJsonArray anon;
    for (const auto& [root, key] : m_keys)
        (key == ffi::KeyKind::Anon ? anon : node).append(hexOf(root.bytes));
    const QByteArray raw = QJsonDocument(QJsonObject{{QStringLiteral("node"), node}, {QStringLiteral("anon"), anon}})
                               .toJson(QJsonDocument::Compact);
    // which files are hidden: owner-only like the keys
    if (!writeBlob(QDir(m_dir).filePath(kKeysFile), std::as_bytes(std::span{raw.constData(), std::size_t(raw.size())})))
        logEd3kWarning(QStringLiteral("ed3k: cannot write %1").arg(kKeysFile));
}

void Ed3kHide::scheduleSave()
{
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

} // namespace eMule::ed3k
