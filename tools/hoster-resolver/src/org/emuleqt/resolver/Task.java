// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.util.concurrent.atomic.AtomicBoolean;

/** One request that runs on a thread of its own. */
abstract class Task {
    final long id;
    final AtomicBoolean cancelled = new AtomicBoolean(false);
    private volatile Thread thread;

    Task(final long id) {
        this.id = id;
    }

    /** The thread type matters to JD plugins (Plugin.PluginEnvironment). */
    abstract Thread newThread(Runnable body);

    abstract void run() throws Throwable;

    void begin() {
        thread = newThread(() -> {
            try {
                run();
            } catch (final Throwable e) {
                Out.send(cancelled.get() ? Out.event(id, "cancelled") : Jd.failure(id, e));
            } finally {
                ResolverMain.TASKS.remove(id);
            }
        });
        thread.setDaemon(true);
        thread.start();
    }

    void cancel() {
        if (cancelled.compareAndSet(false, true)) {
            onCancel();
            final Thread t = thread;
            if (t != null) {
                t.interrupt();
            }
        }
    }

    /** Extra stop work, e.g. stopping a transfer. */
    void onCancel() {
    }
}
