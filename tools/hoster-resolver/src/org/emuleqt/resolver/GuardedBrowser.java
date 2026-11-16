// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.io.IOException;

import jd.http.Browser;
import jd.http.Request;
import jd.http.URLConnectionAdapter;
import jd.plugins.Plugin;
import jd.plugins.PluginBrowser;

import org.appwork.utils.net.httpconnection.HTTPProxy;

/**
 * A plugin's browser that asks AddressGuard before every connection (#202). JD picks the route
 * once per request and a redirect inherits it, so the global selector sees the first hop only;
 * here every hop, the download and every clone are checked. Used where the plugin keeps JD's
 * plain PluginBrowser (DirectHTTP does).
 */
final class GuardedBrowser extends PluginBrowser<Plugin> {
    GuardedBrowser(final Plugin plugin) {
        super(plugin);
    }

    /** @p plugin's fresh browser, guarded unless the plugin brings its own browser class. */
    static Browser of(final Plugin plugin) {
        final Browser b = plugin.createNewBrowserInstance();
        return b != null && b.getClass() == PluginBrowser.class ? new GuardedBrowser(plugin) : b;
    }

    @Override
    public Browser createNewBrowserInstance() {
        return new GuardedBrowser(getPlugin());
    }

    @Override
    public URLConnectionAdapter createHTTPConnection(final Request request, final HTTPProxy proxy) throws IOException {
        // through a proxy: literals and local names only, the proxy resolves
        final boolean viaProxy = proxy != null && !proxy.isLocal();
        AddressGuard.check(request.getURL().getHost(), !viaProxy);
        return super.createHTTPConnection(request, proxy);
    }
}
