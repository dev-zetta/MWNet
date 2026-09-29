#ifndef MWNET_DISCOVERY_PROBE_HPP
#define MWNET_DISCOVERY_PROBE_HPP
#include "Protocol.hpp"
#include <atomic>
namespace mwmp::discovery
{
    bool probe(const Listing& listing, const std::string& fingerprint, bool localTest,
        const std::atomic_bool& cancel);
}
#endif
