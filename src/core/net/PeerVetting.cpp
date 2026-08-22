/// @file PeerVetting.cpp

#include "net/PeerVetting.h"

#include "client/ClientList.h"
#include "ipfilter/IPFilter.h"
// isGoodIP(const Address&) is declared in net/Address.h, already pulled in by the header.

namespace eMule {

Address vetPeerAddress(const Address& addr, const IPFilter* filter, const ClientList* clients)
{
    if (addr.isNull())
        return {};
    if (!isGoodIP(addr))
        return {};
    if (filter && filter->isFiltered(addr))
        return {};
    if (clients && clients->isBannedClient(addr))
        return {};
    return addr;
}

} // namespace eMule
