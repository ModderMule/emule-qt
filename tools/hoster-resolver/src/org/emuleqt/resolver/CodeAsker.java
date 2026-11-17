// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Map;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;

import javax.swing.Icon;

import org.appwork.uio.CloseReason;
import org.appwork.uio.InputDialogInterface;
import org.appwork.uio.UIOManager;
import org.appwork.uio.UserIODefinition;
import org.appwork.uio.UserIOHandlerInterface;
import org.appwork.utils.swing.dialog.Dialog;
import org.appwork.utils.swing.dialog.DialogCanceledException;

/**
 * JD's input window, headless: a login's confirmation code (PluginForHost.getTwoFACode, e.g. an
 * XFileSharing hoster's "new sign in" mail code or Google 2FA) goes through getUserInput →
 * UIOManager. Only an accountCheck the user started asks: a `code` event, the daemon's
 * `answerCode` op answers. Any other input dialog is cancelled (JD's own headless handler hands
 * back the unshown dialog, which throws "Dialog has not been closed yet").
 */
final class CodeAsker implements UserIOHandlerInterface {
    private static final int TIMEOUT_S = 600;
    /** accountCheck thread → its task */
    private static final Map<Thread, Long> CHECKS = new ConcurrentHashMap<Thread, Long>();
    /** task → where its answer goes */
    private static final Map<Long, BlockingQueue<String>> WAITING = new ConcurrentHashMap<Long, BlockingQueue<String>>();
    private final UserIOHandlerInterface base;

    private CodeAsker(final UserIOHandlerInterface base) {
        this.base = base;
    }

    static void install() {
        UIOManager.setUserIO(new CodeAsker(UIOManager.I()));
    }

    /** The current thread checks a login for @p task: a code it needs goes to the user. */
    static void checking(final long task) {
        CHECKS.put(Thread.currentThread(), task);
    }

    static void done() {
        final Long task = CHECKS.remove(Thread.currentThread());
        if (task != null) {
            WAITING.remove(task);
        }
    }

    /** The user's code for @p task; empty = cancel. */
    static void answer(final long task, final String text) {
        final BlockingQueue<String> q = WAITING.get(task);
        if (q != null) {
            q.offer(text == null ? "" : text.trim());
        }
    }

    @Override
    public <T extends UserIODefinition> T show(final Class<T> cls, final T impl) {
        if (!(impl instanceof InputDialogInterface) || !cls.isInterface()) {
            return base.show(cls, impl);
        }
        final InputDialogInterface in = (InputDialogInterface) impl;
        final Long task = CHECKS.get(Thread.currentThread());
        if (task == null) {
            Out.log("JD input skipped: " + in.getMessage());
            return answered(cls, impl, null);
        }
        final BlockingQueue<String> q = new ArrayBlockingQueue<String>(1);
        WAITING.put(task, q);
        final Map<String, Object> m = Out.event(task, "code");
        m.put("title", safe(in::getTitle));
        // only a site's own words ("We noticed a new sign in..."); JD's stock line: the GUI's own
        final String text = safe(in::getMessage);
        final String stock = safe(org.jdownloader.gui.translate._GUI.T::jd_gui_swing_components_AccountDialog_2FA_login);
        m.put("text", text.equals(stock) ? "" : text);
        m.put("timeout", TIMEOUT_S);
        Out.send(m);
        String code = null;
        try {
            code = q.poll(TIMEOUT_S, TimeUnit.SECONDS);
        } catch (final InterruptedException e) {
            // the task was cancelled
            Thread.currentThread().interrupt();
        } finally {
            WAITING.remove(task);
        }
        return answered(cls, impl, code == null || code.isEmpty() ? null : code);
    }

    /** @p impl closed: OK with @p code, else cancelled. */
    @SuppressWarnings("unchecked")
    private static <T> T answered(final Class<T> cls, final Object impl, final String code) {
        final CloseReason reason = code != null ? CloseReason.OK : CloseReason.CANCEL;
        final InvocationHandler h = (proxy, method, args) -> {
            switch (method.getName()) {
            case "getCloseReason":
                return reason;
            case "getText":
                return code;
            case "throwCloseExceptions":
                if (code == null) {
                    throw new DialogCanceledException(Dialog.RETURN_CANCEL);
                }
                return null;
            case "setCloseReason":
                return null;
            case "isDontShowAgainSelected":
            case "isRemoteAPIEnabled":
                return false;
            case "hashCode":
                return System.identityHashCode(proxy);
            case "equals":
                return proxy == args[0];
            case "toString":
                return "CodeAsker answer";
            default:
                break;
            }
            // getMessage, getTitle, getLinkName ...: the dialog's own
            try {
                return method.invoke(impl, args);
            } catch (final InvocationTargetException | IllegalAccessException | IllegalArgumentException e) {
                return defaultOf(method);
            }
        };
        return (T) Proxy.newProxyInstance(cls.getClassLoader(), new Class<?>[] { cls }, h);
    }

    private static Object defaultOf(final Method m) {
        final Class<?> r = m.getReturnType();
        if (r == boolean.class) {
            return false;
        }
        if (r == int.class || r == long.class || r == short.class || r == byte.class) {
            return r == long.class ? (Object) 0L : (Object) 0;
        }
        return null;
    }

    private static String safe(final java.util.function.Supplier<String> s) {
        try {
            final String t = s.get();
            return t == null ? "" : t;
        } catch (final Throwable e) {
            return "";
        }
    }

    @Override
    public boolean showConfirmDialog(final int flag, final String title, final String message) {
        return base.showConfirmDialog(flag, title, message);
    }

    @Override
    public boolean showConfirmDialog(final int flags, final String title, final String message, final Icon icon, final String ok,
            final String cancel) {
        return base.showConfirmDialog(flags, title, message, icon, ok, cancel);
    }

    @Override
    public boolean showConfirmDialog(final int flags, final String title, final String message, final Icon icon, final String ok,
            final String cancel, final String dontShowAgainKey) {
        return base.showConfirmDialog(flags, title, message, icon, ok, cancel, dontShowAgainKey);
    }

    @Override
    public void showErrorMessage(final String message) {
        Out.log("JD error: " + message);
    }

    @Override
    public void showMessageDialog(final String message) {
        Out.log("JD message: " + message);
    }

    @Override
    public void showException(final String message, final Throwable e) {
        Out.log("JD exception: " + message);
    }
}
