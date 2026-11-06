// eMuleQt hoster resolver: JDownloader plugins driven by emulecored.
// GPL v3 or later, see ../../../../LICENSE
package org.emuleqt.resolver;

import javax.swing.Icon;
import javax.swing.ListCellRenderer;

import jd.gui.UserIO;

/**
 * JD's dialogs answered at once: no Swing, no console (container plugins show a message for a
 * bad file, which would hang a headless task). Messages go to the log; questions say cancel.
 */
final class HeadlessUserIO extends UserIO {
    static void install() {
        UserIO.setInstance(new HeadlessUserIO());
    }

    @Override
    public int requestConfirmDialog(final int flag, final String title, final String message, final Icon icon, final String okOption,
            final String cancelOption) {
        Out.log("JD dialog skipped: " + message);
        return UserIO.RETURN_CANCEL;
    }

    @Override
    public void requestMessageDialog(final int flag, final String title, final String message) {
        // the base looks up an icon first
        Out.log("JD message: " + message);
    }

    @Override
    public String requestInputDialog(final int flag, final String title, final String message, final String defaultMessage, final Icon icon,
            final String okOption, final String cancelOption) {
        Out.log("JD input skipped: " + message);
        return null;
    }

    @Override
    public int requestComboDialog(final int flag, final String title, final String question, final Object[] options, final int defaultSelection,
            final Icon icon, final String okText, final String cancelText, final ListCellRenderer renderer) {
        return -1;
    }

    @Override
    public int[] requestMultiSelectionDialog(final int flag, final String title, final String question, final Object[] options,
            final int[] preSelection, final Icon icon, final String okText, final String cancelText, final ListCellRenderer renderer) {
        return null;
    }
}
