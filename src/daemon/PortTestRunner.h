#pragma once

/// @file PortTestRunner.h
/// @brief TestPorts (IPC 224): the BitTorrent or ed3k port test through the emule-qt.org API,
///        run from the daemon so its own public address is the one tested.
///
/// ed3k: a PROBE sealed to the node itself goes along; the server sends it, and after the
/// reply the node is asked for up to kSeenWaitMs whether it arrived (PortTestApi.h).

#include <QCborMap>
#include <QString>

#include <functional>

namespace eMule::PortTestRunner {

/// Ok: the result map {network, tcpPort?, udpPort, targets}; else a PortTestApi::Failure and
/// an English detail (IPC stays untranslated; the GUI words the code).
using Done = std::function<void(bool ok, const QCborMap& result, int failure, const QString& detail)>;

/// Starts the test of @p network (PortTestApi::Network). @p done runs once, on this thread.
void run(int network, Done done);

} // namespace eMule::PortTestRunner
