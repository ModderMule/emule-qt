// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.util.Map;

/** Login passed with a request. Held for that request only; never logged. */
final class Account {
    final String user;
    final String password;

    private Account(final String user, final String password) {
        this.user = user;
        this.password = password;
    }

    /** From {"account": {"user": .., "password": ..}}, else null. */
    @SuppressWarnings("unchecked")
    static Account of(final Map<String, Object> req) {
        final Object a = req.get("account");
        if (!(a instanceof Map)) {
            return null;
        }
        final Map<String, Object> m = (Map<String, Object>) a;
        final String u = ResolverMain.str(m.get("user"));
        final String p = ResolverMain.str(m.get("password"));
        return u == null || u.isEmpty() ? null : new Account(u, p == null ? "" : p);
    }

    jd.plugins.Account toJd(final String host) {
        final jd.plugins.Account acc = new jd.plugins.Account(user, password);
        acc.setHoster(host);
        return acc;
    }

    @Override
    public String toString() {
        return "Account[redacted]";
    }
}
