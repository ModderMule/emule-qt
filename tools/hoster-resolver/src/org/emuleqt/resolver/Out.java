// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.io.OutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

import org.appwork.storage.JSonStorage;

/** One JSON object per line on the original stdout. JD's own prints go to stderr. */
final class Out {
    private static PrintStream proto;

    private Out() {
    }

    /** Take stdout for the protocol; everything else that prints lands on stderr. */
    static void claimStdout() {
        final OutputStream raw = System.out;
        proto = new PrintStream(raw, false, StandardCharsets.UTF_8);
        System.setOut(System.err);
    }

    static Map<String, Object> event(final long id, final String ev) {
        final Map<String, Object> m = new LinkedHashMap<String, Object>();
        m.put("id", id);
        m.put("ev", ev);
        return m;
    }

    static void send(final Map<String, Object> m) {
        final String line = JSonStorage.serializeToJson(m).replace('\n', ' ');
        synchronized (Out.class) {
            proto.print(line);
            proto.print('\n');
            proto.flush();
        }
    }

    static void send(final long id, final String ev) {
        send(event(id, ev));
    }

    static void error(final long id, final String code, final String text) {
        final Map<String, Object> m = event(id, "error");
        m.put("code", code);
        if (text != null) {
            m.put("text", text);
        }
        send(m);
    }

    /** stderr log line; never pass account data here. */
    static void log(final String s) {
        System.err.println("resolver: " + s);
    }
}
