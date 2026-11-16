// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.io.IOException;
import java.net.Inet4Address;
import java.net.Inet6Address;
import java.net.InetAddress;
import java.net.URL;
import java.net.UnknownHostException;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

import jd.http.ProxySelectorInterface;
import jd.http.Request;

import org.appwork.utils.net.httpconnection.HTTPProxy;

/**
 * Wraps the global route: every request JD makes (redirects too) is asked here first, and one
 * to a loopback, private, link-local or other non-public address is refused (#202). Direct: the
 * name is resolved and every address checked (JVM DNS cache: the connect gets the same ones).
 * Through a proxy: literals and local names only, the proxy resolves (no DNS leak).
 */
final class AddressGuard implements ProxySelectorInterface {
    /** A request the guard refused; Jd.failure maps it to error code "private". */
    static final class Blocked extends RuntimeException {
        private static final long serialVersionUID = 1L;
        final String host;

        Blocked(final String host) {
            super("non-public address: " + host);
            this.host = host;
            BLOCKED.put(Thread.currentThread(), host);
        }
    }

    /** The last host refused on a thread: plugins swallow the exception and say "gone". */
    private static final java.util.Map<Thread, String> BLOCKED = new java.util.concurrent.ConcurrentHashMap<Thread, String>();

    /** The host refused on @p thread since the last call; null: none. */
    static String takeBlocked(final Thread thread) {
        return BLOCKED.remove(thread);
    }

    private static final List<HTTPProxy> DIRECT = Collections.singletonList(HTTPProxy.NONE);

    private final ProxySelectorInterface inner;
    private final boolean viaProxy;

    /** @p inner null: direct. @p viaProxy: a proxy resolves names, never resolve here. */
    AddressGuard(final ProxySelectorInterface inner, final boolean viaProxy) {
        this.inner = inner;
        this.viaProxy = viaProxy;
    }

    @Override
    public List<HTTPProxy> getProxiesByURL(final URL url) {
        check(url == null ? null : url.getHost(), !viaProxy);
        return inner == null ? DIRECT : inner.getProxiesByURL(url);
    }

    @Override
    public boolean updateProxy(final Request request, final int retryCounter) {
        return inner != null && inner.updateProxy(request, retryCounter);
    }

    @Override
    public boolean reportConnectException(final Request request, final int retryCounter, final IOException e) {
        return inner != null && inner.reportConnectException(request, retryCounter, e);
    }

    @Override
    public String toString() {
        return "AddressGuard(" + (inner == null ? "direct" : inner.toString()) + ")";
    }

    /** Throws Blocked for a non-public host. @p resolve: look names up (direct route). */
    static void check(final String rawHost, final boolean resolve) {
        if (rawHost == null || rawHost.isEmpty()) {
            return;   // no host: JD fails on its own
        }
        String host = rawHost.toLowerCase(Locale.ROOT);
        if (host.startsWith("[") && host.endsWith("]")) {
            host = host.substring(1, host.length() - 1);
        }
        if (Jd.loopbackHoster("http://" + (host.indexOf(':') >= 0 ? "[" + host + "]" : host) + "/")) {
            return;   // tests: the mock hoster
        }
        if (localName(host)) {
            throw new Blocked(rawHost);
        }
        if (isLiteral(host)) {
            try {
                if (!isPublic(InetAddress.getByName(host))) {
                    throw new Blocked(rawHost);
                }
            } catch (final UnknownHostException e) {
                // not a literal after all: JD reports it
            }
            return;
        }
        if (!resolve) {
            return;
        }
        final InetAddress[] all;
        try {
            all = InetAddress.getAllByName(host);
        } catch (final UnknownHostException e) {
            return;   // JD's own lookup fails the same way
        }
        for (final InetAddress a : all) {
            if (!isPublic(a)) {
                throw new Blocked(rawHost);
            }
        }
    }

    /** Names that only mean something on this machine or LAN; no lookup. */
    static boolean localName(final String host) {
        final String h = host.endsWith(".") ? host.substring(0, host.length() - 1) : host;
        return h.equals("localhost") || h.endsWith(".localhost") || h.endsWith(".local") || h.endsWith(".lan")
                || h.endsWith(".internal") || h.endsWith(".home.arpa") || h.indexOf('.') < 0 && h.indexOf(':') < 0
                && !h.chars().allMatch(Character::isDigit);
    }

    /** An IP literal (v6, dotted v4); a bare number goes through the lookup, as for the connect. */
    static boolean isLiteral(final String host) {
        return host.indexOf(':') >= 0 || host.matches("\\d{1,3}(\\.\\d{1,3}){3}");
    }

    /** Globally routable unicast. */
    static boolean isPublic(final InetAddress a) {
        if (a.isAnyLocalAddress() || a.isLoopbackAddress() || a.isLinkLocalAddress() || a.isSiteLocalAddress()
                || a.isMulticastAddress()) {
            return false;
        }
        final byte[] b = a.getAddress();
        if (a instanceof Inet4Address) {
            return publicV4(b, 0);
        }
        if (a instanceof Inet6Address) {
            final int b0 = b[0] & 0xff;
            if ((b0 & 0xfe) == 0xfc) {
                return false;   // fc00::/7 unique local
            }
            if (b0 == 0x20 && (b[1] & 0xff) == 0x02) {
                return publicV4(b, 2);   // 2002::/16 6to4: the embedded v4
            }
            if (b0 == 0x00 && (b[1] & 0xff) == 0x64 && (b[2] & 0xff) == 0xff && (b[3] & 0xff) == 0x9b) {
                return publicV4(b, 12);   // 64:ff9b::/96 NAT64: the embedded v4
            }
            boolean zeroHead = true;   // ::/96 (v4-compatible, ::, ::1)
            for (int i = 0; i < 12; ++i) {
                zeroHead &= b[i] == 0;
            }
            return !zeroHead;
        }
        return false;
    }

    private static boolean publicV4(final byte[] b, final int at) {
        final int o1 = b[at] & 0xff;
        final int o2 = b[at + 1] & 0xff;
        final int o3 = b[at + 2] & 0xff;
        return !(o1 == 0 || o1 == 10 || o1 == 127 || o1 >= 224               // this net, private, loopback, multicast, reserved
                || o1 == 100 && (o2 & 0xc0) == 64                             // 100.64/10 CGNAT
                || o1 == 169 && o2 == 254                                     // link-local
                || o1 == 172 && (o2 & 0xf0) == 16                             // 172.16/12
                || o1 == 192 && o2 == 168                                     // 192.168/16
                || o1 == 192 && o2 == 0 && o3 == 0                            // 192.0.0/24
                || o1 == 198 && (o2 & 0xfe) == 18);                           // 198.18/15 benchmarking
    }
}
