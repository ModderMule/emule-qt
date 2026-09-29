/// @file MetaSearchService.cpp
/// @brief Daemon side of eNode meta search: metafile fetch + Meta API accounts.

#include "MetaSearchService.h"

#include "app/AppContext.h"
#include "enodemeta/GrpcWeb.h"
#include "search/SearchFile.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "utils/Log.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QPointer>
#include <QUrl>

namespace eMule {

using enodemeta::CallResult;
using Ipc::MetaStatus;
namespace pb = enodemeta::pb;
namespace grpcweb = enodemeta::grpcweb;

namespace {

MetaSearchService::Target targetFor(const Server& srv)
{
    MetaSearchService::Target t;
    t.endpoint.baseUrl = srv.metaApiUrl();
    t.endpoint.pin = srv.metaApiPin();
    t.endpoint.serverName = srv.name().isEmpty() ? srv.address() : srv.name();
    t.serverAddr = srv.addressWithPort();
    return t;
}

} // namespace

MetaSearchService& MetaSearchService::instance()
{
    static QPointer<MetaSearchService> s;
    if (!s)
        s = new MetaSearchService(QCoreApplication::instance());
    return *s;
}

MetaSearchService::MetaSearchService(QObject* parent)
    : QObject(parent)
{
}

std::optional<MetaSearchService::Target> MetaSearchService::targetForResult(const SearchFile& file) const
{
    return targetForServers(file.servers());
}

std::optional<MetaSearchService::Target>
MetaSearchService::targetForServers(const std::list<SearchFile::SServer>& servers) const
{
    if (theApp.serverList) {
        for (const auto& s : servers) {
            const Server* srv = theApp.serverList->findByIPTcp(s.ip, s.port);
            if (!srv)
                srv = theApp.serverList->getServerByIP(s.ip);
            if (srv && srv->hasMetaApi())
                return targetFor(*srv);
        }
    }
    // UDP answers from servers we never logged in to have no ident; the
    // connected server is the best remaining guess
    if (theApp.serverConnect) {
        if (const Server* cur = theApp.serverConnect->currentServer(); cur && cur->hasMetaApi())
            return targetFor(*cur);
    }
    return std::nullopt;
}

std::optional<MetaSearchService::Target> MetaSearchService::targetForServer(const QString& serverAddr) const
{
    auto matches = [&serverAddr](const Server& srv) {
        return srv.addressWithPort().compare(serverAddr, Qt::CaseInsensitive) == 0;
    };
    if (theApp.serverConnect) {
        if (const Server* cur = theApp.serverConnect->currentServer(); cur && matches(*cur) && cur->hasMetaApi())
            return targetFor(*cur);
    }
    if (theApp.serverList) {
        for (const auto& srv : theApp.serverList->servers()) {
            if (srv && matches(*srv) && srv->hasMetaApi())
                return targetFor(*srv);
        }
    }
    return std::nullopt;
}

void MetaSearchService::fetch(const Target& target, const QByteArray& hash16, const QString& catalogId,
                              quint32 maxBytes, FetchCallback cb)
{
    QPointer<MetaSearchService> self(this);
    m_client.getCaps(target.endpoint, [self, target, hash16, catalogId, maxBytes, cb = std::move(cb)]
                                      (const CallResult& r, const pb::Caps& caps) {
        if (!self)
            return;
        if (!r.ok()) {
            const MetaStatus st = statusFor(r);
            cb(st, errorText(r, st), baseMap(st, target), {});
            return;
        }
        self->fetchWithToken(target, caps, hash16, catalogId, maxBytes, cb);
    });
}

void MetaSearchService::authStatus(const Target& target, MapCallback cb)
{
    const QString token = store().account(target.endpoint.origin()).token;
    QPointer<MetaSearchService> self(this);
    m_client.getCaps(target.endpoint, [self, target, token, cb = std::move(cb)]
                                      (const CallResult& capsResult, const pb::Caps& caps) {
        if (!self)
            return;
        if (!capsResult.ok()) {
            const MetaStatus st = statusFor(capsResult);
            cb(false, errorText(capsResult, st), baseMap(st, target));
            return;
        }
        self->m_client.getAuthStatus(target.endpoint, token,
            [self, target, caps, cb](const CallResult& r, const pb::AuthStatus& st) {
            if (!self)
                return;
            QCborMap m = baseMap(MetaStatus::Ok, target);
            addCaps(m, caps);
            if (!r.ok()) {
                const MetaStatus s = statusFor(r);
                m.insert(QStringLiteral("status"), static_cast<int>(s));
                cb(false, errorText(r, s), m);
                return;
            }
            addAuthStatus(m, st);
            const QString username = self->store().account(target.endpoint.origin()).username;
            if (!st.loggedIn())
                m.insert(QStringLiteral("username"), username);   // prefill the form
            cb(true, {}, m);
        });
    });
}

void MetaSearchService::login(const Target& target, const QString& username, const QString& password,
                              MapCallback cb)
{
    QPointer<MetaSearchService> self(this);
    m_client.login(target.endpoint, username, password,
                   [self, target, username, cb = std::move(cb)](const CallResult& r, const pb::LoginResponse& resp) {
        if (!self)
            return;
        if (!r.ok()) {
            MetaStatus st = statusFor(r);
            QString err = errorText(r, st);
            if (r.grpcCode == grpcweb::Unauthenticated) {
                st = MetaStatus::AuthRequired;
                err = tr("Wrong user name or password.");
            }
            QCborMap m = baseMap(st, target);
            if (r.errorInfo)
                addErrorInfo(m, *r.errorInfo);
            cb(false, err, m);
            return;
        }

        enodemeta::MetaAccount acc;
        acc.username = username;
        acc.token = resp.token();
        acc.tokenExpiresAt = resp.tokenExpiresAtUnix();
        self->store().setAccount(target.endpoint.origin(), acc);
        logInfo(tr("Logged in to the Meta API of %1 as %2").arg(target.endpoint.serverName, username));

        QCborMap m = baseMap(MetaStatus::Ok, target);
        if (resp.hasStatus()) {
            addAuthStatus(m, resp.status());
            // Login succeeds for a pending account; say so right away
            if (resp.status().state() != pb::AccountStateGadget::AccountState::ACCOUNT_STATE_ACTIVE
                && resp.status().authMode() == pb::AuthModeGadget::AuthMode::AUTH_MODE_ACCOUNT_REQUIRED)
                m.insert(QStringLiteral("status"), static_cast<int>(MetaStatus::AccountInactive));
        }
        cb(true, {}, m);
    });
}

void MetaSearchService::logout(const Target& target, MapCallback cb)
{
    const QString origin = target.endpoint.origin();
    const QString token = store().account(origin).token;
    // forget locally whatever the server says — the user asked to be logged out
    store().clearToken(origin);
    if (token.isEmpty()) {
        cb(true, {}, baseMap(MetaStatus::Ok, target));
        return;
    }
    m_client.logout(target.endpoint, token, [target, cb = std::move(cb)](const CallResult& r, const pb::LogoutResponse&) {
        if (!r.ok())
            logWarning(QStringLiteral("Meta API logout on %1: %2").arg(target.endpoint.serverName, r.message));
        cb(true, {}, baseMap(MetaStatus::Ok, target));
    });
}

QCborMap MetaSearchService::statusMap(MetaStatus status, const QString& serverAddr, const QString& serverName)
{
    return QCborMap{
        {QStringLiteral("status"),     static_cast<int>(status)},
        {QStringLiteral("serverAddr"), serverAddr},
        {QStringLiteral("serverName"), serverName},
    };
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

enodemeta::MetaAccountStore& MetaSearchService::store()
{
    // configDir is only final once preferences loaded — create on first use
    if (!m_store)
        m_store = std::make_unique<enodemeta::MetaAccountStore>();
    return *m_store;
}

void MetaSearchService::fetchWithToken(const Target& target, const pb::Caps& caps, const QByteArray& hash16,
                                       const QString& catalogId, quint32 maxBytes, FetchCallback cb)
{
    const QString origin = target.endpoint.origin();
    const QString token = store().account(origin).token;
    const bool accountRequired = caps.authMode() == pb::AuthModeGadget::AuthMode::AUTH_MODE_ACCOUNT_REQUIRED;

    if (accountRequired && token.isEmpty()) {
        QCborMap m = baseMap(MetaStatus::AuthRequired, target);
        addCaps(m, caps);
        m.insert(QStringLiteral("username"), store().account(origin).username);
        cb(MetaStatus::AuthRequired, tr("%1 needs an account to download torrent and Usenet results.")
                                         .arg(target.endpoint.serverName), m, {});
        return;
    }

    const quint32 limit = maxBytes > 0 && (caps.maxMetafileBytes() == 0 || maxBytes < caps.maxMetafileBytes())
        ? maxBytes : caps.maxMetafileBytes();

    QPointer<MetaSearchService> self(this);
    m_client.getMetaFile(target.endpoint, hash16, catalogId, token, limit,
        [self, target, caps, origin, cb = std::move(cb)](const CallResult& r, const pb::MetaFile& file) {
        if (!self)
            return;
        if (r.ok()) {
            cb(MetaStatus::Ok, {}, baseMap(MetaStatus::Ok, target), file);
            return;
        }

        const MetaStatus st = statusFor(r);
        QCborMap m = baseMap(st, target);
        addCaps(m, caps);
        if (r.errorInfo)
            addErrorInfo(m, *r.errorInfo);
        if (st == MetaStatus::AuthRequired) {
            self->store().clearToken(origin);   // expired or revoked session
            m.insert(QStringLiteral("username"), self->store().account(origin).username);
        }
        if (st == MetaStatus::VerifyFailed)
            logWarning(tr("Meta API of %1 served a file that does not match its search result: %2")
                           .arg(target.endpoint.serverName, r.message));
        cb(st, errorText(r, st), m, {});
    });
}

QCborMap MetaSearchService::baseMap(MetaStatus status, const Target& target)
{
    return statusMap(status, target.serverAddr, target.endpoint.serverName);
}

void MetaSearchService::addCaps(QCborMap& m, const pb::Caps& caps)
{
    m.insert(QStringLiteral("authMode"), static_cast<int>(caps.authMode()));
    if (!caps.registrationUrl().isEmpty())
        m.insert(QStringLiteral("registrationUrl"), caps.registrationUrl());
    if (!caps.accountUrl().isEmpty())
        m.insert(QStringLiteral("accountUrl"), caps.accountUrl());
}

void MetaSearchService::addErrorInfo(QCborMap& m, const pb::ErrorInfo& info)
{
    if (!info.msgCode().isEmpty())
        m.insert(QStringLiteral("msgCode"), info.msgCode());
    if (!info.registrationUrl().isEmpty())
        m.insert(QStringLiteral("registrationUrl"), info.registrationUrl());
    if (!info.accountUrl().isEmpty())
        m.insert(QStringLiteral("accountUrl"), info.accountUrl());
    if (!info.pendingSteps().isEmpty()) {
        QCborArray steps;
        for (const auto& s : info.pendingSteps())
            steps.append(QCborMap{{QStringLiteral("title"), s.title()},
                                  {QStringLiteral("kind"), static_cast<int>(s.kind())},
                                  {QStringLiteral("url"), s.url()}});
        m.insert(QStringLiteral("pendingSteps"), steps);
    }
}

void MetaSearchService::addAuthStatus(QCborMap& m, const pb::AuthStatus& st)
{
    m.insert(QStringLiteral("authMode"), static_cast<int>(st.authMode()));
    m.insert(QStringLiteral("loggedIn"), st.loggedIn());
    m.insert(QStringLiteral("username"), st.username());
    m.insert(QStringLiteral("state"), static_cast<int>(st.state()));
    m.insert(QStringLiteral("expiresAt"), static_cast<qint64>(st.expiresAtUnix()));
    if (!st.registrationUrl().isEmpty())
        m.insert(QStringLiteral("registrationUrl"), st.registrationUrl());
    if (!st.accountUrl().isEmpty())
        m.insert(QStringLiteral("accountUrl"), st.accountUrl());
    if (!st.msgCode().isEmpty())
        m.insert(QStringLiteral("msgCode"), st.msgCode());
    QCborArray steps;
    for (const auto& s : st.pendingSteps())
        steps.append(QCborMap{{QStringLiteral("title"), s.title()},
                              {QStringLiteral("kind"), static_cast<int>(s.kind())},
                              {QStringLiteral("url"), s.url()}});
    m.insert(QStringLiteral("pendingSteps"), steps);
}

MetaStatus MetaSearchService::statusFor(const CallResult& r)
{
    switch (r.grpcCode) {
    case grpcweb::Ok:                return MetaStatus::Ok;
    case grpcweb::Unauthenticated:   return MetaStatus::AuthRequired;
    case grpcweb::PermissionDenied:  return MetaStatus::AccountInactive;
    case grpcweb::NotFound:          return MetaStatus::NotFound;
    case grpcweb::DataLoss:          return MetaStatus::VerifyFailed;
    case grpcweb::Unavailable:
    case grpcweb::DeadlineExceeded:  return MetaStatus::Unavailable;
    case grpcweb::ResourceExhausted:
        return r.errorInfo && r.errorInfo->msgCode() == u"metafile.too_large"
            ? MetaStatus::TooLarge : MetaStatus::RateLimited;
    default:                         return MetaStatus::Error;
    }
}

QString MetaSearchService::errorText(const CallResult& r, MetaStatus status)
{
    switch (status) {
    case MetaStatus::AuthRequired:    return tr("Please log in to the server's account.");
    case MetaStatus::AccountInactive: return tr("The account is not active yet — complete its registration steps.");
    case MetaStatus::NotFound:        return tr("The server no longer has this release.");
    case MetaStatus::VerifyFailed:    return tr("The file the server sent does not match the search result.");
    case MetaStatus::Unavailable:     return tr("The server's catalogue is unavailable right now.");
    case MetaStatus::RateLimited:     return tr("Too many requests — try again in a minute.");
    case MetaStatus::TooLarge:        return tr("The file is too large.");
    default:
        return r.message.isEmpty() ? tr("The Meta API request failed.") : r.message;
    }
}

} // namespace eMule
