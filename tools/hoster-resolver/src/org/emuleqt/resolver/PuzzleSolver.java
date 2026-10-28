// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.lang.reflect.Method;
import java.nio.file.Files;
import java.util.Base64;
import java.util.EnumSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

import jd.plugins.Plugin;
import jd.plugins.PluginForHost;

import org.jdownloader.captcha.v2.Challenge;
import org.jdownloader.captcha.v2.ChallengeResponseController;
import org.jdownloader.captcha.v2.ChallengeSolver;
import org.jdownloader.captcha.v2.challenge.stringcaptcha.BasicCaptchaChallenge;
import org.jdownloader.captcha.v2.challenge.stringcaptcha.CaptchaResponse;
import org.jdownloader.captcha.v2.solver.service.DialogSolverService;
import org.jdownloader.captcha.v2.solverjob.SolverJob;

/**
 * Image puzzles go to the daemon as a `puzzle` event and wait for `answer`.
 * reCAPTCHA, hCaptcha and Turnstile need a browser; no solver takes them, so JD skips and
 * the task ends as needsUser{puzzle}.
 */
final class PuzzleSolver extends ChallengeSolver<String> {
    private static final PuzzleSolver INSTANCE = new PuzzleSolver();
    /** Wait for the user at most this long; the GUI shows the same countdown. */
    private static final long ANSWER_TIMEOUT_S = 300;
    private static final Map<Long, CompletableFuture<String>> PENDING = new ConcurrentHashMap<Long, CompletableFuture<String>>();
    private static volatile boolean ask = true;

    private PuzzleSolver() {
        super(DialogSolverService.getInstance(), 0);
    }

    /** Registers this solver next to JD's own headless ones (addSolver is private). */
    static void install() {
        final ChallengeResponseController crc = ChallengeResponseController.getInstance();
        crc.init();
        try {
            final Method m = ChallengeResponseController.class.getDeclaredMethod("addSolver", ChallengeSolver.class);
            m.setAccessible(true);
            m.invoke(crc, INSTANCE);
        } catch (final Throwable e) {
            Out.log("puzzle solver not installed: " + e.getClass().getSimpleName());
        }
    }

    /** "never": puzzles are skipped without asking. */
    static void setAsk(final boolean on) {
        ask = on;
    }

    static void answer(final long puzzle, final String text) {
        final CompletableFuture<String> f = PENDING.get(puzzle);
        if (f != null) {
            f.complete(text == null ? "" : text);
        }
    }

    @Override
    public Set<jd.plugins.CaptchaType.CAPTCHA_TYPE> getSupportedCaptchaTypes() {
        return EnumSet.of(jd.plugins.CaptchaType.CAPTCHA_TYPE.IMAGE);
    }

    @Override
    public boolean isEnabled() {
        return ask;
    }

    @Override
    public void solve(final SolverJob<String> job) throws InterruptedException {
        final Challenge<String> c = job.getChallenge();
        if (!(c instanceof BasicCaptchaChallenge)) {
            return;
        }
        final BasicCaptchaChallenge bc = (BasicCaptchaChallenge) c;
        final long pid = c.getId().getID();
        final CompletableFuture<String> f = new CompletableFuture<String>();
        PENDING.put(pid, f);
        try {
            final byte[] img = Files.readAllBytes(bc.getImageFile().toPath());
            final Map<String, Object> m = Out.event(taskOf(c), "puzzle");
            m.put("puzzle", pid);
            m.put("kind", "image");
            m.put("image", Base64.getEncoder().encodeToString(img));
            m.put("host", c.getHost());
            if (c.getExplain() != null) {
                m.put("explain", c.getExplain());
            }
            m.put("timeout", ANSWER_TIMEOUT_S);
            Out.send(m);
            final String text = f.get(ANSWER_TIMEOUT_S, TimeUnit.SECONDS);
            if (!text.isEmpty()) {
                job.addAnswer(new CaptchaResponse(bc, this, text));
            }
        } catch (final TimeoutException | java.util.concurrent.ExecutionException | java.io.IOException e) {
            // no answer: JD skips the puzzle
        } finally {
            PENDING.remove(pid);
        }
    }

    /** The task whose download asked, else 0. */
    private static long taskOf(final Challenge<?> c) {
        final Plugin p = c.getPlugin();
        if (p instanceof PluginForHost) {
            final jd.plugins.DownloadLink dl = ((PluginForHost) p).getDownloadLink();
            final Long id = dl == null ? null : OpenTask.taskOfLink(dl);
            if (id != null) {
                return id;
            }
        }
        return 0;
    }
}
