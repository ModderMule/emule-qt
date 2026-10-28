// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.io.InterruptedIOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;

import jd.controlling.linkcrawler.CrawledLink;
import jd.controlling.linkcrawler.LinkCrawler;
import jd.http.Browser;
import jd.http.Browser.BrowserException;
import jd.http.StaticProxySelector;
import jd.plugins.DownloadLink;
import jd.plugins.LinkStatus;
import jd.plugins.PluginException;
import jd.plugins.PluginForHost;

import org.appwork.utils.logging2.LogSource;
import org.appwork.utils.net.httpconnection.HTTPConnectionImpl;
import org.appwork.utils.net.httpconnection.HTTPProxy;
import org.appwork.utils.net.httpconnection.JavaSSLSocketStreamFactory;
import org.appwork.utils.net.httpconnection.JavaSSLSocketStreamFactory.TLS;
import org.jdownloader.net.AutoBCSSLSocketStreamFactory;
import org.jdownloader.plugins.SkipReason;
import org.jdownloader.plugins.SkipReasonException;
import org.jdownloader.plugins.controller.PluginClassLoader;
import org.jdownloader.plugins.controller.crawler.CrawlerPluginController;
import org.jdownloader.plugins.controller.crawler.LazyCrawlerPlugin;
import org.jdownloader.plugins.controller.host.HostPluginController;
import org.jdownloader.plugins.controller.host.LazyHostPlugin;

/** The few JD calls the driver needs, in one place. */
final class Jd {
    private Jd() {
    }

    /** Startup subset of org.jdownloader.startup.Main and SecondLevelLaunch.go, headless. */
    static void init() {
        System.setProperty("java.awt.headless", "true");
        System.setProperty("java.util.Arrays.useLegacyMergeSort", "true");
        if (System.getProperty("java.net.preferIPv6Addresses") == null) {
            System.setProperty("java.net.preferIPv6Addresses", "system");
        }
        org.appwork.utils.Application.setApplication(".emuleqt_resolver");
        try {
            java.security.Security.setProperty("networkaddress.cache.negative.ttl", "0");
        } catch (final Throwable ignore) {
        }
        try {
            if (!JavaSSLSocketStreamFactory.getInstance().isTLSSupported(TLS.TLS_1_3, null, null)) {
                HTTPConnectionImpl.setDefaultSSLSocketStreamFactory(new AutoBCSSLSocketStreamFactory());
            }
        } catch (final Throwable e) {
            HTTPConnectionImpl.setDefaultSSLSocketStreamFactory(new AutoBCSSLSocketStreamFactory());
        }
        Browser.setGlobalReadTimeout(60000);
        Browser.setGlobalConnectTimeout(30000);
        HostPluginController.getInstance().ensureLoaded();
        CrawlerPluginController.getInstance().ensureLoaded();
    }

    /** null = direct. "http://host:port", "socks5://user:pass@host:port". */
    static void setProxy(final String url) {
        if (url == null || url.isEmpty()) {
            Browser.setGlobalProxy(null);
            return;
        }
        final HTTPProxy proxy = HTTPProxy.parseHTTPProxy(url);
        Browser.setGlobalProxy(proxy == null ? null : new StaticProxySelector(proxy));
    }

    static int hostPluginCount() {
        return HostPluginController.getInstance().list().size();
    }

    static int crawlerPluginCount() {
        return CrawlerPluginController.getInstance().list().size();
    }

    /** Plugin that would take the link, crawler first as LinkCrawler does. No network. */
    static String match(final String url) {
        for (final LazyCrawlerPlugin p : CrawlerPluginController.getInstance().list()) {
            if (!p.isHasConfig() && "httpdirectorycrawler".equalsIgnoreCase(p.getDisplayName())) {
                continue;
            }
            if (isGeneric(p.getDisplayName())) {
                continue;
            }
            if (p.canHandle(url)) {
                return p.getDisplayName();
            }
        }
        final LazyHostPlugin p = matchHostPlugin(url);
        return p == null ? null : p.getDisplayName();
    }

    /** Runs the link through JD's crawler: decrypters first, then host plugins. */
    static List<DownloadLink> crawl(final String url) throws InterruptedException {
        final LinkCrawler lc = new LinkCrawler(false, false);
        lc.crawl(url);
        lc.waitForCrawling();
        final List<DownloadLink> out = new ArrayList<DownloadLink>();
        for (final CrawledLink cl : lc.getCrawledLinks()) {
            final DownloadLink dl = cl.getDownloadLink();
            if (dl != null && dl.getDefaultPlugin() != null && !isGeneric(dl.getDefaultPlugin().getHost())) {
                out.add(dl);
            }
        }
        return out;
    }

    /** A fresh plugin instance for one link, ready to be called on the current thread. */
    static PluginForHost newPlugin(final DownloadLink link) throws Exception {
        final LazyHostPlugin lazy = link.getDefaultPlugin().getLazyP();
        final PluginClassLoader.PluginClassLoaderChild cl = PluginClassLoader.getInstance().getChild();
        PluginClassLoader.setThreadPluginClassLoaderChild(cl, cl);
        final PluginForHost plugin = lazy.newInstance(cl);
        plugin.setBrowser(plugin.createNewBrowserInstance());
        plugin.setLogger(quietLog(lazy.getDisplayName()));
        plugin.setDownloadLink(link);
        plugin.init();
        return plugin;
    }

    /** Plugin logs: warnings only, so requests and form data never reach stderr. */
    static LogSource quietLog(final String name) {
        final LogSource log = new LogSource("resolver." + name);
        log.setLevel(java.util.logging.Level.WARNING);
        return log;
    }

    /** The host plugin for a domain or one of its aliases (siteSupportedNames); "www." and subdomains too. */
    static LazyHostPlugin hostPlugin(final String host) {
        if (host == null || host.trim().isEmpty()) {
            return null;
        }
        String h = host.trim().toLowerCase(Locale.ROOT);
        if (h.startsWith("www.")) {
            h = h.substring(4);
        }
        final LazyHostPlugin direct = HostPluginController.getInstance().get(h);
        if (usable(direct)) {
            return direct;
        }
        for (String d = h; d.indexOf('.') > 0; d = d.substring(d.indexOf('.') + 1)) {
            for (final LazyHostPlugin p : HostPluginController.getInstance().list()) {
                if (usable(p) && sites(p).contains(d)) {
                    return p;
                }
            }
        }
        return direct;
    }

    /** A plugin's domains, lower case: its host first, then its aliases. */
    static List<String> sites(final LazyHostPlugin p) {
        final List<String> out = new ArrayList<String>();
        out.add(p.getHost().toLowerCase(Locale.ROOT));
        final String[] more = p.getSitesSupported();
        if (more != null) {
            for (final String s : more) {
                final String d = s == null ? "" : s.trim().toLowerCase(Locale.ROOT);
                if (!d.isEmpty() && d.indexOf('.') > 0 && !out.contains(d)) {
                    out.add(d);
                }
            }
        }
        return out;
    }

    /** The host plugin match() would pick, or null. No network. */
    static LazyHostPlugin matchHostPlugin(final String url) {
        for (final LazyHostPlugin p : HostPluginController.getInstance().list()) {
            if (usable(p) && p.canHandle(url)) {
                return p;
            }
        }
        return null;
    }

    /** Maps a plugin failure to an event of the protocol (hoster-resolver.local.md §4). */
    static Map<String, Object> failure(final long id, final Throwable t) {
        Throwable e = t;
        if (e instanceof BrowserException && e.getCause() != null) {
            e = e.getCause();
        }
        if (e instanceof InterruptedException || e instanceof InterruptedIOException) {
            return Out.event(id, "cancelled");
        }
        if (e instanceof SkipReasonException) {
            final SkipReason r = ((SkipReasonException) e).getSkipReason();
            if (r == SkipReason.CAPTCHA) {
                return needsUser(id, "puzzle");
            }
            if (r == SkipReason.NO_ACCOUNT) {
                return needsUser(id, "login");
            }
            return errorEvent(id, "skipped", r.name());
        }
        if (e instanceof PluginException) {
            final PluginException pe = (PluginException) e;
            final long ms = pe.getValue();
            switch (pe.getLinkStatus()) {
            case LinkStatus.ERROR_FILE_NOT_FOUND:
                return Out.event(id, "gone");
            case LinkStatus.ERROR_CAPTCHA:
                return needsUser(id, "puzzle");
            case LinkStatus.ERROR_PREMIUM:
                return needsUser(id, "login");
            case LinkStatus.ERROR_IP_BLOCKED:
            case LinkStatus.ERROR_TEMPORARILY_UNAVAILABLE:
            case LinkStatus.ERROR_HOSTER_TEMPORARILY_UNAVAILABLE: {
                final Map<String, Object> m = Out.event(id, "wait");
                m.put("seconds", ms > 0 ? (ms + 999) / 1000 : 60);
                m.put("reason", pe.getLinkStatus() == LinkStatus.ERROR_IP_BLOCKED ? "quota" : "unavailable");
                return m;
            }
            case LinkStatus.ERROR_PLUGIN_DEFECT:
                return errorEvent(id, "defect", pe.getMessage());
            default:
                return errorEvent(id, "failed", pe.getMessage());
            }
        }
        if (e instanceof java.io.IOException) {
            return errorEvent(id, "io", e.getClass().getSimpleName());
        }
        return errorEvent(id, "defect", e.getClass().getSimpleName());
    }

    private static boolean usable(final LazyHostPlugin p) {
        return p != null && !p.isFallbackPlugin() && !p.isOfflinePlugin() && !isGeneric(p.getDisplayName());
    }

    /** A host plugin that would take any link, not a hoster: never a resolver. */
    private static boolean isGeneric(final String name) {
        if (name == null) {
            return true;
        }
        final String n = name.toLowerCase(java.util.Locale.ROOT);
        return n.equals("http links") || n.equals("directhttp") || n.equals("ftp") || n.equals("updaterequired")
                || n.equals("offline") || n.equals("generichttpdirectoryindex") || n.equals("generic autocontainer")
                || n.startsWith("generic");
    }

    private static Map<String, Object> needsUser(final long id, final String kind) {
        final Map<String, Object> m = Out.event(id, "needsUser");
        m.put("kind", kind);
        return m;
    }

    private static Map<String, Object> errorEvent(final long id, final String code, final String text) {
        final Map<String, Object> m = Out.event(id, "error");
        m.put("code", code);
        if (text != null) {
            m.put("text", text);
        }
        return m;
    }
}
