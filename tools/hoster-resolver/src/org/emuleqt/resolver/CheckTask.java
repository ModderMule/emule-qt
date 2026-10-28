// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.util.List;
import java.util.Map;

import jd.controlling.linkchecker.LinkCheckerThread;
import jd.plugins.DownloadLink;
import jd.plugins.DownloadLink.AvailableStatus;
import jd.plugins.PluginForHost;
import jd.plugins.download.HashInfo;

/** check: is the file there, its name, size and a checksum the hoster tells. */
final class CheckTask extends Task {
    private final String url;
    private final Account account;

    CheckTask(final long id, final String url, final Account account) {
        super(id);
        this.url = url;
        this.account = account;
    }

    @Override
    Thread newThread(final Runnable body) {
        return new LinkCheckerThread() {
            @Override
            public void run() {
                body.run();
            }
        };
    }

    @Override
    void run() throws Throwable {
        final List<DownloadLink> links = Jd.crawl(url);
        if (cancelled.get()) {
            Out.send(id, "cancelled");
            return;
        }
        if (links.isEmpty()) {
            Out.send(id, "gone");
            return;
        }
        if (links.size() > 1) {
            // a folder: the daemon asks expand
            final Map<String, Object> m = Out.event(id, "folder");
            m.put("count", links.size());
            Out.send(m);
            return;
        }
        final DownloadLink link = links.get(0);
        final PluginForHost plugin = Jd.newPlugin(link);
        try {
            final AvailableStatus st = plugin.checkLink(link);
            if (st == AvailableStatus.FALSE) {
                Out.send(id, "gone");
                return;
            }
            Out.send(found(id, link));
        } finally {
            plugin.clean();
        }
    }

    static Map<String, Object> found(final long id, final DownloadLink link) {
        final Map<String, Object> m = Out.event(id, "found");
        m.put("url", link.getPluginPatternMatcher());
        m.put("host", link.getHost());
        if (link.getName() != null) {
            m.put("name", link.getName());
        }
        final long size = link.getVerifiedFileSize() > 0 ? link.getVerifiedFileSize() : link.getKnownDownloadSize();
        if (size > 0) {
            m.put("size", size);
        }
        putHash(m, link.getHashInfo());
        return m;
    }

    /** A checksum the hoster tells: checked, not verified (alias.md §5 rule 5). */
    static void putHash(final Map<String, Object> m, final HashInfo h) {
        if (h != null && h.getHash() != null && h.isTrustworthy()) {
            m.put("hashType", h.getType().name().toLowerCase(java.util.Locale.ROOT));
            m.put("hash", h.getHash());
        }
    }
}
