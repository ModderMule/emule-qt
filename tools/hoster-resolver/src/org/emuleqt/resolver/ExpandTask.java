// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.util.List;
import java.util.Map;

import jd.plugins.DownloadLink;

/** expand: the links a folder or list link stands for. LinkCrawler runs its own threads. */
final class ExpandTask extends Task {
    private final String url;

    ExpandTask(final long id, final String url) {
        super(id);
        this.url = url;
    }

    @Override
    Thread newThread(final Runnable body) {
        return new Thread(body, "resolver-expand-" + id);
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
        final Map<String, Object> m = Out.event(id, "links");
        m.put("links", ResolverMain.describe(links));
        Out.send(m);
    }
}
