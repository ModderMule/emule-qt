// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import jd.controlling.AccountController;
import jd.controlling.AccountFilter;
import jd.controlling.accountchecker.AccountCheckerThread;
import jd.plugins.AccountInfo;
import jd.plugins.PluginException;
import jd.plugins.PluginForHost;

import org.jdownloader.plugins.components.captchasolver.abstractPluginForCaptchaSolver;
import org.jdownloader.plugins.controller.LazyPlugin;
import org.jdownloader.plugins.controller.PluginClassLoader;
import org.jdownloader.plugins.controller.host.LazyHostPlugin;
import org.jdownloader.settings.staticreferences.CFG_GENERAL;

/**
 * solverAccounts: replaces JD's captcha solver accounts (2captcha.com, anti-captcha.com, ...).
 * Each is checked first; only valid ones are added, then JD's solver plugins take puzzles
 * ahead of the user. Answers `solvers` with per-account valid/balance.
 */
final class SolverAccountsTask extends Task {
    private final List<Map<String, Object>> accounts;

    SolverAccountsTask(final long id, final List<Map<String, Object>> accounts) {
        super(id);
        this.accounts = accounts;
    }

    /** Drops every solver account, also ones JD saved in an earlier run. */
    static void clear() {
        final AccountController ac = AccountController.getInstance();
        for (final jd.plugins.Account a : ac.listAccounts(new AccountFilter().setFeature(LazyPlugin.FEATURE.CAPTCHA_SOLVER))) {
            ac.removeAccount(a);
        }
    }

    /** The solver plugin of @p host, else null. */
    static LazyHostPlugin solverPlugin(final String host) {
        final LazyHostPlugin lazy = Jd.hostPlugin(host);
        return lazy != null && lazy.hasFeature(LazyPlugin.FEATURE.CAPTCHA_SOLVER) ? lazy : null;
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
        clear();
        CFG_GENERAL.CFG.setUseAvailableCaptchaSolverAccounts(true);
        final List<Map<String, Object>> out = new ArrayList<Map<String, Object>>();
        for (final Map<String, Object> a : accounts) {
            if (cancelled.get()) {
                break;
            }
            out.add(add(a));
        }
        final Map<String, Object> m = Out.event(id, "solvers");
        m.put("accounts", out);
        Out.send(m);
    }

    /** Checks one account and adds it when valid; the result row. */
    private static Map<String, Object> add(final Map<String, Object> a) {
        final String host = ResolverMain.str(a.get("host"));
        final String user = ResolverMain.str(a.get("user"));
        final String password = ResolverMain.str(a.get("password"));
        final Map<String, Object> r = new java.util.LinkedHashMap<String, Object>();
        r.put("host", host);
        r.put("valid", false);
        final LazyHostPlugin lazy = solverPlugin(host);
        if (lazy == null) {
            r.put("code", "noPlugin");
            return r;
        }
        r.put("host", lazy.getHost());
        final PluginClassLoader.PluginClassLoaderChild cl = PluginClassLoader.getInstance().getChild();
        PluginClassLoader.setThreadPluginClassLoaderChild(cl, cl);
        PluginForHost plugin = null;
        try {
            plugin = lazy.newInstance(cl);
            plugin.setBrowser(plugin.createNewBrowserInstance());
            plugin.setLogger(Jd.quietLog(lazy.getDisplayName()));
            // key-only services read the password field; their user is free text
            final jd.plugins.Account acc = new jd.plugins.Account(user == null || user.isEmpty() ? "apikey" : user,
                                                                  password == null ? "" : password);
            acc.setHoster(lazy.getHost());
            acc.setPlugin(plugin);
            final AccountInfo ai = ((abstractPluginForCaptchaSolver) plugin).fetchAccountInfo(acc);
            if (ai == null || ai.isExpired()) {
                r.put("code", "login");
                return r;
            }
            acc.setAccountInfo(ai);
            r.put("valid", true);
            r.put("balance", ai.getAccountBalance());
            if (ai.getCurrency() != null) {
                r.put("currency", ai.getCurrency().getCurrencyCode());
            }
            AccountController.getInstance().addAccount(acc, false);
            plugin = null;   // the account keeps it
        } catch (final PluginException e) {
            r.put("code", "login");
            if (e.getMessage() != null) {
                r.put("text", e.getMessage());
            }
        } catch (final java.io.IOException e) {
            r.put("code", "io");
            r.put("text", e.getClass().getSimpleName());
        } catch (final Exception e) {
            r.put("code", "failed");
            r.put("text", e.getClass().getSimpleName());
        } finally {
            if (plugin != null) {
                plugin.clean();
            }
        }
        return r;
    }
}
