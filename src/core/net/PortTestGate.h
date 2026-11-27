#pragma once

/// @file PortTestGate.h
/// @brief The window of a running eD2K port test (#266, not MFC).
///
/// The test server dials from a hosting range, which a strict IP filter (level 127 with
/// emule-security lists) blocks: the core closed the test link at once and the website said
/// "encrypted connection failed". While the window is open:
/// - the test server's own addresses (porttest/source, plugin >= 1.9.0) pass the IP filter
/// - every other incoming TCP/UDP peer the filter rejects is recorded, so the GUI can name
///   the filter when the test fails anyway (old server, server behind NAT)
///
/// Thread-safe: the UDP socket may check from another thread than the daemon's.

#include "net/Address.h"

#include <QString>

#include <vector>

namespace eMule::PortTestGate {

/// An IP filter hit during the window.
struct Hit {
    Address address;
    QString description;   ///< the filter range's description
    bool tcp = false;      ///< else UDP
};

/// Opens the window for @p durationMs (a safety net; close() ends it): @p exempt pass the IP
/// filter, hits so far are dropped.
void open(std::vector<Address> exempt, int durationMs);

/// Ends the window; returns the hits it recorded.
std::vector<Hit> close();

/// The window is open and @p address is a test server's.
[[nodiscard]] bool isExempt(const Address& address);

/// Records a filtered incoming peer while the window is open; no-op otherwise.
void noteFiltered(const Address& address, const QString& description, bool tcp);

} // namespace eMule::PortTestGate
