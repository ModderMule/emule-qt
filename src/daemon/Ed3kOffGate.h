#pragma once

/// @file Ed3kOffGate.h
/// @brief While ed3k is switched off, nothing under config/ed3k is written (#88).
///
/// Header only: the ed3k bridges are also built one by one into tests.

#include "IpcMessage.h"
#include "prefs/Preferences.h"

#include <QCborMap>
#include <QCoreApplication>

#include <algorithm>
#include <functional>
#include <initializer_list>

namespace eMule::Ed3kBridge {

/// ed3k switched off (pref ed3kEnabled): a request that is not one of @p reads is answered
/// failed ("ed3k is off.") so that nothing under config/ed3k is created. True: answered.
inline bool refuseWhileOff(const Ipc::IpcMessage& msg, std::initializer_list<Ipc::IpcMsgType> reads,
                           const std::function<void(const Ipc::IpcMessage&)>& reply)
{
    if (thePrefs.ed3kEnabled() || std::ranges::find(reads, msg.type()) != reads.end())
        return false;
    reply(Ipc::IpcMessage::makeResult(
        msg.seqId(), false,
        QCborMap{{QStringLiteral("error"), QCoreApplication::translate("eMule::Ed3kBridge", "ed3k is off.")}}));
    return true;
}

} // namespace eMule::Ed3kBridge
