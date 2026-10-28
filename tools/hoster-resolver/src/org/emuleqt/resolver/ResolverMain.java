// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import jd.plugins.DownloadLink;

import org.appwork.storage.JSonStorage;
import org.appwork.storage.TypeRef;

/**
 * Entry point. Reads one JSON request per line from stdin, answers with events on stdout
 * (hoster-resolver.local.md §4). Ends when stdin closes, so it dies with the daemon.
 */
public final class ResolverMain {
    /** Bumped when a request or event changes shape. */
    static final int PROTOCOL = 1;

    /** Running tasks by request id, for cancel and answer. */
    static final Map<Long, Task> TASKS = new ConcurrentHashMap<Long, Task>();

    private ResolverMain() {
    }

    public static void main(final String[] args) throws Exception {
        Out.claimStdout();
        // JD runs from loose classes here, which AppWorkUtils takes for an IDE: that would pick
        // the new HTTPDownloader (no resume from chunksProgress) and IDE-only checks. Be the
        // release build. Must run before BrowserAdapter loads (its NEW_CORE is final).
        org.appwork.utils.DebugMode.TRUE_IN_IDE_ELSE_FALSE = false;
        Jd.init();
        PuzzleSolver.install();
        Out.log("ready, " + Jd.hostPluginCount() + " host and " + Jd.crawlerPluginCount() + " crawler plugins");
        final BufferedReader in = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8));
        String line;
        while ((line = in.readLine()) != null) {
            if (line.isBlank()) {
                continue;
            }
            Map<String, Object> req;
            try {
                req = JSonStorage.restoreFromString(line, TypeRef.MAP);
            } catch (final Throwable e) {
                Out.log("bad request line");
                continue;
            }
            try {
                dispatch(req);
            } catch (final Throwable e) {
                Out.error(num(req.get("id")), "defect", e.getClass().getSimpleName());
            }
        }
        for (final Task t : TASKS.values()) {
            t.cancel();
        }
        System.exit(0);
    }

    static long num(final Object o) {
        return o instanceof Number ? ((Number) o).longValue() : 0;
    }

    static String str(final Object o) {
        return o == null ? null : o.toString();
    }

    private static void dispatch(final Map<String, Object> req) {
        final long id = num(req.get("id"));
        final String op = str(req.get("op"));
        if (op == null) {
            Out.error(id, "bad", "no op");
            return;
        }
        switch (op) {
        case "hello": {
            final Map<String, Object> m = Out.event(id, "hello");
            m.put("protocol", PROTOCOL);
            m.put("jdRevision", str(System.getProperty("emuleqt.jdRevision", "?")));
            m.put("hosts", Jd.hostPluginCount());
            m.put("crawlers", Jd.crawlerPluginCount());
            Out.send(m);
            break;
        }
        case "setProxy":
            Jd.setProxy(str(req.get("url")));
            Out.send(id, "ok");
            break;
        case "setPuzzles":
            PuzzleSolver.setAsk(!"never".equals(str(req.get("mode"))));
            Out.send(id, "ok");
            break;
        case "match": {
            final Map<String, Object> m = Out.event(id, "match");
            final String host = Jd.match(str(req.get("url")));
            if (host != null) {
                m.put("host", host);
            }
            Out.send(m);
            break;
        }
        case "check":
            start(new CheckTask(id, str(req.get("url")), Account.of(req)));
            break;
        case "expand":
            start(new ExpandTask(id, str(req.get("url"))));
            break;
        case "open":
            start(new OpenTask(id, str(req.get("url")), Account.of(req), str(req.get("path")), num(req.get("have"))));
            break;
        case "accountCheck":
            start(new AccountTask(id, str(req.get("host")), Account.of(req)));
            break;
        case "answer":
            PuzzleSolver.answer(num(req.get("puzzle")), str(req.get("text")));
            break;
        case "cancel": {
            final Task t = TASKS.get(num(req.get("task")));
            if (t != null) {
                t.cancel();
            } else {
                Out.send(num(req.get("task")), "cancelled");
            }
            break;
        }
        default:
            Out.error(id, "bad", "unknown op");
        }
    }

    private static void start(final Task t) {
        TASKS.put(t.id, t);
        t.begin();
    }

    /** Links as events: {url, name?, size?}. */
    static List<Map<String, Object>> describe(final List<DownloadLink> links) {
        final List<Map<String, Object>> out = new ArrayList<Map<String, Object>>();
        for (final DownloadLink dl : links) {
            final Map<String, Object> m = new java.util.LinkedHashMap<String, Object>();
            m.put("url", dl.getPluginPatternMatcher());
            if (dl.getName() != null) {
                m.put("name", dl.getName());
            }
            if (dl.getKnownDownloadSize() > 0) {
                m.put("size", dl.getKnownDownloadSize());
            }
            m.put("host", dl.getHost());
            out.add(m);
        }
        return out;
    }
}
