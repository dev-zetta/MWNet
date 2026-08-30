#ifndef OPENMW_MP_TRANSPORT_SECURE_TRANSPORT_HPP
#define OPENMW_MP_TRANSPORT_SECURE_TRANSPORT_HPP

#include "ITransport.hpp"

#include <components/openmw-mp/Security/ServerIdentity.hpp>
#include <components/openmw-mp/Security/TrustStore.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace mwmp::transport
{
    class SecureTransport final : public ITransport
    {
    public:
        SecureTransport(std::unique_ptr<ITransport> transport, security::ServerIdentity identity);
        SecureTransport(std::unique_ptr<ITransport> transport, security::TrustStore trustStore);
        ~SecureTransport() override;

        SecureTransport(const SecureTransport&) = delete;
        SecureTransport& operator=(const SecureTransport&) = delete;

        bool listen(const ListenOptions& options, TransportError& error) override;
        bool connect(const ConnectOptions& options, TransportConnectionId& connection,
            TransportError& error) override;
        bool send(TransportMessage message, TransportError& error) override;
        std::optional<TransportEvent> poll(std::chrono::milliseconds timeout) override;
        std::optional<std::string> peerAddress(
            TransportConnectionId connection) const override;
        void disconnect(TransportConnectionId connection) override;
        void shutdown(std::chrono::milliseconds timeout) override;

        bool confirmFingerprint(TransportConnectionId connection, std::string_view fingerprint,
            TransportError& error);
        std::optional<std::string> serverFingerprint() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
