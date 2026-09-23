#include "Probe.hpp"
#include <components/openmw-mp/Transport/GameEndpoint.hpp>
#include <boost/asio/ip/address.hpp>

namespace mwmp::discovery
{
    bool probe(const Listing& listing, const std::string& fingerprint, bool localTest,
        const std::atomic_bool& cancel)
    {
        boost::system::error_code ec;
        auto address = boost::asio::ip::make_address(listing.host, ec);
        // Numeric addresses deliberately avoid DNS rebinding and unbounded resolver work.
        if (ec || address.is_unspecified() || address.is_multicast()
            || (!localTest && !publicAddress(listing.host)) || cancel) return false;
        auto endpoint = transport::GameEndpoint::createProbe();
        transport::ConnectOptions options;
        options.host = address.to_string(); options.port = listing.port;
        options.expectedFingerprint = fingerprint;
        options.timeouts.connect = std::chrono::seconds(3);
        options.timeouts.handshake = std::chrono::seconds(3);
        transport::TransportConnectionId connection;
        transport::TransportError error;
        if (!endpoint->connect(options,connection,error)) return false;
        bool result = false;
        const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(4);
        while (!cancel && std::chrono::steady_clock::now() < deadline)
        {
            auto event = endpoint->poll(std::chrono::milliseconds(20));
            if (!event) continue;
            if (event->type == transport::TransportEventType::TrustRequired
                || event->type == transport::TransportEventType::Connected)
            { result = event->detail == fingerprint; break; }
            if (event->type == transport::TransportEventType::Disconnected) break;
        }
        endpoint->disconnect(connection);
        endpoint->shutdown(std::chrono::milliseconds(100));
        return result;
    }
}
