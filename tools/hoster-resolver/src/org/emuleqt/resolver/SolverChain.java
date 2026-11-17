// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

import jd.controlling.AccountController;
import jd.controlling.AccountFilter;
import jd.controlling.accountchecker.AccountCheckerThread;
import jd.controlling.captcha.CaptchaSettings;
import jd.plugins.AccountInfo;

import org.appwork.storage.config.JsonConfig;
import org.jdownloader.captcha.event.ChallengeResponseListener;
import org.jdownloader.captcha.v2.AbstractResponse;
import org.jdownloader.captcha.v2.Challenge;
import org.jdownloader.captcha.v2.ChallengeResponseController;
import org.jdownloader.captcha.v2.ChallengeSolver;
import org.jdownloader.captcha.v2.PluginChallengeSolver;
import org.jdownloader.captcha.v2.SolverService;
import org.jdownloader.captcha.v2.solverjob.SolverJob;
import org.jdownloader.captcha.v2.solverjob.SolverJob.SolverOutcome;
import org.jdownloader.captcha.v2.solverjob.SolverJob.SolverResult;
import org.jdownloader.plugins.components.captchasolver.abstractPluginForCaptchaSolver;
import org.jdownloader.plugins.controller.LazyPlugin;

/**
 * Paid captcha solvers first, the user last. JD ranks its dialog type (our PuzzleSolver) ahead of
 * external solvers, so the user saw every puzzle while the paid accounts idled. Here: services that
 * need the user move to the end, accounts are re-checked after use (balance, login) so an empty or
 * broken one is vetoed next time, and each step goes to the hoster log.
 */
final class SolverChain implements ChallengeResponseListener {
    /** accounts with a balance re-check running */
    private static final Set<String> REFRESHING = ConcurrentHashMap.newKeySet();

    private SolverChain() {
    }

    /** After PuzzleSolver.install(): every solver service exists then. */
    static void install() {
        final ChallengeResponseController crc = ChallengeResponseController.getInstance();
        final List<SolverService> order = new ArrayList<SolverService>();
        final List<SolverService> user = new ArrayList<SolverService>();
        for (final SolverService s : crc.getOrderedServices()) {
            (s.getType().requiresUserOnThisComputer() ? user : order).add(s);
        }
        order.addAll(user);
        crc.setServiceOrder(order);
        // login captchas: JD would ask the user first there too
        JsonConfig.create(CaptchaSettings.class).setAvoidAutoSolverForLoginCaptchas(false);
        crc.getEventSender().addListener(new SolverChain());
    }

    /** True for solvers that work without the user (paid services, JD's local OCR). */
    static boolean automatic(final ChallengeSolver<?> s) {
        return !s.getSolverType().requiresUserOnThisComputer();
    }

    /** "anti-captcha.com (1.23 USD)" */
    static String label(final ChallengeSolver<?> s) {
        if (!(s instanceof PluginChallengeSolver)) {
            return s.getService().getName();
        }
        final jd.plugins.Account a = ((PluginChallengeSolver<?>) s).getAccount();
        return a.getHoster() + balance(a.getAccountInfo());
    }

    @Override
    public void onNewJob(final SolverJob<?> job) {
        final Challenge<?> c = job.getChallenge();
        final List<String> used = new ArrayList<String>();
        for (final ChallengeSolver<?> s : job.getSolverList()) {
            if (s instanceof PluginChallengeSolver) {
                used.add(label(s));
            }
        }
        final List<jd.plugins.Account> accounts = solverAccounts();
        if (accounts.isEmpty()) {
            Out.info("captcha for " + c.getHost() + ": no working captcha solver, asking you");
            return;
        }
        // accounts JD left out, and why
        for (final jd.plugins.Account a : accounts) {
            if (!inJob(job, a)) {
                Out.warn("captcha for " + c.getHost() + ": solver " + a.getHoster() + " skipped: " + vetoOf(c, a));
            }
        }
        if (used.isEmpty()) {
            Out.warn("captcha for " + c.getHost() + ": no captcha solver usable, asking you");
        } else {
            Out.info("captcha for " + c.getHost() + ": trying " + String.join(", ", used));
        }
    }

    @Override
    public void onJobSolverStart(final ChallengeSolver<?> solver, final SolverJob<?> job) {
        if (solver instanceof PluginChallengeSolver) {
            Out.log("captcha for " + job.getChallenge().getHost() + ": " + label(solver) + " started");
        }
    }

    @Override
    public void onJobSolverEnd(final ChallengeSolver<?> solver, final SolverJob<?> job) {
        if (!(solver instanceof PluginChallengeSolver)) {
            return;
        }
        final String host = job.getChallenge().getHost();
        final jd.plugins.Account a = ((PluginChallengeSolver<?>) solver).getAccount();
        final SolverResult r = job.getSolverResult(solver);
        final SolverOutcome o = r == null ? null : r.getOutcome();
        if (o == SolverOutcome.ANSWERED) {
            Out.info("captcha for " + host + ": solved by " + a.getHoster());
        } else if (job.isSolved() && (o == null || o == SolverOutcome.ABORTED || o == SolverOutcome.NOT_STARTED)) {
            Out.log("captcha for " + host + ": " + a.getHoster() + " not needed, solved by another");
            return;
        } else {
            final String why = (o == null ? "no answer" : o.name().toLowerCase(Locale.ROOT).replace('_', ' '))
                               + (r != null && r.getMessage() != null ? " (" + r.getMessage() + ")" : "");
            Out.warn("captcha for " + host + ": " + a.getHoster() + " failed: " + why + (job.isDone() ? "" : ", trying next"));
        }
        // the balance changed, or the account broke: next puzzle sees the new state
        if (o != SolverOutcome.NOT_STARTED && o != SolverOutcome.ABORTED) {
            refresh(a);
        }
    }

    @Override
    public void onJobDone(final SolverJob<?> job) {
    }

    @Override
    public void onNewJobAnswer(final SolverJob<?> job, final AbstractResponse<?> response) {
    }

    /** Re-reads balance and login of @p a in the background; a broken or empty one is vetoed by JD. */
    static void refresh(final jd.plugins.Account a) {
        final String key = a.getHoster() + "/" + a.getId();
        if (!REFRESHING.add(key)) {
            return;
        }
        final AccountCheckerThread t = new AccountCheckerThread() {
            @Override
            public void run() {
                try {
                    final AccountInfo ai = SolverAccountsTask.fetch(a);
                    if (ai == null || ai.isExpired()) {
                        a.setError(jd.plugins.Account.AccountError.INVALID, -1, "login");
                        Out.warn("captcha solver " + a.getHoster() + ": login refused, not used any more");
                        return;
                    }
                    a.setAccountInfo(ai);
                    if (ai.getAccountBalance() <= 0) {
                        Out.warn("captcha solver " + a.getHoster() + ": no balance left, not used any more");
                    } else {
                        Out.log("captcha solver " + a.getHoster() + ": balance" + balance(ai));
                    }
                } catch (final Throwable e) {
                    // service down: keep the account, JD tries it again next time
                    Out.log("captcha solver " + a.getHoster() + ": balance check failed: " + e.getClass().getSimpleName());
                } finally {
                    REFRESHING.remove(key);
                }
            }
        };
        t.setDaemon(true);
        t.setName("solver balance " + a.getHoster());
        t.start();
    }

    private static List<jd.plugins.Account> solverAccounts() {
        final List<jd.plugins.Account> l = AccountController.getInstance().listAccounts(new AccountFilter().setFeature(LazyPlugin.FEATURE.CAPTCHA_SOLVER));
        return l == null ? Collections.<jd.plugins.Account>emptyList() : l;
    }

    private static boolean inJob(final SolverJob<?> job, final jd.plugins.Account a) {
        for (final ChallengeSolver<?> s : job.getSolverList()) {
            if (s instanceof PluginChallengeSolver && ((PluginChallengeSolver<?>) s).getAccount() == a) {
                return true;
            }
        }
        return false;
    }

    /** Why JD did not use @p a for @p c, as log text. */
    @SuppressWarnings({ "unchecked", "rawtypes" })
    private static String vetoOf(final Challenge c, final jd.plugins.Account a) {
        if (a.getAccountInfo() == null) {
            return "not checked";
        }
        try {
            final abstractPluginForCaptchaSolver p = (abstractPluginForCaptchaSolver) a.getPlugin();
            final ChallengeSolver s = p == null ? null : p.getPluginChallengeSolver(c, a);
            if (s == null) {
                return "can't take this captcha type";
            }
            final Object v = s.getChallengeVetoReason(c);
            return String.valueOf(v).toLowerCase(Locale.ROOT).replace('_', ' ') + balance(a.getAccountInfo());
        } catch (final Throwable e) {
            return e.getClass().getSimpleName();
        }
    }

    /** " (1.23 USD)", empty without info */
    private static String balance(final AccountInfo ai) {
        if (ai == null) {
            return "";
        }
        final String cur = ai.getCurrency() == null ? "" : " " + ai.getCurrency().getCurrencyCode();
        return String.format(Locale.ROOT, " (%.2f%s)", ai.getAccountBalance(), cur);
    }
}
