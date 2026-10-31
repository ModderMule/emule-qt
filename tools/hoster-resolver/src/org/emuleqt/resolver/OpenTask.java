// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.io.File;
import java.util.List;
import java.util.Map;

import jd.controlling.downloadcontroller.AccountCache.CachedAccount;
import jd.controlling.downloadcontroller.DownloadLinkCandidate;
import jd.controlling.downloadcontroller.DownloadWatchDog;
import jd.controlling.downloadcontroller.SingleDownloadController;
import jd.plugins.DownloadLink;
import jd.plugins.FilePackage;
import jd.plugins.PluginForHost;
import jd.plugins.PluginProgress;
import jd.plugins.download.DownloadInterface;

import org.jdownloader.plugins.CaptchaStepProgress;
import org.jdownloader.plugins.SleepPluginProgress;

/**
 * open: the hoster's file into <path>/data.part, the way JD writes it (way A of note §5).
 * The daemon reads the part file as it grows and verifies it under the root.
 */
final class OpenTask extends Task {
    /** Fixed file name inside the task dir, so the daemon knows the part file up front. */
    static final String DATA = "data";

    private final String url;
    private final Account account;
    private final File dir;
    private final long have;
    private static final Map<DownloadLink, Long> BY_LINK = new java.util.concurrent.ConcurrentHashMap<DownloadLink, Long>();
    private volatile Driver driver;
    private volatile DownloadLink link;
    private String lastStatus;

    /** The open task that downloads @p dl, for a puzzle it raises. */
    static Long taskOfLink(final DownloadLink dl) {
        return BY_LINK.get(dl);
    }

    OpenTask(final long id, final String url, final Account account, final String path, final long have) {
        super(id);
        this.url = url;
        this.account = account;
        this.dir = new File(path);
        this.have = Math.max(0, have);
    }

    @Override
    Thread newThread(final Runnable body) {
        // crawl on a plain thread; the download itself runs on Driver, a SingleDownloadController
        return new Thread(body, "resolver-open-" + id);
    }

    @Override
    void run() throws Throwable {
        status("link");
        final List<DownloadLink> links = Jd.crawl(url);
        if (cancelled.get()) {
            Out.send(id, "cancelled");
            return;
        }
        if (links.size() != 1) {
            if (links.isEmpty()) {
                Out.send(id, "gone");
            } else {
                Out.error(id, "folder", null);
            }
            return;
        }
        link = links.get(0);
        BY_LINK.put(link, id);
        try {
            download();
        } finally {
            BY_LINK.remove(link);
        }
    }

    private void download() throws Throwable {
        dir.mkdirs();
        final FilePackage fp = FilePackage.getInstance();
        fp.setDownloadDirectory(dir.getAbsolutePath());
        fp.add(link);
        link.setInternalTmpFilename(DATA);
        link.setChunks(1);
        // have: the daemon wrote the front of data.part; JD resumes from the last byte index
        final File part = new File(dir, DATA + ".part");
        if (have > 0 && part.length() >= have) {
            link.setChunksProgress(new long[] { have - 1 });
        } else {
            link.setChunksProgress(null);
        }
        final PluginForHost proto = link.getDefaultPlugin();
        final jd.plugins.Account acc = account == null ? null : account.toJd(proto.getHost());
        final CachedAccount ca = new CachedAccount(proto.getHost(), acc, proto);
        final DownloadLinkCandidate cand = new DownloadLinkCandidate(link, true, ca, null, false);
        final Driver d = new Driver(cand, acc);
        driver = d;
        link.setDownloadLinkController(d);
        d.setSessionDownloadDirectory(dir.getAbsolutePath());
        d.setDaemon(true);
        d.start();
        long lastBytes = -1;
        String lastWait = null;
        // have > 0: where the bytes start, once JD decided (resumed from have, or from 0)
        Long from = have > 0 ? null : Long.valueOf(0);
        boolean fromSent = have == 0;
        boolean told = false;
        while (d.isAlive()) {
            d.join(500);
            final DownloadInterface di = d.downloadInterface();
            if (di != null && !told) {
                told = true;
                Out.log("download interface " + di.getClass().getSimpleName());
            }
            if (from == null && di != null) {
                final long[] cp = link.getChunksProgress();
                if (di.isResumedDownload()) {
                    // JD may start up to 1 KiB before have; from have on the bytes are new
                    from = Long.valueOf(have);
                } else if (cp != null && cp.length == 1 && cp[0] + 1 + 4096 < have) {
                    // a virgin start reset the progress: the hoster cannot resume
                    from = Long.valueOf(0);
                }
            }
            final long bytes = loaded(part);
            if ((bytes != lastBytes && bytes >= 0) || (from != null && !fromSent)) {
                lastBytes = bytes;
                final Map<String, Object> m = Out.event(id, "progress");
                m.put("bytes", bytes);
                if (from != null && !fromSent) {
                    m.put("from", from);
                    fromSent = true;
                }
                final long total = link.getVerifiedFileSize() > 0 ? link.getVerifiedFileSize() : link.getKnownDownloadSize();
                if (total > 0) {
                    m.put("total", total);
                }
                m.put("path", part.getAbsolutePath());
                Out.send(m);
            }
            final PluginProgress pp = link.getPluginProgress();
            final String wait = pp instanceof SleepPluginProgress ? String.valueOf(pp.getCurrent() / 1000) : null;
            if (wait != null && !wait.equals(lastWait)) {
                final Map<String, Object> m = Out.event(id, "wait");
                m.put("seconds", pp.getCurrent() / 1000);
                m.put("reason", "timer");
                Out.send(m);
            }
            lastWait = wait;
            if (wait == null) {
                status(step(pp, di, acc != null, bytes));
            }
        }
        link.setDownloadLinkController(null);
        if (cancelled.get()) {
            Out.send(id, "cancelled");
            return;
        }
        if (d.failure != null) {
            Out.send(Jd.failure(id, d.failure));
            return;
        }
        // JD renames data.part to data on success
        final File done = new File(dir, DATA);
        final File out = done.isFile() ? done : part;
        final Map<String, Object> m = Out.event(id, "done");
        m.put("size", out.length());
        m.put("path", out.getAbsolutePath());
        if (link.getName() != null) {
            m.put("name", link.getName());
        }
        m.put("resumed", d.resumedHere);
        if (have > 0) {
            m.put("from", d.resumedHere ? have : 0L);
        }
        CheckTask.putHash(m, link.getHashInfo());
        Out.send(m);
    }

    /**
     * What the plugin does before bytes flow, for the GUI's status: "hoster", "hosterAccount" (pages,
     * a login), "puzzle", "connect" (the transfer opens), "transfer" (bytes come).
     */
    private String step(final PluginProgress pp, final DownloadInterface di, final boolean account, final long bytes) {
        if (pp instanceof CaptchaStepProgress) {
            return "puzzle";
        }
        if (di == null) {
            return account ? "hosterAccount" : "hoster";
        }
        return bytes > have ? "transfer" : "connect";
    }

    /** Sends a status when it changed. */
    private void status(final String text) {
        if (text.equals(lastStatus)) {
            return;
        }
        lastStatus = text;
        final Map<String, Object> m = Out.event(id, "status");
        m.put("text", text);
        Out.send(m);
    }

    @Override
    void onCancel() {
        final Driver d = driver;
        if (d != null) {
            d.stopNow();
        }
    }

    /** Bytes on disk from the front: JD's last loaded index + 1, else the part file size. */
    private long loaded(final File part) {
        final DownloadLink l = link;
        final long[] cp = l == null ? null : l.getChunksProgress();
        if (cp != null && cp.length == 1 && cp[0] >= 0) {
            return Math.min(cp[0] + 1, part.length());
        }
        return part.isFile() ? part.length() : 0;
    }

    /**
     * SingleDownloadController with the steps of its private download() (321-462), without the
     * watchdog's queue, the download list and the link collector (note §6).
     */
    private static final class Driver extends SingleDownloadController {
        private final jd.plugins.Account acc;
        private final java.util.concurrent.atomic.AtomicBoolean abort = new java.util.concurrent.atomic.AtomicBoolean(false);
        private volatile PluginForHost plugin;
        volatile Throwable failure;
        volatile boolean resumedHere;

        Driver(final DownloadLinkCandidate cand, final jd.plugins.Account acc) {
            super(cand, DownloadWatchDog.getInstance());
            this.acc = acc;
        }

        @Override
        public PluginForHost getProcessingPlugin() {
            return plugin;
        }

        @Override
        public boolean isAborting() {
            return abort.get();
        }

        @Override
        public boolean isActive() {
            return plugin != null;
        }

        DownloadInterface downloadInterface() {
            final PluginForHost p = plugin;
            return p == null ? null : p.getDownloadInterface();
        }

        void stopNow() {
            abort.set(true);
            final PluginForHost p = plugin;
            final DownloadInterface di = p == null ? null : p.getDownloadInterface();
            if (di != null) {
                try {
                    di.stopDownload();
                } catch (final Throwable ignore) {
                }
            }
            interrupt();
        }

        @Override
        public void run() {
            final DownloadLink dl = getDownloadLink();
            PluginForHost p = null;
            try {
                p = Jd.newPlugin(dl);
                plugin = p;
                dl.setLivePlugin(p);
                p.handle(dl, acc);
                final DownloadInterface di = p.getDownloadInterface();
                resumedHere = di != null && di.isResumedDownload();
            } catch (final Throwable e) {
                failure = e;
            } finally {
                dl.setLivePlugin(null);
                if (p != null) {
                    try {
                        p.clean();
                    } catch (final Throwable ignore) {
                    }
                }
                plugin = null;
            }
        }
    }
}
