// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.nio.file.Files;
import java.util.Base64;
import java.util.List;
import java.util.Map;

import jd.plugins.CaptchaType.CAPTCHA_TYPE;

import org.jdownloader.captcha.v2.Challenge;
import org.jdownloader.captcha.v2.Challenge.CaptchaRequestType;
import org.jdownloader.captcha.v2.ChallengeResponseController;
import org.jdownloader.captcha.v2.ChallengeSolver;
import org.jdownloader.captcha.v2.SolverService;
import org.jdownloader.captcha.v2.solverjob.ResponseList;
import org.jdownloader.captcha.v2.solverjob.SolverJob;
import org.jdownloader.captcha.v2.tester.CaptchaTestChallengeFactory;
import org.jdownloader.plugins.controller.host.LazyHostPlugin;

/**
 * solveTest: one test puzzle through JD's captcha test path (CaptchaTestChallengeFactory +
 * handleTest), with the solver accounts solverAccounts added. Type "IMAGE" uses JD's own
 * test image unless the request carries one; reCAPTCHA and the like use JD's demo pages.
 * Answers `solved` {text, solver, expected}; error "noSolver" when no account takes the type,
 * "unsolved" when the solvers gave no answer.
 */
final class SolveTestTask extends Task {
    private final String solver;
    private final String type;
    private final String image;

    SolveTestTask(final long id, final String solver, final String type, final String image) {
        super(id);
        this.solver = solver;
        this.type = type;
        this.image = image;
    }

    @Override
    Thread newThread(final Runnable body) {
        return new Thread(body, "resolver-solve-" + id);
    }

    @SuppressWarnings({ "unchecked", "rawtypes" })
    @Override
    void run() throws Throwable {
        final CAPTCHA_TYPE t;
        try {
            t = CAPTCHA_TYPE.valueOf(type == null || type.isEmpty() ? "IMAGE" : type);
        } catch (final IllegalArgumentException e) {
            Out.error(id, "bad", "type");
            return;
        }
        final Map<String, Object> values = CaptchaTestChallengeFactory.getDefaults(t);
        if (values == null) {
            Out.error(id, "bad", "no test data for " + t.name());
            return;
        }
        if (image != null && !image.isEmpty()) {
            final java.io.File f = Files.createTempFile("emuleqt-solve", ".img").toFile();
            f.deleteOnExit();
            Files.write(f.toPath(), Base64.getDecoder().decode(image));
            values.put(CaptchaTestChallengeFactory.KEY_IMAGE_FILE, f);
            values.remove(CaptchaTestChallengeFactory.KEY_EXPECTED_RESULT);
        }
        SolverService service = null;
        if (solver != null && !solver.isEmpty()) {
            final LazyHostPlugin lazy = SolverAccountsTask.solverPlugin(solver);
            service = lazy == null ? null : ChallengeResponseController.getInstance().getServiceByID(lazy.getHost());
            if (service == null) {
                Out.error(id, "noSolver", solver);
                return;
            }
        }
        final Challenge c = CaptchaTestChallengeFactory.newChallenge(CaptchaRequestType.HOSTER, t, values);
        if (c.getPlugin() != null) {
            // the solver logs through the challenge's plugin: keys sit in request URLs
            c.getPlugin().setLogger(Jd.quietLog("solver"));
        }
        final SolverJob job = solve(c, service);
        if (job == null) {
            Out.error(id, "noSolver", solver);
            return;
        }
        final ResponseList res = job == null ? null : job.getResponse();
        final Object text = res == null ? null : res.getValue();
        if (text == null) {
            Out.error(id, "unsolved", null);
            return;
        }
        final Map<String, Object> m = Out.event(id, "solved");
        m.put("text", String.valueOf(text));
        final Object by = res.isEmpty() ? null : ((org.jdownloader.captcha.v2.AbstractResponse) res.get(0)).getSolver();
        if (by instanceof ChallengeSolver && ((ChallengeSolver) by).getService() != null) {
            m.put("solver", ((ChallengeSolver) by).getService().getID());
        }
        final Object expected = values.get(CaptchaTestChallengeFactory.KEY_EXPECTED_RESULT);
        if (expected != null) {
            m.put("expected", String.valueOf(expected));
        }
        Out.send(m);
    }

    /** External solvers only (not the user's puzzle prompt); null: none takes @p c. */
    private static <T> SolverJob<T> solve(final Challenge<T> c, final SolverService service) throws Exception {
        final ChallengeResponseController crc = ChallengeResponseController.getInstance();
        final List<ChallengeSolver<T>> solvers = crc.getTestSolvers(c, service);
        solvers.removeIf(s -> s.getService() == null || s.getService().getType() != ChallengeSolver.SolverType.EXTERNAL);
        return solvers.isEmpty() ? null : crc.handleTest(c, solvers);
    }
}
