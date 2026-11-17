// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.lang.reflect.Method;
import java.util.EnumSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

import jd.plugins.Plugin;

import org.jdownloader.captcha.v2.Challenge;
import org.jdownloader.captcha.v2.ChallengeResponseController;
import org.jdownloader.captcha.v2.ChallengeSolver;
import org.jdownloader.captcha.v2.challenge.oauth.OAuthChallenge;
import org.jdownloader.captcha.v2.solver.service.DialogSolverService;
import org.jdownloader.captcha.v2.solverjob.SolverJob;

/**
 * Browser logins (OAuth device flow, e.g. Real-Debrid): what JD's OAuthDialogSolver does headless.
 * Only an accountCheck asks the user: a `login` event with the URL, then the challenge's own
 * poll() (ChallengeResponseController, every second) sees the user allow it in the browser.
 * Downloads never wait for one: their login fails and the next mirror / multi-hoster goes.
 */
final class LoginSolver extends ChallengeSolver<Boolean> {
    private static final LoginSolver INSTANCE = new LoginSolver();
    /** plugin instance → the accountCheck task that runs it */
    private static final Map<Plugin, Long> CHECKS = new ConcurrentHashMap<Plugin, Long>();

    private LoginSolver() {
        super(DialogSolverService.getInstance(), 1);
    }

    static void install() {
        try {
            final Method m = ChallengeResponseController.class.getDeclaredMethod("addSolver", ChallengeSolver.class);
            m.setAccessible(true);
            m.invoke(ChallengeResponseController.getInstance(), INSTANCE);
        } catch (final Throwable e) {
            Out.log("login solver not installed: " + e.getClass().getSimpleName());
        }
    }

    /** @p plugin checks a login for @p task: a browser login it needs goes to the user. */
    static void checking(final Plugin plugin, final long task) {
        CHECKS.put(plugin, task);
    }

    static void done(final Plugin plugin) {
        CHECKS.remove(plugin);
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
            final int left = c.getRemainingTimeout();
            if (left == 0) {
                break;
            }
            Thread.sleep(1000);
        }
    }
}
