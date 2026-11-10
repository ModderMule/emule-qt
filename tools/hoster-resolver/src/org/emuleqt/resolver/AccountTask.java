// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.util.Map;

import jd.controlling.accountchecker.AccountCheckerThread;
import jd.plugins.AccountInfo;
import jd.plugins.PluginForHost;

import org.jdownloader.plugins.controller.LazyPlugin;
import org.jdownloader.plugins.controller.PluginClassLoader;
import org.jdownloader.plugins.controller.host.LazyHostPlugin;

/**
 * accountCheck: is a login valid, premium, until when, how much traffic, downloads at once; a
 * solver service's credit.
 */
final class AccountTask extends Task {
    private final String host;
    private final Account account;

    AccountTask(final long id, final String host, final Account account) {
        super(id);
        this.host = host;
        this.account = account;
    }

    @Override
    Thread newThread(final Runnable body) {
        return new AccountCheckerThread() {
            @Override
            public void run() {
                body.run();
            }
        };
    }

    @Override
    void run() throws Throwable {
        final LazyHostPlugin lazy = host == null ? null : Jd.hostPlugin(host);
        if (lazy == null || account == null) {
            Out.error(id, "noPlugin", null);
            return;
        }
        final PluginClassLoader.PluginClassLoaderChild cl = PluginClassLoader.getInstance().getChild();
        PluginClassLoader.setThreadPluginClassLoaderChild(cl, cl);
        final PluginForHost plugin = lazy.newInstance(cl);
        plugin.setBrowser(plugin.createNewBrowserInstance());
        plugin.setLogger(Jd.quietLog(lazy.getDisplayName()));
        final jd.plugins.Account acc = account.toJd(lazy.getHost());
        final Map<String, Object> m = Out.event(id, "account");
        try {
            final AccountInfo ai = plugin.fetchAccountInfo(acc);
            final boolean expired = ai != null && ai.isExpired();
            m.put("valid", ai != null && !expired);
            final jd.plugins.Account.AccountType type = acc.getType();
            m.put("premium", type == jd.plugins.Account.AccountType.PREMIUM || type == jd.plugins.Account.AccountType.LIFETIME);
            // JD's downloads at once for this account (what fetchAccountInfo set, else the plugin's)
            final Integer max = maxDownloads(plugin, acc);
            if (max != null) {
                m.put("maxDownloads", max);
            }
            if (ai != null && ai.getValidUntil() > 0) {
                m.put("expires", ai.getValidUntil() / 1000);
            }
            if (ai != null && !ai.isUnlimitedTraffic() && ai.getTrafficLeft() >= 0) {
                m.put("trafficLeft", ai.getTrafficLeft());
            }
            if (ai != null && lazy.hasFeature(LazyPlugin.FEATURE.CAPTCHA_SOLVER)) {
                m.put("balance", ai.getAccountBalance());
                if (ai.getCurrency() != null) {
                    m.put("currency", ai.getCurrency().getCurrencyCode());
                }
            }
        } catch (final jd.plugins.PluginException e) {
            m.put("valid", false);
        } finally {
            plugin.clean();
        }
        Out.send(m);
    }

    /** JD's getMaxSimultanDownload for @p acc without a link: -1 unlimited, null unknown. */
    private static Integer maxDownloads(final PluginForHost plugin, final jd.plugins.Account acc) {
        try {
            final int n = plugin.getMaxSimultanDownload(null, acc, null);
            return n == Integer.MAX_VALUE ? -1 : Math.max(n, 0);
        } catch (final Throwable e) {
            // a plugin that needs the link
            return null;
        }
    }
}
