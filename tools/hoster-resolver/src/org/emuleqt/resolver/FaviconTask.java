// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.awt.Image;
import java.awt.RenderingHints;
import java.awt.image.BufferedImage;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collection;
import java.util.List;
import java.util.Map;

import javax.imageio.ImageIO;
import javax.swing.Icon;

import jd.plugins.PluginForHost;

import org.appwork.utils.images.IconIO;
import org.jdownloader.plugins.controller.LazyPlugin;
import org.jdownloader.plugins.controller.host.LazyHostPlugin;

/**
 * favicon: a host's icon the way JD's FavIcons loads it (the plugin's own getFavIcon first,
 * then the site's icon tags and /favicon.ico, parent domains last), scaled to at most
 * kMaxSide px. Answers `favicon` {host, image: base64 PNG}; error "none" when there is none.
 */
final class FaviconTask extends Task {
    /** sharp at 16 px on a 2x screen */
    private static final int kMaxSide = 32;
    private static final int kQuickTimeoutMs = 4000;
    private final String host;

    FaviconTask(final long id, final String host) {
        super(id);
        this.host = host == null ? "" : host.trim().toLowerCase(java.util.Locale.ROOT);
    }

    @Override
    Thread newThread(final Runnable body) {
        return new Thread(body, "resolver-favicon-" + id);
    }

    @Override
    void run() throws Throwable {
        if (host.isEmpty() || host.indexOf('.') <= 0) {
            Out.error(id, "bad", "host");
            return;
        }
        final org.appwork.utils.logging2.LogSource log = Jd.quietLog("favicon");
        final List<String> tryHosts = new ArrayList<String>();
        BufferedImage icon = null;
        String[] names = null;
        final LazyHostPlugin lazy = Jd.hostPlugin(host);
        if (lazy != null && lazy.hasFeature(LazyPlugin.FEATURE.FAVICON)) {
            try {
                final PluginForHost p = lazy.newInstance(null, false);
                p.setLogger(log);
                names = p.siteSupportedNames();
                final Object r = p.getFavIcon(host);
                if (r instanceof Icon) {
                    icon = IconIO.toBufferedImage((Icon) r);
                } else if (r instanceof Image) {
                    icon = IconIO.toBufferedImage((Image) r);
                } else if (r instanceof String) {
                    tryHosts.add((String) r);
                } else if (r instanceof Collection) {
                    for (final Object o : (Collection<?>) r) {
                        if (o instanceof String && !tryHosts.contains(o)) {
                            tryHosts.add((String) o);
                        }
                    }
                }
            } catch (final Exception e) {
                // the site's own icon below
            }
        }
        if (icon == null) {
            if (lazy != null && !tryHosts.contains(lazy.getHost())) {
                tryHosts.add(lazy.getHost());
            }
            if (!tryHosts.contains(host)) {
                tryHosts.add(host);
            }
            // sub.example.com -> example.com, as JD's addParentDomains
            for (String d = host; d.indexOf('.') != d.lastIndexOf('.');) {
                d = d.substring(d.indexOf('.') + 1);
                if (!tryHosts.contains(d)) {
                    tryHosts.add(d);
                }
            }
            if (cancelled.get()) {
                Out.send(id, "cancelled");
                return;
            }
            // most sites: /favicon.ico at once; JD loads the page first (slow, sleeps on redirects)
            icon = quickIcon(log, tryHosts.get(0));
            if (icon == null && !cancelled.get()) {
                icon = jd.controlling.faviconcontroller.FavIcons.downloadFavIcon(log, tryHosts, names);
            }
        }
        if (icon == null || icon.getWidth() < 2 || icon.getHeight() < 2) {
            Out.error(id, "none", host);
            return;
        }
        final ByteArrayOutputStream png = new ByteArrayOutputStream();
        ImageIO.write(scaled(icon), "png", png);
        final Map<String, Object> m = Out.event(id, "favicon");
        m.put("host", host);
        m.put("image", Base64.getEncoder().encodeToString(png.toByteArray()));
        Out.send(m);
    }

    /** https://host/favicon.ico with short timeouts; null: JD's way. */
    private static BufferedImage quickIcon(final org.appwork.utils.logging2.LogSource log, final String host) {
        try {
            final jd.http.Browser br = new jd.http.Browser();
            br.setLogger(log);
            br.setConnectTimeout(kQuickTimeoutMs);
            br.setReadTimeout(kQuickTimeoutMs);
            br.getHeaders().put("User-Agent", "Mozilla/5.0 (X11; Linux x86_64; rv:145.0) Gecko/20100101 Firefox/145.0");
            final BufferedImage img = jd.controlling.faviconcontroller.FavIcons.download_FavIconTag(br, "https://" + host + "/favicon.ico", host);
            return img != null && img.getWidth() >= 8 && img.getHeight() >= 8 ? img : null;
        } catch (final Throwable e) {
            return null;
        }
    }

    private static BufferedImage scaled(final BufferedImage src) {
        final int w = src.getWidth();
        final int h = src.getHeight();
        if (w <= kMaxSide && h <= kMaxSide) {
            return src;
        }
        final double f = Math.min((double) kMaxSide / w, (double) kMaxSide / h);
        final int nw = Math.max(1, (int) Math.round(w * f));
        final int nh = Math.max(1, (int) Math.round(h * f));
        final BufferedImage out = new BufferedImage(nw, nh, BufferedImage.TYPE_INT_ARGB);
        final java.awt.Graphics2D g = out.createGraphics();
        try {
            g.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BICUBIC);
            g.setRenderingHint(RenderingHints.KEY_RENDERING, RenderingHints.VALUE_RENDER_QUALITY);
            g.drawImage(src, 0, 0, nw, nh, null);
        } finally {
            g.dispose();
        }
        return out;
    }
}
