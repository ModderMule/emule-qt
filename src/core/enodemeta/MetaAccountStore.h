#pragma once

/// @file MetaAccountStore.h
/// @brief Meta API session tokens, per server origin (daemon-side).
///
/// `<configDir>/metaaccounts.yml`, mode 0600. Stores the bearer token
/// AccountApi.Login returned — never the password.

#include <QHash>
#include <QString>

namespace eMule::enodemeta {

struct MetaAccount {
    QString username;
    QString token;
    qint64 tokenExpiresAt = 0;   ///< unix seconds, 0 = unknown/none

    [[nodiscard]] bool hasToken() const { return !token.isEmpty(); }
};

class MetaAccountStore {
public:
    /// @p path defaults to `<configDir>/metaaccounts.yml`.
    explicit MetaAccountStore(QString path = {});

    /// Account for @p origin; an expired token reads as none.
    [[nodiscard]] MetaAccount account(const QString& origin) const;

    void setAccount(const QString& origin, const MetaAccount& account);
    /// Forget the token, keep the username for the next login form.
    void clearToken(const QString& origin);

    [[nodiscard]] const QString& path() const { return m_path; }

private:
    void load();
    void save() const;

    QString m_path;
    QHash<QString, MetaAccount> m_accounts;
};

} // namespace eMule::enodemeta
