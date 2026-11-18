// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

import jd.plugins.Plugin;

import org.jdownloader.captcha.v2.Challenge;
import org.jdownloader.captcha.v2.ChallengeResponseController;
import org.jdownloader.captcha.v2.ChallengeSolver;
import org.jdownloader.captcha.v2.challenge.oauth.AccountOAuthSolver;
import org.jdownloader.captcha.v2.challenge.oauth.OAuthChallenge;
import org.jdownloader.captcha.v2.solver.service.DialogSolverService;
import org.jdownloader.captcha.v2.solverjob.SolverJob;

/**
 * Logins through a page (OAuth device flow, e.g. Real-Debrid), in place of JD's AccountOAuthSolver:
 * only an accountCheck the user started asks: a `login` event with the URL, the challenge's own
 * poll() (ChallengeResponseController, every second) sees the user allow it in the browser.
 * Never the plugin's auto login (form + captcha + mail code), like JD desktop: its headless code
 * form never took a code (RD, live 2026-11-18), each try mailed one and paid a captcha.
 * Anything else (the startup check, a download) fails at once: needsBrowser() tells the check.
 */
final class LoginSolver extends ChallengeSolver<Boolean> {
    private static final LoginSolver INSTANCE = new LoginSolver();
    /** plugin instance → the accountCheck task that runs it */
    private static final Map<Plugin, Long> CHECKS = new ConcurrentHashMap<Plugin, Long>();
    /** plugins whose login wanted a browser nobody was asked to open */
    private static final Set<Plugin> NO_BROWSER = ConcurrentHashMap.newKeySet();

    private LoginSolver() {
        super(DialogSolverService.getInstance(), 1);
    }

    @SuppressWarnings("unchecked")
    static void install() {
        try {
            final ChallengeResponseController crc = ChallengeResponseController.getInstance();
            // JD's would run the same auto login a 2nd time, its code ask on a thread nobody knows
            final Field f = ChallengeResponseController.class.getDeclaredField("solverList");
            f.setAccessible(true);
            ((List<ChallengeSolver<?>>) f.get(crc)).remove(AccountOAuthSolver.getInstance());
            final Method m = ChallengeResponseController.class.getDeclaredMethod("addSolver", ChallengeSolver.class);
            m.setAccessible(true);
            m.invoke(crc, INSTANCE);
        } catch (final Throwable e) {
            Out.log("login solver not installed: " + e.getClass().getSimpleName());
        }
    }

    /** @p plugin checks a login for @p task: a browser login it needs goes to the user. */
    static void checking(final Plugin plugin, final long task) {
        CHECKS.put(plugin, task);
    }

    /** @p plugin's login failed for a browser login only the user can do (Check in Options). */
    static boolean needsBrowser(final Plugin plugin) {
        return NO_BROWSER.contains(plugin);
    }

    static void done(final Plugin plugin) {
        CHECKS.remove(plugin);
        NO_BROWSER.remove(plugin);
    }

    @Override
    public ChallengeVetoReason getUnsupportedReason(final Challenge<?> c) {
        // no captcha type: like AccountOAuthSolver, super would veto
        return c instanceof OAuthChallenge ? ChallengeVetoReason.NONE : ChallengeVetoReason.UNSUPPORTED_BY_SOLVER;
    }

    @Override
    public Set<jd.plugins.CaptchaType.CAPTCHA_TYPE> getSupportedCaptchaTypes() {
        return EnumSet.noneOf(jd.plugins.CaptchaType.CAPTCHA_TYPE.class);
    }

    @Override
    public boolean isEnabled() {
        return true;
    }

    @Override
    public void solve(final SolverJob<Boolean> job) throws InterruptedException {
        if (!(job.getChallenge() instanceof OAuthChallenge)) {
            return;
        }
        final OAuthChallenge c = (OAuthChallenge) job.getChallenge();
        final Long task = c.getPlugin() == null ? null : CHECKS.get(c.getPlugin());
        if (task == null || c.getUrl() == null) {
            if (c.getPlugin() != null) {
                NO_BROWSER.add(c.getPlugin());
            }
            return;
        }
        final Map<String, Object> m = Out.event(task, "login");
        m.put("url", c.getUrl());
        m.put("host", c.getHost());
        final int remaining = c.getRemainingTimeout();
        m.put("timeout", remaining > 0 ? remaining / 1000 : 300);
        Out.send(m);
        // the challenge's poll() answers the job once the user allowed it
        while (!job.isSolved() && job.getSkipRequest() == null && CHECKS.containsKey(c.getPlugin())) {
            if (c.getRemainingTimeout() == 0) {
                break;
            }
            Thread.sleep(1000);
        }
    }
}
