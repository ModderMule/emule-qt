// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import jd.plugins.AccountInfo;
import jd.plugins.MultiHostHost;
import jd.plugins.PluginForHost;

import org.jdownloader.plugins.controller.PluginClassLoader;
import org.jdownloader.plugins.controller.host.LazyHostPlugin;

/**
 * Multi-hosters (Real-Debrid, AllDebrid, Premiumize, ...): JD plugins whose account downloads
 * other hosters' links (handleMultiHost). What AccountController does for them, without it.
 */
final class Debrid {
    /** An account's info is fetched again after this, for an open. */
    private static final long INFO_TTL_MS = 10 * 60 * 1000L;
    private static final Map<jd.plugins.Account, Long> FETCHED = new ConcurrentHashMap<jd.plugins.Account, Long>();

    private Debrid() {
    }

    /** @p ai goes to @p acc and marks it a multi-hoster account when it names hosts (AccountController). */
    static void adopt(final jd.plugins.Account acc, final AccountInfo ai) {
        acc.setAccountInfo(ai);
        final List<MultiHostHost> hosts = ai == null ? null : ai.getMultiHostSupportV2();
        acc.setProperty(jd.plugins.Account.IS_MULTI_HOSTER_ACCOUNT, hosts != null && !hosts.isEmpty());
        FETCHED.put(acc, System.currentTimeMillis());
    }

    /** @p acc's info, fetched when there is none or it is old. Throws the plugin's login error. */
    static AccountInfo ensure(final LazyHostPlugin lazy, final jd.plugins.Account acc) throws Exception {
        final Long at = FETCHED.get(acc);
        final AccountInfo have = acc.getAccountInfo();
        if (have != null && at != null && System.currentTimeMillis() - at < INFO_TTL_MS) {
            return have;
        }
        final PluginClassLoader.PluginClassLoaderChild cl = PluginClassLoader.getInstance().getChild();
        PluginClassLoader.setThreadPluginClassLoaderChild(cl, cl);
        final PluginForHost plugin = lazy.newInstance(cl);
        plugin.setBrowser(GuardedBrowser.of(plugin));
        plugin.setLogger(Jd.quietLog(lazy.getDisplayName()));
        try {
            final AccountInfo ai = plugin.fetchAccountInfo(acc);
            if (ai == null || ai.isExpired()) {
                throw new jd.plugins.PluginException(jd.plugins.LinkStatus.ERROR_PREMIUM);
            }
            adopt(acc, ai);
            return ai;
        } finally {
            plugin.clean();
        }
    }

    /**
     * The hosts @p ai downloads, for accountCheck: domain, domains, status, linksLeft/Max,
     * trafficLeft/Max (bytes; -1 unlimited / unknown), factor (traffic per byte, percent),
     * maxDownloads (-1 unlimited).
     */
    static List<Map<String, Object>> hosts(final AccountInfo ai) {
        final List<Map<String, Object>> out = new ArrayList<Map<String, Object>>();
        final List<MultiHostHost> hosts = ai == null ? null : ai.getMultiHostSupportV2();
        if (hosts == null) {
            return out;
        }
        for (final MultiHostHost h : hosts) {
            if (h.getDomain() == null) {
                continue;
            }
            final Map<String, Object> m = new LinkedHashMap<String, Object>();
            m.put("domain", h.getDomain());
            m.put("domains", new ArrayList<String>(h.getDomains()));
            m.put("status", working(h) ? "working" : h.getStatus().name().toLowerCase(java.util.Locale.ROOT));
            m.put("linksLeft", h.isUnlimitedLinks() ? -1L : Math.max(0, h.getLinksLeft()));
            m.put("linksMax", h.isUnlimitedLinks() ? -1L : h.getLinksMax());
            m.put("trafficLeft", h.isUnlimitedTraffic() ? -1L : Math.max(0, h.getTrafficLeft()));
            m.put("trafficMax", h.isUnlimitedTraffic() ? -1L : h.getTrafficMax());
            m.put("factor", (int) h.getTrafficCalculationFactorPercent());
            final int max = h.getMaxDownloads();
            m.put("maxDownloads", max == Integer.MAX_VALUE ? -1 : Math.max(0, max));
            if (h.getUnavailableTimeMillis() > 0) {
                m.put("unavailableS", h.getUnavailableTimeMillis() / 1000);
            }
            out.add(m);
        }
        return out;
    }

    /** Usable now: JD's status says so and the user did not switch it off. */
    static boolean working(final MultiHostHost h) {
        switch (h.getStatus()) {
        case WORKING:
        case WORKING_UNSTABLE:
            return h.isEnabled() && h.getUnavailableTimeMillis() <= 0;
        default:
            return false;
        }
    }
}
