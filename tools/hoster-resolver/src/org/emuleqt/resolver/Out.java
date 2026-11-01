// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.net.InetAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

import org.appwork.storage.JSonStorage;

/**
 * One JSON object per line over a loopback socket to the daemon. stdout and stderr are JD's log;
 * System.out is pointed at stderr so nothing JD prints is mixed with the driver's own output.
 */
final class Out {
    private static Socket socket;
    private static Writer proto;

    private Out() {
    }

    /** Connects to the daemon and authenticates with the token it gave on stdin. */
    static void connect(final int port, final String token) throws IOException {
        System.setOut(System.err);
        socket = new Socket(InetAddress.getLoopbackAddress(), port);
        socket.setTcpNoDelay(true);
        proto = new BufferedWriter(new OutputStreamWriter(socket.getOutputStream(), StandardCharsets.UTF_8));
        final Map<String, Object> m = new LinkedHashMap<String, Object>();
        m.put("ev", "auth");
        m.put("token", token);
        send(m);
    }

    /** The daemon's requests, one per line; ends when the daemon closes or dies. */
    static BufferedReader input() throws IOException {
        return new BufferedReader(new InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8));
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
            try {
                proto.write(line);
                proto.write('\n');
                proto.flush();
            } catch (final IOException e) {
                // the daemon is gone: the read loop ends on EOF and exits
            }
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
