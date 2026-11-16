// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Login passed with a request; never logged. The JD account made of it is kept for this run, so a
 * plugin's session (cookies, an OAuth token) serves the next request too; state() hands those
 * properties to the daemon, which gives them back after a restart.
 */
final class Account {
    /** JD accounts by plugin host, user and password: one per login, shared like JD does. */
    private static final Map<String, jd.plugins.Account> JD = new ConcurrentHashMap<String, jd.plugins.Account>();

    final String host;   ///< via: the multi-hoster's domain; null for a hoster's own login
    final String user;
    final String password;
    private final Map<String, Object> state;

    private Account(final String host, final String user, final String password, final Map<String, Object> state) {
        this.host = host;
        this.user = user;
        this.password = password;
        this.state = state;
    }

    /** From {"account": {"user": .., "password": .., "state"?: {..}}}, else null. */
    static Account of(final Map<String, Object> req) {
        return parse(req.get("account"), false);
    }

    /** From {"via": {"host": .., "user": .., "password": .., "state"?: {..}}}: a multi-hoster login, else null. */
    static Account via(final Map<String, Object> req) {
        return parse(req.get("via"), true);
    }

    @SuppressWarnings("unchecked")
    private static Account parse(final Object a, final boolean needHost) {
        if (!(a instanceof Map)) {
            return null;
        }
        final Map<String, Object> m = (Map<String, Object>) a;
        final String h = ResolverMain.str(m.get("host"));
        final String u = ResolverMain.str(m.get("user"));
        final String p = ResolverMain.str(m.get("password"));
        if (u == null || u.isEmpty() || (needHost && (h == null || h.isEmpty()))) {
            return null;
        }
        final Object s = m.get("state");
        return new Account(h, u, p == null ? "" : p, s instanceof Map ? (Map<String, Object>) s : null);
    }

    /** The JD account for @p host: the one of this run, else a new one with the daemon's state. */
    jd.plugins.Account toJd(final String host) {
        final String key = host + '\n' + user + '\n' + password;
        return JD.computeIfAbsent(key, k -> {
            final jd.plugins.Account acc = new jd.plugins.Account(user, password);
            acc.setHoster(host);
            if (state != null) {
                for (final Map.Entry<String, Object> e : state.entrySet()) {
                    acc.setProperty(e.getKey(), e.getValue());
                }
            }
            return acc;
        });
    }

    /** What a plugin keeps in @p acc between requests (session, token): plain values only. */
    static Map<String, Object> state(final jd.plugins.Account acc) {
        final Map<String, Object> out = new LinkedHashMap<String, Object>();
        final Map<String, Object> props = acc.getProperties();
        if (props == null) {
            return out;
        }
        for (final Map.Entry<String, Object> e : props.entrySet()) {
            final Object v = e.getValue();
            if (jd.plugins.Account.IS_MULTI_HOSTER_ACCOUNT.equalsIgnoreCase(e.getKey())) {
                continue;
            }
            if (v instanceof String || v instanceof Number || v instanceof Boolean) {
                out.put(e.getKey(), v);
            }
        }
        return out;
    }

    @Override
    public String toString() {
        return "Account[redacted]";
    }
}
