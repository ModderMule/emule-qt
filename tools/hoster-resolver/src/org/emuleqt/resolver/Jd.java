// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.io.InterruptedIOException;
import java.net.InetAddress;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;

import jd.controlling.linkcrawler.CrawledLink;
import jd.controlling.linkcrawler.LinkCrawler;
import jd.http.Browser;
import jd.http.Browser.BrowserException;
import jd.http.ProxySelectorInterface;
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
import org.jdownloader.plugins.controller.LazyPlugin;
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
        clearCaptchas();
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
        // the guard resolves before the connect: both must see the same addresses (JVM cache)
        try {
            java.security.Security.setProperty("networkaddress.cache.ttl", "30");
        } catch (final Throwable ignore) {
        }
        applyRoute();
        Browser.setGlobalReadTimeout(60000);
        Browser.setGlobalConnectTimeout(30000);
        HostPluginController.getInstance().ensureLoaded();
        CrawlerPluginController.getInstance().ensureLoaded();
    }

    /** Application.IS_JARED is private and set on first use: false before anything asks. */
    static void notJared() {
        try {
            final java.lang.reflect.Field f = org.appwork.utils.Application.class.getDeclaredField("IS_JARED");
            f.setAccessible(true);
            f.set(null, Boolean.FALSE);
        } catch (final Throwable e) {
            Out.log("jar mode not set: " + e.getClass().getSimpleName());
        }
    }

    /** Puzzle images JD saved in an earlier run (ROOT/captchas): nobody reads them again. */
    static void clearCaptchas() {
        final String root = System.getProperty("ROOT");
        if (root == null) {
            return;
        }
        final java.nio.file.Path dir = java.nio.file.Paths.get(root, "captchas");
        if (!java.nio.file.Files.isDirectory(dir)) {
            return;
        }
        try (java.util.stream.Stream<java.nio.file.Path> all = java.nio.file.Files.walk(dir)) {
            all.sorted(java.util.Comparator.reverseOrder()).forEach(p -> p.toFile().delete());
        } catch (final java.io.IOException | RuntimeException e) {
            Out.log("old puzzles not cleared: " + e.getClass().getSimpleName());
        }
    }

    private static String proxyUrl = "";
    private static String bindAddress = "";

    /** null = direct. "http://host:port", "socks5://user:pass@host:port". */
    static synchronized void setProxy(final String url) {
        proxyUrl = url == null ? "" : url;
        applyRoute();
    }

    /** The core's interface selection: direct connections leave from this address; empty = any. */
    static synchronized void setBind(final String address) {
        bindAddress = address == null ? "" : address;
        applyRoute();
    }

    /**
     * A proxy wins (the user's chosen route); else direct, bound to the selected address. Either
     * way behind AddressGuard: no request to a non-public address.
     */
    private static synchronized void applyRoute() {
        if (!proxyUrl.isEmpty()) {
            final HTTPProxy proxy = HTTPProxy.parseHTTPProxy(proxyUrl);
            Browser.setGlobalProxy(new AddressGuard(proxy == null ? null : new StaticProxySelector(proxy), proxy != null));
            return;
        }
        ProxySelectorInterface direct = null;
        if (!bindAddress.isEmpty()) {
            try {
                direct = new StaticProxySelector(new HTTPProxy(InetAddress.getByName(bindAddress)));
            } catch (final Exception e) {
                // a literal never resolves: refuse rather than leak through the default route
                Out.log("bad bind address: " + e.getClass().getSimpleName());
                direct = new StaticProxySelector(new HTTPProxy(InetAddress.getLoopbackAddress()));
            }
        }
        Browser.setGlobalProxy(new AddressGuard(direct, false));
    }

    static int hostPluginCount() {
        return HostPluginController.getInstance().list().size();
    }

    static int crawlerPluginCount() {
        return CrawlerPluginController.getInstance().list().size();
    }

    /** Tests only (-Demuleqt.loopbackHosters=true): JD's DirectHTTP takes loopback URLs as a hoster. */
    static final boolean LOOPBACK_HOSTERS = Boolean.getBoolean("emuleqt.loopbackHosters");

    /** A loopback http(s) URL while LOOPBACK_HOSTERS is on. */
    static boolean loopbackHoster(final String url) {
        if (!LOOPBACK_HOSTERS || url == null) {
            return false;
        }
        try {
            final java.net.URI u = new java.net.URI(url);
            final String scheme = u.getScheme();
            final String host = u.getHost();
            return host != null && ("http".equalsIgnoreCase(scheme) || "https".equalsIgnoreCase(scheme))
                    && (host.equals("127.0.0.1") || host.equalsIgnoreCase("localhost") || host.equals("[::1]"));
        } catch (final java.net.URISyntaxException e) {
            return false;
        }
    }

    /** The daemon's switch (setGeneric): a public http(s) link no site plugin takes goes to DirectHTTP. */
    private static volatile boolean generic = false;

    static void setGeneric(final boolean on) {
        generic = on;
    }

    /** JD's generic plugin, for links no site plugin takes (setGeneric). */
    static final String DIRECT_HTTP = "DirectHTTP";

    /**
     * How only the generic plugin takes @p url: "file" (the path names a file, JD's "http links"),
     * "any" (any other public http(s) URL), null (a site plugin takes it, or none). No network.
     */
    static String genericKind(final String url) {
        if (!generic || !publicHttp(url) || siteMatch(url) != null || hasSitePlugin(url)) {
            return null;
        }
        final LazyHostPlugin links = HostPluginController.getInstance().get("http links");
        return links != null && links.canHandle(url) ? "file" : "any";
    }

    /** The URL's site has a hoster plugin (a link of it the plugin does not take: no plain download there). */
    private static boolean hasSitePlugin(final String url) {
        try {
            final LazyHostPlugin p = hostPlugin(new java.net.URI(url.trim()).getHost());
            return usable(p);
        } catch (final java.net.URISyntaxException e) {
            return false;
        }
    }

    /** An http(s) URL with a host that is no literal non-public address and no local name. No network. */
    static boolean publicHttp(final String url) {
        if (url == null) {
            return false;
        }
        try {
            final java.net.URI u = new java.net.URI(url.trim());
            final String scheme = u.getScheme();
            if (!"http".equalsIgnoreCase(scheme) && !"https".equalsIgnoreCase(scheme) || u.getHost() == null) {
                return false;
            }
            AddressGuard.check(u.getHost(), false);
            return true;
        } catch (final java.net.URISyntaxException | AddressGuard.Blocked e) {
            return false;
        }
    }

    /** Plugin that would take the link, crawler first as LinkCrawler does. No network. */
    static String match(final String url) {
        if (loopbackHoster(url)) {
            return DIRECT_HTTP;
        }
        final String site = siteMatch(url);
        if (site != null || genericKind(url) == null) {
            return site;
        }
        return DIRECT_HTTP;
    }

    /** match() without the generic plugin. */
    private static String siteMatch(final String url) {
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
        return crawl(url, false);
    }

    /**
     * As crawl(url); deep: a page no plugin takes is loaded and every hoster link on it crawled.
     * With setGeneric: a link no site plugin takes goes to DirectHTTP (not deep: a page is no
     * file), and direct links a decrypter finds are kept.
     */
    static List<DownloadLink> crawl(final String url, final boolean deep) throws InterruptedException {
        final boolean direct = loopbackHoster(url) || !deep && genericKind(url) != null;
        final LinkCrawler lc = new LinkCrawler(false, false);
        lc.crawl(direct ? "directhttp://" + url.trim() : url, null, deep);
        lc.waitForCrawling();
        final List<DownloadLink> out = new ArrayList<DownloadLink>();
        for (final CrawledLink cl : lc.getCrawledLinks()) {
            final DownloadLink dl = cl.getDownloadLink();
            if (dl == null || dl.getDefaultPlugin() == null) {
                continue;
            }
            final String name = dl.getDefaultPlugin().getHost();
            final String found = dl.getPluginPatternMatcher();
            if (direct) {
                // JD also lifts URLs out of the query (?url=...): the link asked for wins, else public ones
                if (url.trim().equals(plainUrl(found))) {
                    out.clear();
                    out.add(dl);
                    break;
                }
                if (isDirectHttp(name) && (loopbackHoster(url) || publicHttp(plainUrl(found)))) {
                    out.add(dl);
                }
            } else if (!isGeneric(name) || generic && isDirectHttp(name) && publicHttp(plainUrl(found))) {
                out.add(dl);
            }
        }
        return out;
    }

    /** A crawled generic link as http(s): without JD's directhttp:// prefix or its httpviajd scheme. */
    private static String plainUrl(final String url) {
        if (url == null) {
            return null;
        }
        String u = url.startsWith("directhttp://") ? url.substring("directhttp://".length()) : url;
        if (u.startsWith("httpviajd://") || u.startsWith("httpsviajd://")) {
            u = u.replaceFirst("viajd://", "://");
        }
        return u;
    }

    /** JD's generic http plugin, under either of its names. */
    private static boolean isDirectHttp(final String name) {
        return name != null && (name.equalsIgnoreCase(DIRECT_HTTP) || name.equalsIgnoreCase("http links"));
    }

    /** A fresh plugin instance for one link, ready to be called on the current thread. */
    static PluginForHost newPlugin(final DownloadLink link) throws Exception {
        return newPlugin(link, link.getDefaultPlugin().getLazyP());
    }

    /** @p lazy's plugin for @p link: its own, or a multi-hoster's that downloads it. */
    static PluginForHost newPlugin(final DownloadLink link, final LazyHostPlugin lazy) throws Exception {
        final PluginClassLoader.PluginClassLoaderChild cl = PluginClassLoader.getInstance().getChild();
        PluginClassLoader.setThreadPluginClassLoaderChild(cl, cl);
        final PluginForHost plugin = lazy.newInstance(cl);
        // every connection of the plugin checked, redirects too (AddressGuard)
        plugin.setBrowser(GuardedBrowser.of(plugin));
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

    /**
     * accountHosts event: {hosts: [{host, name?, multi?, solver?}]}, every plugin that takes a premium
     * account (JD's AddAccountDialog list) but Usenet ones (eMule Qt has its own), sorted by host.
     */
    static Map<String, Object> accountHosts(final long id) {
        final java.util.TreeMap<String, Map<String, Object>> sorted = new java.util.TreeMap<String, Map<String, Object>>();
        for (final LazyHostPlugin p : HostPluginController.getInstance().list()) {
            if (!usable(p) || !p.isPremium() || p.hasFeature(LazyPlugin.FEATURE.USENET)
                || p.hasFeature(LazyPlugin.FEATURE.INTERNAL)) {
                continue;
            }
            final String host = p.getHost().toLowerCase(Locale.ROOT);
            if (host.indexOf('.') <= 0 || sorted.containsKey(host)) {
                continue;
            }
            final Map<String, Object> h = new java.util.LinkedHashMap<String, Object>();
            h.put("host", host);
            final String name = p.getDisplayName();
            if (name != null && !name.equalsIgnoreCase(host)) {
                h.put("name", name);
            }
            if (p.hasFeature(LazyPlugin.FEATURE.MULTIHOST)) {
                h.put("multi", true);
            }
            if (p.hasFeature(LazyPlugin.FEATURE.CAPTCHA_SOLVER)) {
                h.put("solver", true);
            }
            final List<String> subs = subdomains(p);
            if (!subs.isEmpty()) {
                h.put("subs", subs);
            }
            sorted.put(host, h);
        }
        final Map<String, Object> m = Out.event(id, "accountHosts");
        m.put("hosts", new ArrayList<Object>(sorted.values()));
        return m;
    }

    /** subdomain labels in a URL pattern: "(?:www\\.|new\\.|m\\.)?" */
    private static final java.util.regex.Pattern kLabelGroup = java.util.regex.Pattern
        .compile("\\(\\?:((?:[a-z0-9-]+\\.\\|)*[a-z0-9-]+\\.)\\)");

    /**
     * Other names of a plugin's sites, for a server country when its host sits behind a CDN:
     * its alias domains, subdomain labels and literal subdomains of its URL pattern ("www" left
     * out), the premium page's host. At most 8, host itself not.
     */
    static List<String> subdomains(final LazyHostPlugin p) {
        final List<String> sites = sites(p);
        final String host = sites.get(0);
        final java.util.LinkedHashSet<String> out = new java.util.LinkedHashSet<String>();
        String src = "";
        try {
            src = p.getPatternSource().toLowerCase(Locale.ROOT).replace("\\.", ".").replace("\\-", "-");
        } catch (final Throwable e) {
            // aliases only
        }
        final List<String> labels = new ArrayList<String>();
        for (final java.util.regex.Matcher m = kLabelGroup.matcher(src); m.find();) {
            for (final String l : m.group(1).split("\\|")) {
                final String label = l.substring(0, l.length() - 1);
                if (!label.equals("www") && !labels.contains(label)) {
                    labels.add(label);
                }
            }
        }
        for (final String d : sites) {
            for (final String l : labels) {
                out.add(l + "." + d);
            }
            // a literal "api.example.com" in the pattern
            final java.util.regex.Matcher m = java.util.regex.Pattern
                .compile("(?<![a-z0-9.-])((?:[a-z0-9-]+\\.)+)" + java.util.regex.Pattern.quote(d) + "(?![a-z0-9-])").matcher(src);
            while (m.find()) {
                final String sub = m.group(1) + d;
                if (!sub.startsWith("www.")) {
                    out.add(sub);
                }
            }
        }
        for (int i = 1; i < sites.size(); i++) {
            out.add(sites.get(i));
        }
        try {
            final String premium = p.getPremiumUrl();
            if (premium != null) {
                final String ph = new java.net.URL(premium).getHost().toLowerCase(Locale.ROOT);
                for (final String d : sites) {
                    if (!ph.startsWith("www.") && ph.endsWith("." + d)) {
                        out.add(ph);
                    }
                }
            }
        } catch (final Throwable e) {
            // no premium page
        }
        out.remove(host);
        final List<String> list = new ArrayList<String>();
        for (final String s : out) {
            if (list.size() < 8 && s.indexOf('.') > 0 && s.matches("[a-z0-9.-]+")) {
                list.add(s);
            }
        }
        return list;
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
        for (Throwable c = t; c != null; c = c.getCause() == c ? null : c.getCause()) {
            if (c instanceof AddressGuard.Blocked) {
                return errorEvent(id, "private", ((AddressGuard.Blocked) c).host);
            }
        }
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
                return needsUser(id, puzzleKind(id));
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
                return needsUser(id, puzzleKind(id));
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

    /** "skipped" or "wrong" after an image puzzle the user saw; else "puzzle" (one no solver takes). */
    private static String puzzleKind(final long id) {
        final String o = PuzzleSolver.outcome(id);
        return o != null ? o : "puzzle";
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
