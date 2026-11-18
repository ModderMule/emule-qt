// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.io.File;
import java.util.concurrent.TimeUnit;

import org.appwork.storage.config.JsonConfig;
import org.appwork.utils.logging2.LogInterface;
import org.jdownloader.controlling.ffmpeg.FFmpeg;
import org.jdownloader.controlling.ffmpeg.FFmpegSetup;
import org.jdownloader.controlling.ffmpeg.FFprobe;
import org.jdownloader.logging.LogController;
import org.jdownloader.plugins.SkipReason;
import org.jdownloader.plugins.SkipReasonException;

/**
 * ffmpeg + ffprobe for HLS/DASH plugins. JD looks in its FFmpegSetup config (default: /usr/bin,
 * /usr/local/bin, PATH) and, without JD's updater, skips with FFMPEG_MISSING. The daemon finds or
 * downloads them (Ed3kFfmpegTool): paths come with -Demuleqt.ffmpeg/ffprobe at start or with a
 * setTools request. A task that hits FFMPEG_MISSING asks the daemon (needTools) and waits for
 * the answer, then its download runs once more.
 */
final class Tools {
    /** A download of ~80 MB on a slow line, then the unpack */
    private static final long WAIT_MS = TimeUnit.MINUTES.toMillis(30);

    private static final Object LOCK = new Object();
    /** the daemon takes needTools (-Demuleqt.tools); an older one would never answer */
    private static boolean daemonFetches;
    /** a needTools is out, no answer yet */
    private static boolean asked;
    /** answers so far; a waiter sees a new one */
    private static long answers;
    private static String lastError;

    private Tools() {
    }

    /** At start: the daemon's paths over JD's saved ones. Before any plugin loads. */
    static void init() {
        daemonFetches = Boolean.getBoolean("emuleqt.tools");
        apply(System.getProperty("emuleqt.ffmpeg"), System.getProperty("emuleqt.ffprobe"));
    }

    /** setTools: new paths, or why there are none (@p error). Wakes the waiting tasks. */
    static void set(final String ffmpeg, final String ffprobe, final String error) {
        apply(ffmpeg, ffprobe);
        synchronized (LOCK) {
            asked = false;
            answers++;
            lastError = error == null || error.isEmpty() ? null : error;
            LOCK.notifyAll();
        }
    }

    /** Both binaries there (JD's own check, the one checkFFmpeg makes). */
    static boolean available() {
        try {
            final FFmpeg ffmpeg = new FFmpeg() {
                @Override
                public LogInterface getLogger() {
                    return LogController.CL();
                }
            };
            final FFprobe ffprobe = new FFprobe() {
                @Override
                public LogInterface getLogger() {
                    return LogController.CL();
                }
            };
            return ffmpeg.isAvailable() && ffprobe.isAvailable();
        } catch (final Throwable e) {
            return false;
        }
    }

    /** Whether @p t is JD's skip for a missing ffmpeg or ffprobe. */
    static boolean missing(final Throwable t) {
        if (!(t instanceof SkipReasonException)) {
            return false;
        }
        final SkipReason r = ((SkipReasonException) t).getSkipReason();
        return r == SkipReason.FFMPEG_MISSING || r == SkipReason.FFPROBE_MISSING;
    }

    /**
     * Asks the daemon for ffmpeg and waits (one ask for all tasks). True: both are there now, run
     * the download again. False: none come (@p task gets the reason from lastError()).
     */
    static boolean request(final Task task) throws InterruptedException {
        if (available()) {
            return true;   // came meanwhile
        }
        if (!daemonFetches) {
            return false;
        }
        final long seen;
        synchronized (LOCK) {
            seen = answers;
            if (!asked) {
                asked = true;
                final java.util.Map<String, Object> m = Out.event(0, "needTools");
                m.put("tools", java.util.Arrays.asList("ffmpeg", "ffprobe"));
                Out.send(m);
                Out.info("ffmpeg/ffprobe missing, asked the daemon for them");
            }
        }
        final long end = System.currentTimeMillis() + WAIT_MS;
        synchronized (LOCK) {
            while (answers == seen && !task.cancelled.get()) {
                final long left = end - System.currentTimeMillis();
                if (left <= 0) {
                    lastError = "no answer";
                    asked = false;
                    return false;
                }
                LOCK.wait(Math.min(left, 1000));
            }
        }
        return !task.cancelled.get() && available();
    }

    /** Why the last request brought nothing; null: unknown */
    static String lastError() {
        synchronized (LOCK) {
            return lastError;
        }
    }

    private static void apply(final String ffmpeg, final String ffprobe) {
        final FFmpegSetup cfg = JsonConfig.create(FFmpegSetup.class);
        if (isFile(ffmpeg)) {
            cfg.setBinaryPath(ffmpeg);
            Out.log("ffmpeg " + ffmpeg);
        } else if (!isFile(cfg.getBinaryPath())) {
            // a path saved by an earlier run that is gone: JD's own lookup again (PATH, /usr/bin)
            cfg.setBinaryPath(FFmpegSetup.DefaultFFMpegBinary.find(null, "ffmpeg"));
        }
        if (isFile(ffprobe)) {
            cfg.setBinaryPathProbe(ffprobe);
            Out.log("ffprobe " + ffprobe);
        } else if (!isFile(cfg.getBinaryPathProbe())) {
            cfg.setBinaryPathProbe(FFmpegSetup.DefaultFFMpegBinary.find(null, "ffprobe"));
        }
    }

    private static boolean isFile(final String path) {
        return path != null && !path.isEmpty() && new File(path).isFile();
    }
}
