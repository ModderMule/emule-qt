#pragma once

/// @file BundleEnvironment.h
/// @brief Keep the Linux tarball's library path out of the programs we start.
///
/// The tarball launcher points LD_LIBRARY_PATH at the bundled libs and leaves
/// what it replaced in EMULEQT_ORIG_LD_LIBRARY_PATH. A child that inherits the
/// bundle path loads our glib and OpenSSL over its own: on a newer distribution
/// xdg-open, the file manager and the video player all die in the loader.

#include <QByteArray>
#include <QtEnvironmentVariables>
#include <QtSystemDetection>

namespace eMule {

/// The launcher's path back in place. First thing in main(): the loader read
/// LD_LIBRARY_PATH when the process started, so our own libs and plugins still
/// resolve, and everything started from here on sees the system's.
inline void restoreSystemLibraryPath()
{
#ifdef Q_OS_LINUX
    if (qEnvironmentVariableIsEmpty("EMULEQT_BUNDLE_LIBS"))
        return;   // not started by the tarball launcher
    const QByteArray original = qgetenv("EMULEQT_ORIG_LD_LIBRARY_PATH");
    if (original.isEmpty())
        qunsetenv("LD_LIBRARY_PATH");
    else
        qputenv("LD_LIBRARY_PATH", original);
#endif
}

/// The bundle path again, for exec'ing our own binary: it goes past the launcher.
inline void reapplyBundleLibraryPath()
{
#ifdef Q_OS_LINUX
    const QByteArray bundle = qgetenv("EMULEQT_BUNDLE_LIBS");
    if (bundle.isEmpty())
        return;
    const QByteArray original = qgetenv("EMULEQT_ORIG_LD_LIBRARY_PATH");
    qputenv("LD_LIBRARY_PATH", original.isEmpty() ? bundle : bundle + ':' + original);
#endif
}

} // namespace eMule
