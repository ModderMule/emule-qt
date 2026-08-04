#include "pch.h"
/// @file StatusBarNotifier.cpp
/// @brief The status-bar sink — see StatusBarNotifier.h.

#include "utils/StatusBarNotifier.h"

#include <utility>

namespace eMule::StatusBarNotifier {

namespace {

/// Function-local static so the sink is alive before any static initialiser can post.
Sink& sinkRef()
{
    static Sink sink;
    return sink;
}

} // anonymous namespace

void setSink(Sink sink)
{
    sinkRef() = std::move(sink);
}

void post(const QString& text, int timeoutMs)
{
    if (const Sink& sink = sinkRef(); sink && !text.isEmpty())
        sink(text, timeoutMs);
}

} // namespace eMule::StatusBarNotifier
