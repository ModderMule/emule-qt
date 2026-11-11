#include "pch.h"
#include "utils/AppActivation.h"

#ifdef Q_OS_MACOS
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace eMule {

void activateApp()
{
#ifdef Q_OS_MACOS
    // objc runtime instead of an .mm: [NSApp activate] (macOS 14+), else
    // [NSApp activateIgnoringOtherApps:YES]
    using SendId = id (*)(id, SEL);
    using SendVoid = void (*)(id, SEL);
    using SendBool = void (*)(id, SEL, BOOL);
    const id app = reinterpret_cast<SendId>(objc_msgSend)(
        reinterpret_cast<id>(objc_getClass("NSApplication")), sel_registerName("sharedApplication"));
    if (!app)
        return;
    if (const SEL activate = sel_registerName("activate"); class_respondsToSelector(object_getClass(app), activate))
        reinterpret_cast<SendVoid>(objc_msgSend)(app, activate);
    else
        reinterpret_cast<SendBool>(objc_msgSend)(app, sel_registerName("activateIgnoringOtherApps:"), YES);
#endif
}

} // namespace eMule
