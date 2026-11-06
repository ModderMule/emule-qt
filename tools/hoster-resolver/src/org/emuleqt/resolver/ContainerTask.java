// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;

import jd.controlling.linkcrawler.CrawledLink;
import jd.controlling.linkcrawler.PackageInfo;
import jd.plugins.ContainerStatus;
import jd.plugins.PluginsC;

import org.jdownloader.plugins.controller.container.ContainerPluginController;

/**
 * container: the links a link container file (DLC, RSDF, CCF) holds. Calls the container
 * plugin itself, not LinkCrawler: its status text says why it failed, and the links come
 * back as written, without a check of each (AddHosterLinks checks them).
 * DLC asks JD's dlcrypt service for the key: one request per file, rate limited.
 */
final class ContainerTask extends Task {
    private final String path;

    ContainerTask(final long id, final String path) {
        super(id);
        this.path = path;
    }

    @Override
    Thread newThread(final Runnable body) {
        return new Thread(body, "resolver-container-" + id);
    }

    @Override
    void run() throws Throwable {
        final File file = path == null ? null : new File(path);
        if (file == null || !file.isFile()) {
            Out.error(id, "io", "no such file");
            return;
        }
        final PluginsC plugin = pluginFor(file);
        if (plugin == null) {
            Out.error(id, "noPlugin", file.getName());
            return;
        }
        final ContainerStatus cs = plugin.callDecryption(file);
        if (cancelled.get()) {
            Out.send(id, "cancelled");
            return;
        }
        final List<CrawledLink> links = plugin.getContainedDownloadlinks();
        if (cs == null || !cs.isStatus(ContainerStatus.STATUS_FINISHED) || links.isEmpty()) {
            final String text = cs == null || cs.getStatusText() == null ? "" : cs.getStatusText();
            // D: "DLC Limit reached." when the service refuses for a while
            final String code = text.toLowerCase(Locale.ROOT).contains("limit") ? "limit"
                    : cs != null && cs.isStatus(ContainerStatus.STATUS_INVALID_PASSWORD) ? "password" : "invalid";
            Out.error(id, code, text);
            return;
        }
        final Map<String, Object> m = Out.event(id, "links");
        m.put("links", describe(links));
        Out.send(m);
    }

    /** The container plugin whose pattern takes the file, a fresh instance (keys are per instance). */
    private static PluginsC pluginFor(final File file) {
        final String url = file.toURI().toString();
        for (final PluginsC p : ContainerPluginController.getInstance().list()) {
            if (p.getSupportedLinks().matcher(url).matches()) {
                return p.newPluginInstance();
            }
        }
        return null;
    }

    /** {url, name?, package?, passwords?} per link. */
    private static List<Map<String, Object>> describe(final List<CrawledLink> links) {
        final List<Map<String, Object>> out = new ArrayList<Map<String, Object>>();
        for (final CrawledLink cl : links) {
            final String url = cl.getURL();
            if (url == null || url.isEmpty()) {
                continue;
            }
            final Map<String, Object> m = new java.util.LinkedHashMap<String, Object>();
            m.put("url", url);
            // the name the container wrote, not one guessed from the url
            final String name = cl._getName();
            if (name != null && !name.isEmpty() && !name.contains("<jd:")) {
                m.put("name", name);
            }
            final PackageInfo pi = cl.getDesiredPackageInfo();
            if (pi != null && pi.getName() != null && !pi.getName().isEmpty()) {
                m.put("package", pi.getName());
            }
            if (cl.hasArchiveInfo() && !cl.getArchiveInfo().getExtractionPasswords().isEmpty()) {
                m.put("passwords", new ArrayList<String>(cl.getArchiveInfo().getExtractionPasswords()));
            }
            out.add(m);
        }
        return out;
    }
}
